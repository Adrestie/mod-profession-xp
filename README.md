# mod-profession-xp

An AzerothCore module (WoW 3.3.5a) that gives players experience for crafting and
gathering.

* Each craft, node, skinned corpse and fish caught pays by the **colour** the
  game gives it: **orange** pays well, **yellow** pays normally, **green** pays
  a little and **gray** pays nothing.
* The **first time** a character crafts or gathers a given item, it gets a large
  bonus and a chat line names the item.
* At the **maximum level**, the reward is **Spherite** instead of experience,
  when the server is built with
  [mod-spheregrid](https://github.com/Adrestie/mod-spheregrid). Without it the
  module still builds and runs, and gives nothing at the maximum level.

The profession window shows what the selected recipe pays, and grays out, with
the reason in the client's language, what the player will not get. It works in
the game's own window and in [ForeverUI](https://github.com/Adrestie/mod-forever-ui)'s.

No client patch and no new identifiers: core hooks, one characters table, texts
in `module_string`, and one client script shipped by AIO for the window.

## What counts

| Activity | Counted | Colour from |
|---|---|---|
| Crafting | every recipe of a primary or secondary profession (cooking and first aid included), smelting, enchantments applied to gear, research spells | the recipe's thresholds, read the same way the core reads them for skill-ups |
| Gathering | mining, herbalism, skinning (including creatures skinned with mining or herbalism) | the node or corpse thresholds the core passes to its gathering hook |
| Fishing | every fish caught (one that gets away pays nothing) | the zone's base fishing level: orange below +25, yellow below +50, green below +95, gray from +95 (where the core stops letting fish escape) |

Disenchanting, prospecting, milling, lockpicking and engineering "skinning" do
not count. Playerbots earn nothing and are not tracked.

The core asks about a node once per player per respawn, and about a corpse once,
so reopening a node pays nothing more.

### The rank cap

When a skill is at the cap of its rank (for example 150/150 before the Expert
trainer), it cannot learn anything more, and a yellow recipe would stay yellow
forever. By default, colour rewards stop there until the player trains. The last
rank (450) is not affected. `ProfessionXP.RewardAtRankCap = 1` turns this off.
First-time bonuses are paid in both cases.

### First time

The bonus is per character and per item entry. A craft that leaves no item
behind (an enchantment on gear, a research) counts per spell. Each item counts
separately: a copper vein that gives ore and a stone is two first times. A gray
action records its first time without paying for it
(`ProfessionXP.FirstTime.Gray`).

The list lives in `mod_profession_xp_character_done` (characters database). It
is deleted with the character, and kept when the module is updated.

## Rewards

Below the maximum level, rewards are **percentages of the experience the next
level costs**, so an action is worth the same at every level:

| | Green | Yellow | Orange | First time |
|---|---|---|---|---|
| Experience (% of next level) | 1 | 2 | 3 | +10 |
| Spherite (maximum level) | 2 | 4 | 6 | +25 |

Each activity has a rate that scales its colour reward (crafting 1.0, gathering
1.0, fishing 0.5). A global multiplier applies to experience, and one reward is
capped at 15% of a level by default. A craft that is both orange and a first
time is one reward. Spherite is then boosted by the account's prismatic Nexuses,
like every other Sphere Grid gain. For scale, a quest is worth 25 Spherite by
default.

Everything is set in `conf/mod-profession-xp.conf.dist`, and `.reload config`
applies changes.

## The profession window

Under the selected recipe's description, above its reagents:

* **Profession reward**, then **Crafting: N XP** in the recipe's colour (or
  **N Spherite** at the maximum level, prisms included);
* **First time: +N XP**, in green while the bonus is still to be earned;
* **At most N per craft** when the capped total is lower than the sum.

A line the player will not get is gray, with the reason under it: skill at the
cap of its rank, already made, first-time bonuses off.

Some lines are hidden rather than grayed:

* a recipe too easy to pay (gray) shows no crafting line, and no first-time
  line unless `ProfessionXP.FirstTime.Gray = 1`;
* when nothing at all would be paid, the whole **Profession reward** block is
  hidden: a gray recipe, a skill at its rank cap on a recipe already made,
  crafting rewards off, experience gain turned off for the character, nothing
  at the maximum level (built without mod-spheregrid, or Spherite off);
* nothing is shown for a profession linked by another player.

The texts come from `module_string` (ids 101 to 118) in the client's language:
English, French, German, Spanish (both) and Russian, English for the others.

**How it works.** `data/lua/ProfessionXP/ProfessionXP_Client.lua` is sent to
the client by AIO, like the Sphere Grid's window. It talks to the C++ module
directly over an addon whisper (prefix `ProfXP`). The module sends the
settings, the texts and the list of what the character already made, and
pushes each new first time as it happens. The client works out each line with
the same rules as the server. In ForeverUI the lines are added to its recipe
card; ForeverUI itself is not modified.

Without ALE and AIO on the server, rewards work the same and the window shows
nothing.

## Requirements

| | |
|---|---|
| [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) | 3.3.5a, built with the module in `modules/` |
| [ALE](https://github.com/azerothcore/mod-ale) (the AzerothCore Lua Engine, formerly mod-eluna) | for the profession window only: runs `data/lua/ProfessionXP` |
| [AIO](https://github.com/Rochet2/AIO) | for the profession window only: server AND client, the window is sent over it |
| [mod-spheregrid](https://github.com/Adrestie/mod-spheregrid) | optional: the Spherite reward at maximum level |

Without ALE and AIO, rewards work the same and the window shows nothing.

## Installation

With the WoW-mods installer: point it at this folder. By hand:

1. Copy the module to `modules/mod-profession-xp` in the AzerothCore sources,
   then re-run CMake and rebuild. CMake prints
   `mod-profession-xp: mod-spheregrid found` when the Spherite reward is
   compiled in. That needs `modules/mod-spheregrid` with both modules linked
   statically (the default).
2. Apply `data/sql/characters` to the characters database and `data/sql/world`
   to the world database, or let the core updater do it. Both files can be run
   again safely.
3. Copy `conf/mod-profession-xp.conf.dist` to the server's `configs/modules`
   folder as `mod-profession-xp.conf`.
4. For the profession window: copy `data/lua/ProfessionXP` into the ALE
   scripts folder (`lua_scripts/ProfessionXP`). It needs ALE and AIO, as the
   Sphere Grid's window does.

## In-game tests

Use a test character below the maximum level unless stated otherwise. Run
`.reload config` after each configuration change.

1. **Startup:** the worldserver starts without errors from the module. With
   mod-spheregrid, CMake printed `mod-spheregrid found`. Without it, CMake
   printed `built without mod-spheregrid` and the worldserver log says there is
   no reward at the maximum level.
2. **Orange craft, first time:** learn a recipe that is orange (for example
   Rough Sharpening Stone at Blacksmithing 1) and craft it once. You get one
   experience gain of about 13% of the level (3 + 10), and the chat line
   `First time crafting "Rough Sharpening Stone"`.
3. **Same craft again:** about 3% of the level, and no chat line.
4. **Yellow and green:** craft recipes shown in yellow and green in the trade
   window. You get about 2% and 1% of the level.
5. **Gray:** craft a gray recipe. You get no experience. A gray recipe you never
   made gives no bonus either, and does not pay later.
6. **Enchanting:** enchant a piece of gear. You get the colour reward and, the
   first time, the chat line with the enchantment's name. Enchanting onto a
   vellum names the scroll. Disenchanting gives nothing.
7. **Mining / herbalism:** mine a vein or pick a herb. You get the colour reward
   when the loot window opens. Looting a new item gives the bonus and its chat
   line, once per item (ore, stone, gem each count). Reopening the same node
   before it respawns gives nothing.
8. **Skinning:** skin a corpse. You get the colour reward, and the bonus for a
   new leather.
9. **Fishing:** each fish caught gives half the colour reward. A fish that gets
   away gives nothing. The first catch of a new fish gives the bonus.
   `ProfessionXP.Fishing.Enable = 0` stops all of it, including fishing
   first-time bonuses.
10. **Rank cap:** with a skill at 75/75 (not trained further), crafting a yellow
    recipe gives no experience. A new item still gives the first-time bonus.
    With `ProfessionXP.RewardAtRankCap = 1`, the yellow reward comes back.
11. **Not a profession:** a mage conjuring water, or opening a locked chest,
    gives nothing.
12. **Persistence:** log out, log back in, craft an item already made before.
    There is no new first-time bonus. Rows are in
    `mod_profession_xp_character_done` for the character's guid.
13. **Deletion:** delete the test character. Its rows are gone from
    `mod_profession_xp_character_done`.
14. **Maximum level, with mod-spheregrid:** on a level 80 character, an orange
    craft gives `You gain 6 Spherite.` (more with prisms). An orange first time
    gives 31 plus the chat line. No experience is shown.
15. **Maximum level, without mod-spheregrid** (or `ProfessionXP.Spherite.Enable
    = 0`): nothing is given. The first-time chat line still appears, and the
    item is still recorded.
16. **Cap and multiplier:** set `ProfessionXP.Xp.Multiplier = 10`. An orange
    first time is capped at 15% of the level (`ProfessionXP.Xp.MaxPercent`).
17. **Switches:** `ProfessionXP.Enable = 0` gives nothing and records nothing.
    `ProfessionXP.Crafting.Enable = 0` / `Gathering.Enable = 0` turn each
    activity off on its own.
18. **Locales:** on a French client, the chat line reads
    `Première fabrication de « … »`, with the item's French name.
19. **Experience locked:** a character with experience gain turned off (the
    Warsong Gulch trainer option) gets nothing below the maximum level.

### The profession window

Test each step in the game's own window, then again with ForeverUI installed.

20. **Lines:** open a profession and select an orange recipe never made. Under
    the description: `Profession reward`, `Crafting: N XP` in orange, and
    `First time: +N XP` in green. N matches what crafting it actually gives
    (3% and 10% of the level by default).
21. **Colours:** a yellow recipe shows yellow, a green one green. A gray recipe
    shows no reward block at all, made or not.
22. **After a first craft:** craft the orange recipe. Without closing the
    window, its first-time line turns gray with `Already made: …`.
23. **Rank cap:** at 75/75 before training, a yellow recipe never made shows
    its crafting line grayed with the rank cap reason, and the first-time line
    in green. Once made, the block is hidden.
24. **Cap:** with `ProfessionXP.Xp.Multiplier = 5` and `.reload config`, reopen
    the window: `At most N XP per craft.` appears under an orange first time.
25. **Switches:** `ProfessionXP.Crafting.Enable = 0`: no block.
    `ProfessionXP.FirstTime.Enable = 0`: the first-time line grayed with its
    reason under a paid crafting line.
26. **Experience locked:** with experience gain turned off, no block.
27. **Maximum level:** on a level 80 character with mod-spheregrid, amounts
    read `N Spherite`, boosted by the account's prisms. Without mod-spheregrid,
    no block.
28. **Language:** on a French client, every line and reason is in French.
29. **Layout:** reagents stay below the block and the stock window can still
    scroll down to the last reagent. In ForeverUI, the block sits between the
    description and the reagents of the recipe card.
30. **Linked profession:** open another player's profession link. No reward
    block is shown.
31. **Without AIO:** remove `lua_scripts/ProfessionXP`. The window is the same
    as before the module, and rewards are still given.

## Licence

GPL-2.0-or-later, the licence of AzerothCore it is compiled into. See
[LICENSE](LICENSE).
