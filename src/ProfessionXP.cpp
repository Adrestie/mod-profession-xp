/*
 * This file is part of mod-profession-xp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ProfessionXP.h"

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryResult.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>

// OPTIONAL INTEROPERABILITY, decided by CMake (mod-profession-xp.cmake): the
// define only exists when mod-spheregrid is linked into the same binary.
#if defined(MOD_PROFESSION_XP_SPHEREGRID)
#include "SphereGridPlayerMgr.h"
#endif

// Bots craft and gather too; they earn nothing and are not even tracked.
#if defined(MOD_PLAYERBOTS)
#include "Playerbots.h"
static bool IsBot(Player* player) { return GET_PLAYERBOT_AI(player) != nullptr; }
#else
static bool IsBot(Player* /*player*/) { return false; }
#endif

namespace
{
    constexpr char DATA_KEY[] = "mod-profession-xp";

    uint64 DoneKey(ProfessionXPKind kind, uint32 entry)
    {
        return (uint64(kind) << 32) | entry;
    }

    // THE COLOUR OF A RECIPE, as the client paints it. A recipe stays orange
    // below its yellow threshold (TrivialSkillLineRankLow) and turns gray at
    // TrivialSkillLineRankHigh; green starts halfway, which is also where the
    // core's skill-up chance crosses 50% (Player::CraftSkillGainChance).
    ProfessionXPColor CraftColor(SkillLineAbilityEntry const* ability, uint32 current)
    {
        uint32 const gray = ability->TrivialSkillLineRankHigh;
        uint32 const yellow = ability->TrivialSkillLineRankLow;

        // Thresholds the core cannot interpolate: it treats them as orange
        // then gray, so do we.
        if (gray <= yellow)
            return current < gray ? PROFESSION_XP_ORANGE : PROFESSION_XP_GRAY;

        uint32 const green = (gray + yellow) / 2;
        if (current >= gray)
            return PROFESSION_XP_GRAY;
        if (current >= green)
            return PROFESSION_XP_GREEN;
        if (current >= yellow)
            return PROFESSION_XP_YELLOW;
        return PROFESSION_XP_ORANGE;
    }

    // THE COLOUR OF A NODE OR A CORPSE: the core hands us its three thresholds,
    // the ones Player::SkillGainChance reads.
    ProfessionXPColor GatherColor(uint32 current, uint32 gray, uint32 green, uint32 yellow)
    {
        if (current >= gray)
            return PROFESSION_XP_GRAY;
        if (current >= green)
            return PROFESSION_XP_GREEN;
        if (current >= yellow)
            return PROFESSION_XP_YELLOW;
        return PROFESSION_XP_ORANGE;
    }

    // Fishing has no colour in the game. We give it one from the zone's base
    // level, on the scale of the other gathering skills, gray falling where the
    // core stops letting fish escape (zone + 95).
    ProfessionXPColor FishingColor(int32 skill, int32 zoneSkill)
    {
        if (skill >= zoneSkill + 95)
            return PROFESSION_XP_GRAY;
        if (skill >= zoneSkill + 50)
            return PROFESSION_XP_GREEN;
        if (skill >= zoneSkill + 25)
            return PROFESSION_XP_YELLOW;
        return PROFESSION_XP_ORANGE;
    }

    // Slot of a gathering skill in ProfessionXPPlayerData::lastGatherColor,
    // -1 for a skill the module does not reward.
    int GatherSlot(uint32 skill)
    {
        switch (skill)
        {
            case SKILL_MINING:    return 0;
            case SKILL_HERBALISM: return 1;
            case SKILL_SKINNING:  return 2;
            case SKILL_FISHING:   return 3;
            default:              return -1;
        }
    }

    // THE SKILL A LOOT COMES FROM, judged on its source rather than on the
    // last action: a mining or herbalism node by its lock, a corpse by its
    // skinning loot, a bobber or a school by its type. Anything else — a
    // corpse, a chest, a container — is SKILL_NONE.
    uint32 LootSkill(Player* player, ObjectGuid lootGuid)
    {
        if (lootGuid.IsGameObject())
        {
            GameObject* go = ObjectAccessor::GetGameObject(*player, lootGuid);
            if (!go)
                return SKILL_NONE;

            GameObjectTemplate const* info = go->GetGOInfo();
            if (info->type == GAMEOBJECT_TYPE_FISHINGNODE || info->type == GAMEOBJECT_TYPE_FISHINGHOLE)
                return SKILL_FISHING;

            if (LockEntry const* lock = sLockStore.LookupEntry(info->GetLockId()))
                for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
                    if (lock->Type[i] == LOCK_KEY_SKILL)
                    {
                        SkillType const skill = SkillByLockType(LockType(lock->Index[i]));
                        if (skill == SKILL_MINING || skill == SKILL_HERBALISM)
                            return skill;
                    }
            return SKILL_NONE;
        }

        if (lootGuid.IsCreatureOrVehicle())
        {
            Creature* creature = ObjectAccessor::GetCreature(*player, lootGuid);
            if (creature && creature->loot.loot_type == LOOT_SKINNING)
                return creature->GetCreatureTemplate()->GetRequiredLootSkill();
        }
        return SKILL_NONE;
    }
}

ProfessionXPMgr* ProfessionXPMgr::instance()
{
    static ProfessionXPMgr instance;
    return &instance;
}

void ProfessionXPMgr::LoadConfig()
{
    ProfessionXPConfig c;

    c.enable = sConfigMgr->GetOption<bool>("ProfessionXP.Enable", true);
    c.activity[PROFESSION_XP_CRAFTING] = sConfigMgr->GetOption<bool>("ProfessionXP.Crafting.Enable", true);
    c.activity[PROFESSION_XP_GATHERING] = sConfigMgr->GetOption<bool>("ProfessionXP.Gathering.Enable", true);
    c.activity[PROFESSION_XP_FISHING] = sConfigMgr->GetOption<bool>("ProfessionXP.Fishing.Enable", true);
    c.rate[PROFESSION_XP_CRAFTING] = sConfigMgr->GetOption<float>("ProfessionXP.Crafting.Rate", 1.0f);
    c.rate[PROFESSION_XP_GATHERING] = sConfigMgr->GetOption<float>("ProfessionXP.Gathering.Rate", 1.0f);
    c.rate[PROFESSION_XP_FISHING] = sConfigMgr->GetOption<float>("ProfessionXP.Fishing.Rate", 0.5f);
    c.rewardAtRankCap = sConfigMgr->GetOption<bool>("ProfessionXP.RewardAtRankCap", false);

    c.xpPercent[PROFESSION_XP_GREEN] = sConfigMgr->GetOption<float>("ProfessionXP.Xp.Green", 1.0f);
    c.xpPercent[PROFESSION_XP_YELLOW] = sConfigMgr->GetOption<float>("ProfessionXP.Xp.Yellow", 2.0f);
    c.xpPercent[PROFESSION_XP_ORANGE] = sConfigMgr->GetOption<float>("ProfessionXP.Xp.Orange", 3.0f);
    c.xpFirstTime = sConfigMgr->GetOption<float>("ProfessionXP.Xp.FirstTime", 10.0f);
    c.xpMultiplier = sConfigMgr->GetOption<float>("ProfessionXP.Xp.Multiplier", 1.0f);
    c.xpMaxPercent = sConfigMgr->GetOption<float>("ProfessionXP.Xp.MaxPercent", 15.0f);

    c.firstTime = sConfigMgr->GetOption<bool>("ProfessionXP.FirstTime.Enable", true);
    c.firstTimeOnGray = sConfigMgr->GetOption<bool>("ProfessionXP.FirstTime.Gray", false);
    c.announce = sConfigMgr->GetOption<bool>("ProfessionXP.FirstTime.Announce", true);

    c.spherite = sConfigMgr->GetOption<bool>("ProfessionXP.Spherite.Enable", true);
    c.spheriteAmount[PROFESSION_XP_GREEN] = sConfigMgr->GetOption<uint32>("ProfessionXP.Spherite.Green", 2);
    c.spheriteAmount[PROFESSION_XP_YELLOW] = sConfigMgr->GetOption<uint32>("ProfessionXP.Spherite.Yellow", 4);
    c.spheriteAmount[PROFESSION_XP_ORANGE] = sConfigMgr->GetOption<uint32>("ProfessionXP.Spherite.Orange", 6);
    c.spheriteFirstTime = sConfigMgr->GetOption<uint32>("ProfessionXP.Spherite.FirstTime", 25);

    // Negative values make no sense anywhere: they read as zero.
    for (float& r : c.rate)
        r = std::max(r, 0.0f);
    for (float& p : c.xpPercent)
        p = std::max(p, 0.0f);
    c.xpPercent[PROFESSION_XP_GRAY] = 0.0f;
    c.spheriteAmount[PROFESSION_XP_GRAY] = 0;
    c.xpFirstTime = std::max(c.xpFirstTime, 0.0f);
    c.xpMultiplier = std::max(c.xpMultiplier, 0.0f);
    c.xpMaxPercent = std::max(c.xpMaxPercent, 0.0f);

    _config = c;

#if !defined(MOD_PROFESSION_XP_SPHEREGRID)
    if (_config.enable && _config.spherite)
        LOG_INFO("server.loading", ">> mod-profession-xp: built without mod-spheregrid, no reward at the maximum level.");
#endif
}

ProfessionXPPlayerData* ProfessionXPMgr::Data(Player* player)
{
    return player ? player->CustomData.Get<ProfessionXPPlayerData>(DATA_KEY) : nullptr;
}

void ProfessionXPMgr::Load(Player* player)
{
    if (!player || IsBot(player))
        return;

    auto* data = new ProfessionXPPlayerData();
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT `kind`, `entry` FROM `mod_profession_xp_character_done` WHERE `guid` = {}",
            player->GetGUID().GetCounter()))
    {
        do
        {
            Field* fields = result->Fetch();
            data->done.insert(DoneKey(ProfessionXPKind(fields[0].Get<uint8>()), fields[1].Get<uint32>()));
        } while (result->NextRow());
    }
    player->CustomData.Set(DATA_KEY, data);
}

void ProfessionXPMgr::DeleteCharacter(CharacterDatabaseTransaction trans, uint32 guid)
{
    trans->Append("DELETE FROM `mod_profession_xp_character_done` WHERE `guid` = {}", guid);
}

bool ProfessionXPMgr::MarkFirst(Player* player, ProfessionXPPlayerData* data, ProfessionXPKind kind, uint32 entry)
{
    if (!entry || !data->done.insert(DoneKey(kind, entry)).second)
        return false;

    // The memory is the reference from now on: the write may lag behind, a
    // second craft in the same second already finds the entry.
    CharacterDatabase.Execute(
        "INSERT IGNORE INTO `mod_profession_xp_character_done` (`guid`, `kind`, `entry`) VALUES ({}, {}, {})",
        player->GetGUID().GetCounter(), uint32(kind), entry);

    // The profession window stops promising the bonus at once.
    SendAddon(player, Acore::StringFormat("A\t{}:{}", uint32(kind), entry));
    return true;
}

/*
 * THE WINDOW SCRIPT'S PROTOCOL. Each line is one addon whisper, prefix
 * PROFESSION_XP_ADDON_PREFIX, fields separated by tabs.
 *
 *   client -> server
 *     H            hello: the texts, the settings and the done list
 *     S            the settings alone (the window opens: level, prisms,
 *                  experience lock and a reloaded .conf may have changed)
 *
 *   server -> client
 *     T id text    one text of the window, in the client's language
 *     C a|b|...    the settings, see SendSettings
 *     R            the done list starts over
 *     D k:e,k:e    a slice of the done list (kind:entry)
 *     E            the done list is complete
 *     A k:e        one more entry done
 *
 * A line stays well under the 255 characters of a chat message.
 */
void ProfessionXPMgr::SendAddon(Player* player, std::string const& body)
{
    std::string const message = Acore::StringFormat("{}\t{}", PROFESSION_XP_ADDON_PREFIX, body);
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, message);
    player->SendDirectMessage(&packet);
}

void ProfessionXPMgr::SendTexts(Player* player)
{
    WorldSession* session = player->GetSession();
    for (uint32 id = PROFESSION_XP_STR_WINDOW_FIRST; id <= PROFESSION_XP_STR_WINDOW_LAST; ++id)
    {
        // Only the ids that exist: the localised lookup logs an error and
        // answers "error" for the others, and the range has room to grow.
        if (!sObjectMgr->GetModuleString(PROFESSION_XP_MODULE, id))
            continue;
        if (std::string const* text = session->GetModuleString(PROFESSION_XP_MODULE, id))
            SendAddon(player, Acore::StringFormat("T\t{}\t{}", id, *text));
    }
}

void ProfessionXPMgr::SendSettings(Player* player)
{
    ProfessionXPConfig const& c = _config;

    // What the maximum level pays: 0 = built without mod-spheregrid, 1 = turned
    // off in the configuration, 2 = Spherite. `boost` is what 10000 Spherite
    // become with the account's prisms.
    uint32 spherite = 0;
    uint32 boost = 10000;
#if defined(MOD_PROFESSION_XP_SPHEREGRID)
    spherite = c.spherite ? 2 : 1;
    boost = sSphereGridPlayerMgr->Boosted(player, 10000);
#endif

    // The order of the fields is the contract with ProfessionXP_Client.lua
    // (function Settings); the first one is its version.
    SendAddon(player, Acore::StringFormat(
        "C\t1|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
        (c.enable && c.activity[PROFESSION_XP_CRAFTING]) ? 1 : 0,
        c.rate[PROFESSION_XP_CRAFTING],
        c.xpPercent[PROFESSION_XP_GREEN], c.xpPercent[PROFESSION_XP_YELLOW], c.xpPercent[PROFESSION_XP_ORANGE],
        c.xpFirstTime, c.xpMultiplier, c.xpMaxPercent,
        c.firstTime ? 1 : 0, c.firstTimeOnGray ? 1 : 0,
        c.rewardAtRankCap ? 1 : 0, sWorld->GetConfigMaxSkillValue(),
        sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL),
        spherite,
        c.spheriteAmount[PROFESSION_XP_GREEN], c.spheriteAmount[PROFESSION_XP_YELLOW],
        c.spheriteAmount[PROFESSION_XP_ORANGE], c.spheriteFirstTime,
        boost,
        player->HasPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN) ? 1 : 0));
}

void ProfessionXPMgr::SendDone(Player* player, ProfessionXPPlayerData const* data)
{
    SendAddon(player, "R");
    std::string slice;
    for (uint64 key : data->done)
    {
        std::string const entry = Acore::StringFormat("{}:{}", uint32(key >> 32), uint32(key & 0xFFFFFFFF));
        if (slice.size() + entry.size() + 1 > 200)
        {
            SendAddon(player, "D\t" + slice);
            slice.clear();
        }
        if (!slice.empty())
            slice += ',';
        slice += entry;
    }
    if (!slice.empty())
        SendAddon(player, "D\t" + slice);
    SendAddon(player, "E");
}

bool ProfessionXPMgr::HandleAddonMessage(Player* player, std::string const& message)
{
    std::string_view const prefix = PROFESSION_XP_ADDON_PREFIX;
    if (message.size() <= prefix.size() || message.compare(0, prefix.size(), prefix) != 0 ||
        message[prefix.size()] != '\t')
        return false;

    // Ours from here on, whatever happens: never echoed back as a whisper.
    ProfessionXPPlayerData const* data = Data(player);
    if (!data)
        return true;

    std::string_view const request = std::string_view(message).substr(prefix.size() + 1);
    if (request == "H")
    {
        SendTexts(player);
        SendSettings(player);
        SendDone(player, data);
    }
    else if (request == "S")
        SendSettings(player);
    return true;
}

bool ProfessionXPMgr::SkillHasRoom(Player* player, uint32 skill) const
{
    if (_config.rewardAtRankCap)
        return true;

    // At the cap of its rank, a skill learns nothing until the next trainer:
    // without this, one yellow recipe could be crafted forever for the same
    // reward. The last rank is the exception, where the colours never move.
    uint32 const value = player->GetPureSkillValue(skill);
    return value < player->GetPureMaxSkillValue(skill) || value >= sWorld->GetConfigMaxSkillValue();
}

void ProfessionXPMgr::Grant(Player* player, float percent, uint32 spherite)
{
    if (player->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
    {
        percent = std::min(percent * _config.xpMultiplier, _config.xpMaxPercent);
        if (percent <= 0.0f)
            return;

        // A share of what the NEXT level costs: the same craft keeps the same
        // weight from level 10 to level 79.
        uint32 const next = player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        uint32 const xp = std::max<uint32>(1, uint32(std::lround(double(next) * percent / 100.0)));
        player->GiveXP(xp, nullptr);
        return;
    }

#if defined(MOD_PROFESSION_XP_SPHEREGRID)
    // Earn applies the account's prisms and tells the player.
    if (_config.spherite && spherite)
        sSphereGridPlayerMgr->Earn(player, spherite);
#else
    (void)spherite;
#endif
}

void ProfessionXPMgr::Announce(Player* player, uint32 text, ProfessionXPKind kind, uint32 entry)
{
    if (!_config.announce)
        return;

    WorldSession* session = player->GetSession();
    std::string name;
    if (kind == PROFESSION_XP_KIND_ITEM)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!proto)
            return;
        name = proto->Name1;
        if (ItemLocale const* locale = sObjectMgr->GetItemLocale(entry))
            ObjectMgr::GetLocaleString(locale->Name, session->GetSessionDbLocaleIndex(), name);
    }
    else
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(entry);
        if (!spell)
            return;
        char const* localized = spell->SpellName[session->GetSessionDbcLocale()];
        name = (localized && *localized) ? localized : spell->SpellName[LOCALE_enUS];
    }

    ChatHandler(session).PSendModuleSysMessage(PROFESSION_XP_MODULE, text, name);
}

void ProfessionXPMgr::OnCreateItem(Player* player, Item* item)
{
    ProfessionXPPlayerData* data = Data(player);
    if (!data || !item)
        return;

    data->pendingItem = item->GetEntry();
    data->pendingTick = GameTime::GetGameTimeMS();
}

void ProfessionXPMgr::OnCraft(Player* player, SkillLineAbilityEntry const* ability, uint32 current)
{
    ProfessionXPPlayerData* data = Data(player);
    if (!data)
        return;

    std::chrono::milliseconds const now = GameTime::GetGameTimeMS();

    // The item this very craft created, if any. Taken in every case, so that
    // a leftover never sticks to a later craft.
    uint32 itemEntry = 0;
    if (data->pendingItem && data->pendingTick == now)
        itemEntry = data->pendingItem;
    data->pendingItem = 0;

    if (!_config.enable || !_config.activity[PROFESSION_XP_CRAFTING] || !ability)
        return;

    // Professions only: conjuring a mage's water goes through the same hook,
    // under a class skill line.
    SkillLineEntry const* line = sSkillLineStore.LookupEntry(ability->SkillLine);
    if (!line || (line->categoryId != SKILL_CATEGORY_PROFESSION && line->categoryId != SKILL_CATEGORY_SECONDARY))
        return;

    // Disenchanting destroys rather than makes.
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
    if (!spell || spell->HasEffect(SPELL_EFFECT_DISENCHANT))
        return;

    ProfessionXPColor const color = CraftColor(ability, current);

    bool const sameCast = data->lastCraftSpell == ability->Spell && data->lastCraftTick == now;
    data->lastCraftSpell = ability->Spell;
    data->lastCraftTick = now;

    float percent = 0.0f;
    uint32 spherite = 0;
    if (!sameCast && color != PROFESSION_XP_GRAY && SkillHasRoom(player, ability->SkillLine))
    {
        float const rate = _config.rate[PROFESSION_XP_CRAFTING];
        percent += _config.xpPercent[color] * rate;
        spherite += uint32(std::lround(_config.spheriteAmount[color] * rate));
    }

    ProfessionXPKind const kind = itemEntry ? PROFESSION_XP_KIND_ITEM : PROFESSION_XP_KIND_SPELL;
    uint32 const entry = itemEntry ? itemEntry : ability->Spell;
    if (MarkFirst(player, data, kind, entry) && _config.firstTime &&
        (color != PROFESSION_XP_GRAY || _config.firstTimeOnGray))
    {
        percent += _config.xpFirstTime;
        spherite += _config.spheriteFirstTime;
        Announce(player, PROFESSION_XP_STR_FIRST_CRAFT, kind, entry);
    }

    Grant(player, percent, spherite);
}

void ProfessionXPMgr::OnGather(Player* player, uint32 skill, uint32 current, uint32 gray, uint32 green, uint32 yellow)
{
    // Prospecting, milling and lockpicking go through the same hook.
    if (skill != SKILL_MINING && skill != SKILL_HERBALISM && skill != SKILL_SKINNING)
        return;

    ProfessionXPPlayerData* data = Data(player);
    if (!data || !_config.enable || !_config.activity[PROFESSION_XP_GATHERING])
        return;

    ProfessionXPColor const color = GatherColor(current, gray, green, yellow);
    data->lastGatherColor[GatherSlot(skill)] = color;

    // The core calls us once per node and per respawn, once per corpse.
    if (color == PROFESSION_XP_GRAY || !SkillHasRoom(player, skill))
        return;

    float const rate = _config.rate[PROFESSION_XP_GATHERING];
    Grant(player, _config.xpPercent[color] * rate, uint32(std::lround(_config.spheriteAmount[color] * rate)));
}

void ProfessionXPMgr::OnFish(Player* player, int32 skill, int32 zoneSkill, int32 chance, int32 roll)
{
    ProfessionXPPlayerData* data = Data(player);
    if (!data || !_config.enable || !_config.activity[PROFESSION_XP_FISHING])
        return;

    // A zone without a fishing level is a gap in the data, not a gift.
    if (!zoneSkill)
        return;

    ProfessionXPColor const color = FishingColor(skill, zoneSkill);
    data->lastGatherColor[GatherSlot(SKILL_FISHING)] = color;

    // The fish got away: nothing.
    if (chance < roll)
        return;

    if (color == PROFESSION_XP_GRAY || !SkillHasRoom(player, SKILL_FISHING))
        return;

    float const rate = _config.rate[PROFESSION_XP_FISHING];
    Grant(player, _config.xpPercent[color] * rate, uint32(std::lround(_config.spheriteAmount[color] * rate)));
}

void ProfessionXPMgr::OnLoot(Player* player, Item* item, ObjectGuid lootGuid)
{
    ProfessionXPPlayerData* data = Data(player);
    if (!data || !item || !_config.enable || !_config.firstTime)
        return;

    uint32 const skill = LootSkill(player, lootGuid);
    int const slot = GatherSlot(skill);
    if (slot < 0)
        return;

    ProfessionXPActivity const activity = skill == SKILL_FISHING ? PROFESSION_XP_FISHING : PROFESSION_XP_GATHERING;
    if (!_config.activity[activity])
        return;

    if (!MarkFirst(player, data, PROFESSION_XP_KIND_ITEM, item->GetEntry()))
        return;

    // A gray node teaches nothing new: its first time is recorded, unpaid.
    if (data->lastGatherColor[slot] == PROFESSION_XP_GRAY && !_config.firstTimeOnGray)
        return;

    Announce(player, PROFESSION_XP_STR_FIRST_GATHER, PROFESSION_XP_KIND_ITEM, item->GetEntry());
    Grant(player, _config.xpFirstTime, _config.spheriteFirstTime);
}
