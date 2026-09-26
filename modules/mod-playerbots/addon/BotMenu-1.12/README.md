# BotMenu addon (1.12 / Turtle WoW)

Adds a **"Bots"** entry to the chat bubble menu (the button left of the chat
window, next to Say, Party, Emote, Language, ...). Hovering it opens the
categories **Kampf, Formation, Beute, Berufe, Quests, Gruppe**; a click writes
the playerbot command into the chat line. Press Enter to send it.

The **active chat channel decides who gets the command**, exactly as when you
type it yourself:

| Channel in the chat line | Reaches |
|---|---|
| `/w <Bot>` | that one bot |
| `/p` | the bots in your party |
| `/raid` | the bots in your raid |
| `/s` | your own bots within 25 yards |

The addon only writes chat text. It sends nothing on its own and adds no
protocol; the server checks every command as if it was typed
(twow-repo#354, #292: roster bots follow only their master).

## Install

Copy the folder `BotMenu-1.12` into `Interface\AddOns\` of the client and
rename it to `BotMenu`. Log in; the entry is on by default.

## Switch

`/botmenu off` removes the entry, `/botmenu on` brings it back (saved per
character). `/botmenu` shows the state.

## Menu

| Category | Entries (command) |
|---|---|
| Kampf | follow, stay, attack, flee, guard, free, `co +passive`, `co -passive` |
| Formation | near, melee, line, circle, arrow, spear, queue, chaos, far, shield, `formation ?` |
| Beute | loot, `ll normal`, `ll gray`, `ll all`, `nc +loot`, `nc -loot` |
| Berufe | train, trainer, skill, `nc +gather`, `nc -gather` |
| Quests | quests, `accept *`, talk, `catchup quest ` (then shift-click the quest link) |
| Gruppe | summon, leave |

The list comes from the command inventory in twow-repo#290. `train` needs
twow-core#145, `catchup quest` #147, `formation spear` #148.

## Lua 5.0

Same rules as `mod-dungeon-clear/addon/DungeonClear-1.12`: no `#`, no `%`,
handlers read `this`/`event`, no `SetSize`. The menus reuse Blizzard's
`UIMenuTemplate` (1.12.1 `FrameXML/UIMenu.lua`), like `EmoteMenu`.

## Tests

- `botmenu_addon_contract` (ctest): every menu command is a registered chat
  trigger, every formation is known to the server, 10 formations, 1.12 rules.
- `botmenu_addon_harness` (ctest, where `lua5.1`/`lua` exists): runs the addon
  against stand-ins of the 1.12 menu API; every click writes its command, the
  switch works, old submenus close.
