# mod-profession-xp

An AzerothCore module (WoW 3.3.5a) that gives players experience for crafting, gathering and fishing.

## Key features

- Each craft, node, skinned corpse and fish pays by the colour the game gives it: orange, yellow, green, grey.
- A large bonus the first time a character crafts or gathers a given item.
- Rewards in percent of the next level, worth the same at every level.
- At the maximum level, Spherite instead of experience, with mod-spheregrid.
- The profession window shows what a recipe pays, in the game's window and in ForeverUI's.
- Playerbots earn nothing and are not tracked.

## Dependencies

| Dependency | For |
|---|---|
| [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) (WotLK 3.3.5a) | the server |
| [mod-ale](https://github.com/azerothcore/mod-ale) | the profession window only |
| [AIO](https://github.com/Rochet2/AIO) | the profession window only: `AIO_Server` in `lua_scripts`, and the `AIO_Client` addon on every client |
| [mod-spheregrid](https://github.com/Adrestie/mod-spheregrid) | optional: Spherite at the maximum level, when both modules are built together |
| [mod-forever-ui](https://github.com/Adrestie/mod-forever-ui) | optional: the window's lines in its recipe card |

## How to install

1. Download `installer.exe` from the [WoW-mods-installer releases](https://github.com/Adrestie/WoW-mods-installer/releases).
2. Stop the worldserver.
3. Run `installer.exe`, give it this folder and the folders it asks for, click **Check**, then **Install**.
4. Rebuild the worldserver. On its next start, the core applies the module's SQL.

## How to update

Same steps with the new version's folder, choosing **Update** instead of **Install**. The firsts each character already made, and your `mod-profession-xp.conf` settings, are kept. Rebuild the worldserver.

## How to uninstall

Same steps, choosing **Remove**. Everything goes, database included. Rebuild the worldserver.

## Configuration

`mod-profession-xp.conf` sets:

- the module on or off, and crafting, gathering and fishing each on or off, with a rate each;
- the reward of each colour and of a first time, the global multiplier and the cap of one reward;
- first-time rewards for grey items, and their chat line;
- the Spherite rewards at the maximum level.
