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

/*
 * mod-profession-xp — the hooks. Each one hands over to ProfessionXPMgr.
 */

#include "Player.h"
#include "ProfessionXP.h"
#include "ScriptMgr.h"

class ProfessionXPWorldScript : public WorldScript
{
public:
    ProfessionXPWorldScript() : WorldScript("ProfessionXPWorldScript",
        {
            WORLDHOOK_ON_AFTER_CONFIG_LOAD
        }) { }

    // At startup and on every `.reload config`.
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sProfessionXPMgr->LoadConfig();
    }
};

class ProfessionXPPlayerScript : public PlayerScript
{
public:
    ProfessionXPPlayerScript() : PlayerScript("ProfessionXPPlayerScript",
        {
            // THIS LIST GATES THE CALLS: an overridden method missing from
            // here compiles and never fires.
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_DELETE_FROM_DB,
            PLAYERHOOK_ON_CREATE_ITEM,
            PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
            PLAYERHOOK_ON_UPDATE_GATHERING_SKILL,
            PLAYERHOOK_ON_UPDATE_FISHING_SKILL,
            PLAYERHOOK_ON_LOOT_ITEM,
            // The profession window's requests: addon whispers to oneself.
            PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
        }) { }

    // Only the private overload is ours; the others stay the base class's.
    using PlayerScript::OnPlayerCanUseChat;

    void OnPlayerLogin(Player* player) override
    {
        sProfessionXPMgr->Load(player);
    }

    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction trans, uint32 guid) override
    {
        sProfessionXPMgr->DeleteCharacter(trans, guid);
    }

    // Fires right before the crafting hook below, for the item just made.
    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        sProfessionXPMgr->OnCreateItem(player, item);
    }

    // Before the skill-up roll: `currentLevel` is still the value the colour
    // was shown with. `gain` is left alone.
    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* skill,
                                     uint32 currentLevel, uint32& /*gain*/) override
    {
        sProfessionXPMgr->OnCraft(player, skill, currentLevel);
    }

    void OnPlayerUpdateGatheringSkill(Player* player, uint32 skillId, uint32 current,
                                      uint32 gray, uint32 green, uint32 yellow, uint32& /*gain*/) override
    {
        sProfessionXPMgr->OnGather(player, skillId, current, gray, green, yellow);
    }

    // A boolean hook: false would cancel the skill-up roll. Always true.
    bool OnPlayerUpdateFishingSkill(Player* player, int32 skill, int32 zoneSkill,
                                    int32 chance, int32 roll) override
    {
        sProfessionXPMgr->OnFish(player, skill, zoneSkill, chance, roll);
        return true;
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid lootGuid) override
    {
        sProfessionXPMgr->OnLoot(player, item, lootGuid);
    }

    // `ProfXP<TAB>request`, whispered by the window script to its own
    // character: answered, and swallowed. Any other whisper goes on.
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;
        return !sProfessionXPMgr->HandleAddonMessage(player, msg);
    }
};

void AddSC_profession_xp_scripts()
{
    new ProfessionXPWorldScript();
    new ProfessionXPPlayerScript();
}
