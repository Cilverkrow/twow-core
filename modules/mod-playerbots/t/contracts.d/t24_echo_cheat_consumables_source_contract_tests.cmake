# T24 echo pair (OB-00 10.11.2026, F7 (a); CLI-Berufe T24 PREREG, item cheat off in both arms; log only, no behaviour
# change): [ConfigEcho] proves AiPlayerbot.BotCheats and AiPlayerbot.RndBotCheats as the parsed bit mask (decimal) and the
# seven AiPlayerbot.RosterConsumables keys (twow-repo#485) as their parsed members. config_echo_source_contract checks the
# generic shape, the order and that each member is parsed from its key; this contract pins the nine entries and the bit
# values a kit decodes from the mask (item = 32), so a renumbered BotCheatMask cannot change what the echo means unseen.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "T24 echo: missing ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "T24 echo: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)

# 1. The nine echo entries, each exactly once.
set(entries
  "{\"BotCheats\", Value(botCheatMask)},"
  "{\"RndBotCheats\", Value(rndBotCheatMask)},"
  "{\"RosterConsumables.Buy\", Value(rosterConsumablesBuy)},"
  "{\"RosterConsumables.HealingPotionPct\", Value(rosterConsumablesHealingPotionPct)},"
  "{\"RosterConsumables.KeepStacks\", Value(rosterConsumablesKeepStacks)},"
  "{\"RosterConsumables.ManaPotionPct\", Value(rosterConsumablesManaPotionPct)},"
  "{\"RosterConsumables.Trace\", Value(rosterConsumablesTrace)},"
  "{\"RosterConsumables.TraceCooldownSeconds\", Value(rosterConsumablesTraceCooldownSeconds)},"
  "{\"RosterConsumables.UseReal\", Value(rosterConsumablesUseReal)},")
foreach(entry IN LISTS entries)
  count_text("${config_cpp}" "            ${entry}\n" 1 "(1) echo entry")
endforeach()

# 2. The members are the parsed masks / values (the echo prints what the process understood, not the raw string).
require_text("${config_cpp}" "    botCheatMask = uint32(CheatAction::GetCheatMask(config.GetStringDefault(\"AiPlayerbot.BotCheats\"," "(2) BotCheats parsed into the mask")
require_text("${config_cpp}" "    rndBotCheatMask = uint32(CheatAction::GetCheatMask(config.GetStringDefault(\"AiPlayerbot.RndBotCheats\"," "(2) RndBotCheats parsed into the mask")
require_text("${config_h}" "    uint32 botCheatMask = 0;\n    uint32 rndBotCheatMask = 0;" "(2) masks are uint32 (Value prints them in decimal)")

# 3. The bit values the dist documents and a kit decodes (item = 32).
require_text("${config_h}" "    none = 0,\n    taxi = 1 << 0,\n    gold = 1 << 1,\n    health = 1 << 2,\n    mana = 1 << 3,\n    power = 1 << 4,\n    item = 1 << 5,\n    cooldown = 1 << 6,\n    repair = 1 << 7,\n    movespeed = 1 << 8,\n    attackspeed = 1 << 9,\n    breath = 1 << 10,\n    glyph = 1 << 11," "(3) BotCheatMask bits")
require_text("${config_h}" "BotCheatMaskName = { \"taxi\", \"gold\", \"health\", \"mana\", \"power\", \"item\", \"cooldown\", \"repair\", \"movespeed\", \"attackspeed\", \"breath\", \"glyph\", \"quest\", \"maxMask\" };" "(3) names in bit order")
require_text("${dist}" "# repair 128, movespeed 256, attackspeed 512, breath 1024, glyph 2048, quest 4096. Item cheat on = bit 32 set;\n# \"repair,breath\" = 1152, \"repair,breath,item\" = 1184, nothing = 0.\n" "(3) dist documents the decimal mask")
