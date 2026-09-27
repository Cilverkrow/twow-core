# BotMenu addon (1.12 / Turtle WoW)

Adds a **"Bots"** entry to the chat bubble menu (the button left of the chat
window, next to Say, Party, Emote, Language, ...). Hovering it opens the
categories **Kampf, Formation, Rolle, Gruppe, Quests, Händler & NPC, Beute,
Berufe, Inventar, Tod, Info** (owner order, twow-repo#290); a click writes
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
| Kampf | follow, stay, attack, `tank attack`, pull, `attack rti`, `max dps`, flee, guard, free, wander, `co +passive`, `co -passive` |
| Formation | near, melee, line, circle, arrow, spear, queue, chaos, far, shield, ring, vanguard, rearguard, triangle, block, column, `formation ?` (#389) |
| Rolle | "Nur Tanks/Heiler/Fernkampf/Nahkampf ...": the **next** command click gets `@tank ` / `@heal ` / `@ranged ` / `@melee ` in front, so only those bots react; "Alle" clears it |
| Gruppe | summon, leave, `give leader`, ready |
| Quests | quests, `accept *`, talk, `catchup quest `, `r `, `q ` |
| Händler & NPC | talk, `talk 1`–`talk 3`, home, repair, `s` (sell grey), `s ` / `b `, `bb all`, `bank ?` |
| Beute | loot, `ll normal`, `ll gray`, `ll all`, `nc +loot`, `nc -loot`, `roll need/greed/pass/auto` |
| Berufe | train, trainer, skill, `nc +gather`, `nc -gather` |
| Inventar | c, `c `, `e `, `ue `, `u `, `t ` (trade window open) |
| Tod | release, revive, `self res` |
| Info | stats, where, talents, spells, reputation |

Entries ending in a space wait for a shift-clicked link (item or quest).
Deliberately **not** in the menu: destroy, drop, sendmail, ah, cast, guild
rank changes, reset (owner decision pending in twow-repo#290).

The list comes from the command inventory in twow-repo#290. `train` came
with twow-core#145, `catchup quest` with #147, `formation spear` with #148.

## Lua 5.0

Same rules as `mod-dungeon-clear/addon/DungeonClear-1.12`: no `#`, no `%`,
handlers read `this`/`event`, no `SetSize`. The menus reuse Blizzard's
`UIMenuTemplate` (1.12.1 `FrameXML/UIMenu.lua`), like `EmoteMenu`.

## Tests

- `botmenu_addon_contract` (ctest): every menu command is a registered chat
  trigger, every formation is known to the server, 16 formations, every role
  prefix is a chat filter, 11 categories in the owner order, 1.12 rules.
- `botmenu_addon_harness` (ctest, where `lua5.1`/`lua` exists): runs the addon
  against stand-ins of the 1.12 menu API; every click writes its command, the
  switch works, old submenus close, a role prefix applies to exactly one
  command.
