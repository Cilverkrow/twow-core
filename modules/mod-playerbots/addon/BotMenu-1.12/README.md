# BotMenu addon (1.12 / Turtle WoW)

Adds a **"Bots"** entry to the chat bubble menu (the button left of the chat
window, next to Say, Party, Emote, Language, ...). Hovering it opens the
categories **Combat, Formation, Role, Group, Quests, Vendor & NPC, Loot,
Professions, Inventory, Death, Info** (owner order, twow-repo#290; English labels since 1.4); a click writes
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
| Combat | follow, stay, attack, `tank attack`, pull, `attack rti`, `max dps`, flee, guard, free, wander, `co +passive`, `co -passive` |
| Formation | near, melee, line, circle, arrow, spear, queue, chaos, far, shield, ring, vanguard, rearguard, triangle, block, column, dragonslayer (raid), giantkiller (raid), `formation ?` (#389) |
| Role | "Tanks/Healers/Ranged/Melee only ...": the **next** command click gets `@tank ` / `@heal ` / `@ranged ` / `@melee ` in front, so only those bots react; "All (filter off)" clears it |
| Group | summon, leave, `give leader`, ready |
| Quests | quests, `accept *`, talk, `catchup quest `, `r `, `q ` |
| Vendor & NPC | talk, `talk 1`–`talk 3`, home, repair, `s` (sell grey), `s ` / `b `, `bb all`, `bank ?` |
| Loot | loot, `ll normal`, `ll gray`, `ll all`, `nc +loot`, `nc -loot`, `roll need/greed/pass/auto` |
| Professions | train, trainer, skill, `nc +gather`, `nc -gather` |
| Inventory | c, `c `, `e `, `ue `, `u `, `t ` (trade window open) |
| Death | release, revive, `self res` |
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

## Bot-Liste (1.3, twow-repo#419)

"Bots → Group → Bot list (all online)..." or `/botmenu list` opens a
window with every online character: name, class, level, guild, zone; filters
for faction, class, level range and zone (part of the name); "Invite" for
the selected row and "Convert to raid".

"Scan" collects the list with several `/who` queries, because one answer
holds at most 49 entries: one query per class first, full answers are split
by level (at the median of the answer), then by race. The list fills while
the scan runs and stays until the next scan.

- Pace: as fast as answers come back (GM accounts). A rank-0 account may send
  one `/who` per cooldown (30 s by default, server key
  `WhoList.RequestCooldownSeconds`); the addon notices a dropped query and
  steps to 6 s, then 31 s. `/botmenu pace <s>` sets it by hand,
  `/botmenu pace auto` goes back.
- Measured in `t/botmenu_addon_harness.lua` (simulated server, 180/360
  characters on levels 3-17): GM ≈ 3 s, cooldown 5 s ≈ 50 s, cooldown 30 s
  ≈ 4 min (9-11 queries).
- The stock who window is detached during a scan and given back afterwards;
  your own `/who` cancels a running scan.
- Real players are in the list too; `/who` has no bot marker.
- Class and race splitting know the enUS/enGB and deDE names; other client
  languages split by level only.

## Bot surnames (1.7, twow-repo#518)

Bots keep one-word character names: whisper, `/invite`, chat links and the
server's name lookup only work with those (a space breaks all of them in the
1.12 client). The surname is therefore display only:

- `BotSurnames.lua` maps the character name to its surname. It is generated,
  never edited by hand:

      tools/gen_botmenu_surnames.sh <twow-repo>/deploy/roster/names-518/addon-surnames-810.tsv <sha256> \
          > addon/BotMenu-1.12/BotSurnames.lua

- The unit tooltip (mouseover in the world, target/party/raid frames) shows
  "Name Surname"; if the name line carries a title, the surname is an extra
  grey line. Nothing is ever sent to the server.
- `/botmenu surnames off` hides it, `/botmenu surnames on` shows it again.
- A new roster table (new hash) means: regenerate, bump the version, release.

## Nameplates (1.8, twow-repo#518)

With nameplates on (V hostile, Shift+V friendly), a bot's plate shows
"Name Surname" from the same table. Players without an entry stay unchanged.

- The plates are unnamed children of `WorldFrame` whose first region is the
  nameplate border texture; their first FontString holds the name. New
  children are inspected once; the visible plates are checked every
  `BOTMENU_PLATE_INTERVAL` (0.25 s) on a small frame of its own.
- The client recycles plates and writes the new name itself; the next scan
  extends it again if it is a bot.
- `/botmenu plates off` restores the plain names; `/botmenu surnames off` turns
  off tooltip and plates.
- Other nameplate addons that replace the stock plate (pfUI, ShaguPlates and
  similar) read the name from the same FontString, so they usually show the
  surname too; addons that hide the stock text and keep their own copy of the
  name do not. Nothing else is touched.

## Name fix (1.8, twow-repo#518 probe)

Only relevant while the server shows bots as "Name Surname" (core probe
`Debug.NameQueryDisplayNames`, test realm). The 1.12 client sends that display
name back as the target of a right-click whisper, `/r`, `/invite`, `/friend`,
`/ignore` and mail - or only its last word from a chat link. `BotNameFix.lua`
wraps exactly those calls and turns a known display name back into the
character name; it never sends anything itself. `/botmenu namefix off` turns
it off (to measure what the server side covers alone).
