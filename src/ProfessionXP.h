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
 * mod-profession-xp — experience for crafting and gathering.
 *
 * Every craft, every node, every skinned corpse and every fish caught is
 * judged by the colour the game itself gives it, read from the very thresholds
 * the core uses to decide a skill-up: orange, yellow, green or gray. The colour
 * sets the reward; the first time a character ever makes or gathers a given
 * item adds a large bonus on top.
 *
 * Below the maximum level the reward is experience, a share of what the next
 * level costs. At the maximum level it is Spherite instead, when mod-spheregrid
 * is compiled in the same server (see mod-profession-xp.cmake).
 */

#ifndef MOD_PROFESSION_XP_H_
#define MOD_PROFESSION_XP_H_

#include "DataMap.h"
#include "DatabaseEnvFwd.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <array>
#include <chrono>
#include <string>
#include <unordered_set>

class Item;
class Player;
struct SkillLineAbilityEntry;

constexpr char PROFESSION_XP_MODULE[] = "mod-profession-xp";

// The texts live in `module_string` / `module_string_locale` (data/sql/world).
// Ids are never reused: an old row could still carry them.
enum ProfessionXPStrings : uint32
{
    PROFESSION_XP_STR_FIRST_CRAFT  = 1,   // {} the item, enchantment or research
    PROFESSION_XP_STR_FIRST_GATHER = 2,   // {} the item

    // 101 to 199: the texts of the profession window, sent to the client in
    // its language (data/lua/ProfessionXP/ProfessionXP_Client.lua names them).
    PROFESSION_XP_STR_WINDOW_FIRST = 101,
    PROFESSION_XP_STR_WINDOW_LAST  = 199
};

// THE ADDON CHANNEL between this module and its window script. The client
// whispers itself `ProfXP<TAB>request`, the module answers with addon
// whispers carrying the same prefix. The format is documented with
// ProfessionXPMgr::HandleAddonMessage.
constexpr char PROFESSION_XP_ADDON_PREFIX[] = "ProfXP";

// The colour of an action, in the order of its worth. The values index the
// reward tables of the configuration.
enum ProfessionXPColor : uint8
{
    PROFESSION_XP_GRAY   = 0,
    PROFESSION_XP_GREEN  = 1,
    PROFESSION_XP_YELLOW = 2,
    PROFESSION_XP_ORANGE = 3,
    PROFESSION_XP_COLORS = 4
};

// What a "first time" is keyed by. A craft that leaves no item behind — an
// enchantment applied to a piece of gear, a research — is keyed by its spell.
enum ProfessionXPKind : uint8
{
    PROFESSION_XP_KIND_ITEM  = 0,
    PROFESSION_XP_KIND_SPELL = 1
};

// The activity an action belongs to: each one is switched and scaled on its own.
enum ProfessionXPActivity : uint8
{
    PROFESSION_XP_CRAFTING  = 0,
    PROFESSION_XP_GATHERING = 1,   // mining, herbalism, skinning
    PROFESSION_XP_FISHING   = 2,
    PROFESSION_XP_ACTIVITIES = 3
};

struct ProfessionXPConfig
{
    bool enable = true;
    std::array<bool, PROFESSION_XP_ACTIVITIES> activity = { true, true, true };
    std::array<float, PROFESSION_XP_ACTIVITIES> rate = { 1.0f, 1.0f, 0.5f };
    bool rewardAtRankCap = false;

    std::array<float, PROFESSION_XP_COLORS> xpPercent = { 0.0f, 1.0f, 2.0f, 3.0f };
    float xpFirstTime = 10.0f;
    float xpMultiplier = 1.0f;
    float xpMaxPercent = 15.0f;

    bool firstTime = true;
    bool firstTimeOnGray = false;
    bool announce = true;

    bool spherite = true;
    std::array<uint32, PROFESSION_XP_COLORS> spheriteAmount = { 0, 2, 4, 6 };
    uint32 spheriteFirstTime = 25;
};

// What the module keeps on a character, hung on Player::CustomData: it lives and
// dies with the Player object, and a hook running on another map thread never
// touches the state of anybody else. Missing for a playerbot.
struct ProfessionXPPlayerData : public DataMap::Base
{
    std::unordered_set<uint64> done;                     // (kind << 32) | entry

    // The item a craft just made. OnPlayerCreateItem fires right before the
    // crafting hook, in the same call: the two are matched on the world tick.
    uint32 pendingItem = 0;
    std::chrono::milliseconds pendingTick{0};

    // A spell creating several items runs the crafting hook once per item: the
    // colour reward is given once per cast, the first-time bonus per item.
    uint32 lastCraftSpell = 0;
    std::chrono::milliseconds lastCraftTick{0};

    // The colour of the last mining, herbalism, skinning and fishing action:
    // the loot arrives after the hook, and a gray node grants no first-time
    // bonus. 0xFF while unknown.
    std::array<uint8, 4> lastGatherColor = { 0xFF, 0xFF, 0xFF, 0xFF };
};

class ProfessionXPMgr
{
public:
    static ProfessionXPMgr* instance();

    void LoadConfig();
    [[nodiscard]] ProfessionXPConfig const& Config() const { return _config; }

    void Load(Player* player);                                        // login
    void DeleteCharacter(CharacterDatabaseTransaction trans, uint32 guid);

    // The hooks, in the order the core calls them for a craft: the item is
    // created, then the skill is judged.
    void OnCreateItem(Player* player, Item* item);
    void OnCraft(Player* player, SkillLineAbilityEntry const* ability, uint32 current);
    void OnGather(Player* player, uint32 skill, uint32 current, uint32 gray, uint32 green, uint32 yellow);
    void OnFish(Player* player, int32 skill, int32 zoneSkill, int32 chance, int32 roll);
    void OnLoot(Player* player, Item* item, ObjectGuid lootGuid);

    // A request of the window script (the text after the prefix). False when
    // the message is not ours and must go on as a normal whisper.
    bool HandleAddonMessage(Player* player, std::string const& message);

private:
    ProfessionXPMgr() = default;

    [[nodiscard]] static ProfessionXPPlayerData* Data(Player* player);
    // True when the character never made or gathered this before; records it.
    bool MarkFirst(Player* player, ProfessionXPPlayerData* data, ProfessionXPKind kind, uint32 entry);
    // False when the skill sits at the cap of its current rank (a trainer is
    // needed) and the configuration refuses rewards there.
    [[nodiscard]] bool SkillHasRoom(Player* player, uint32 skill) const;
    // `percent` is a share of the next level, `spherite` the amount at the
    // maximum level. Both already include the activity rate.
    void Grant(Player* player, float percent, uint32 spherite);
    void Announce(Player* player, uint32 text, ProfessionXPKind kind, uint32 entry);

    // The window script's data: an addon whisper per line.
    static void SendAddon(Player* player, std::string const& body);
    void SendTexts(Player* player);
    void SendSettings(Player* player);
    void SendDone(Player* player, ProfessionXPPlayerData const* data);

    ProfessionXPConfig _config;
};

#define sProfessionXPMgr ProfessionXPMgr::instance()

#endif
