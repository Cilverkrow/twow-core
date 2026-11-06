# twow-repo#333 Berufe M2 shadow counter (OB-00 06.11.2026, cheat-off analysis §d; log only, no behaviour change):
# with AiPlayerbot.CheatItem.Trace on (default 0), every bot sums how often the item cheat stood in for an item and writes
# one "[CheatItem]" line per 10-minute window. The counters sit inside the existing cheat branches and change nothing there.
# This contract pins the key, the window rules and their tests (t/cheat_item_policy_tests.cpp), the line and every site.

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
    message(FATAL_ERROR "#333 M2 CheatItem: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2 CheatItem: order ${description}: '${first}' must come before '${second}'")
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
    message(FATAL_ERROR "#333 M2 CheatItem: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/CheatItemPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" use_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseItemAction.h" use_h)
read_source("${PB_SOURCE_DIR}/strategy/actions/GenericSpellActions.cpp" spell_cpp)
read_source("${PB_SOURCE_DIR}/strategy/rogue/RogueActions.h" rogue_h)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_MODULE_DIR}/t/cheat_item_policy_tests.cpp" tests)
read_source("${PB_MODULE_DIR}/tests.cmake" tests_cmake)

# 0. The key: default 0, documented with the line.
require_text("${config_cpp}" "cheatItemTrace = config.GetBoolDefault(\"AiPlayerbot.CheatItem.Trace\", false);" "(0) key read, default off")
require_text("${dist}" "\nAiPlayerbot.CheatItem.Trace = 0\n" "(0) dist default 0")
require_text("${dist}" "#   [CheatItem] bot= level= class= seconds= use= use_had= cast_item= cast_item_had= food= drink= poison= ammo_refill= ammo_init= refresh= cheat_cd_set= self_blocked=" "(0) dist: the line")

# 1. The pure rules and their tests.
require_text("${policy}" "constexpr std::uint64_t kFlushSeconds = 600;" "(1) ten-minute window")
require_text("${policy}" "    return windowStart != 0 && now >= windowStart + kFlushSeconds;" "(1) window rule")
require_text("${policy}" "    return maxStack > before ? maxStack - before : 0;" "(1) refill amount")
require_text("${policy}" "        n[i] += amount > room ? room : amount;" "(1) saturating sums")
require_text("${tests}" "\"use=0 use_had=0 cast_item=0 cast_item_had=0 food=0 drink=0 poison=0 ammo_refill=0 ammo_init=0 refresh=0 cheat_cd_set=0 self_blocked=0\"" "(1) tests: fixed columns")
require_text("${tests}" "ShouldFlush(1600, 1000)" "(1) tests: window")
require_text("${tests_cmake}" "add_test(NAME cheat_item_policy" "(1) tests registered")

# 2. The counter and the line in PlayerbotAI: nothing without the key; one line per window, only with something counted.
require_text("${ai_h}" "#include \"playerbot/CheatItemPolicy.h\"" "(2) policy include")
require_text("${ai_cpp}" "    if (!sPlayerbotAIConfig.cheatItemTrace || !amount)\n        return;\n\n    cheatItemCounts.Add(site, amount);" "(2) counting behind the key")
require_text("${ai_cpp}" "    if (!sPlayerbotAIConfig.cheatItemTrace || !bot)\n        return;" "(2) line behind the key")
require_text("${ai_cpp}" "    if (cheatItemCounts.Any())\n        sLog.outBasic(\"[CheatItem] bot=%u level=%u class=%u seconds=%u %s\"" "(2) line only with counts")
require_order("${ai_cpp}" "                PlayerbotFactory(bot, bot->GetLevel(), 0).InitAmmo();\n#endif" "    FlushCheatItem();\n" "(2) flush after the cheat block")
require_order("${ai_cpp}" "    FlushCheatItem();\n" "    // Alt level sync" "(2) flush in UpdateAI")
count_text("${ai_cpp}" "\"[CheatItem] bot=" 1 "(2) one CheatItem line")

# 3. The sites, each inside its existing cheat branch.
# 3a. Ammo top-up and InitAmmo (PlayerbotAI::UpdateAI, item cheat, hunter/rogue/warrior).
require_text("${ai_cpp}" "                    uint32 const ammoBefore = item->GetCount();   // twow-repo#333 M2 shadow counter\n                    item->SetCount(item->GetProto()->GetMaxStackSize());\n                    NoteCheatItem(ai::cheat_item::Site::AmmoRefill, ai::cheat_item::RefillAmount(ammoBefore, item->GetCount()));" "(3a) refill units")
require_text("${ai_cpp}" "                if (sPlayerbotAIConfig.cheatItemTrace)\n                {\n                    if (uint32 const ammoAfter = bot->GetUInt32Value(PLAYER_AMMO_ID))\n                        NoteCheatItem(ai::cheat_item::Site::AmmoInit, bot->GetItemCount(ammoAfter));" "(3a) init units")
count_text("${ai_cpp}" "NoteCheatItem(ai::cheat_item::Site::Ammo" 2 "(3a) two ammo sites")
# 3b. UseItemAction: a cast without the item under the cheat.
require_text("${use_cpp}" "    if (sPlayerbotAIConfig.cheatItemTrace && successCasts > 0 && !itemUsed && ai->HasCheat(BotCheatMask::item))\n        ai->NoteCheatItem(ai::cheat_item::UseSite(bot->GetItemCount(proto->ItemId)));" "(3b) use site")
count_text("${use_cpp}" "->NoteCheatItem(" 3 "(3b) use site + the two cooldown counters in UseItemAction.cpp")
# 3c. CastItemTargetAction: inside the cheat cooldown branch, once per use.
require_text("${spell_cpp}" "            if (sPlayerbotAIConfig.cheatItemTrace && !count && !item)\n                ai->NoteCheatItem(ai::cheat_item::CastItemSite(bot->GetItemCount(itemId)));" "(3c) cast item site")
require_order("${spell_cpp}" "        if (ai->HasCheat(BotCheatMask::item))\n        {\n            if (!HasSpellCooldown(itemId))" "ai->NoteCheatItem(ai::cheat_item::CastItemSite" "(3c) inside the cheat branch")
count_text("${spell_cpp}" "->NoteCheatItem(" 3 "(3c) cast item site + the two cooldown counters in GenericSpellActions.cpp")
# 3d. Cheat food and drink: the cast and the paired aura.
# After the X1 RestGate fail block (rest_gate_source_contract keeps the cast and that block adjacent): a resting failed cast
# returns before the counter.
require_text("${use_h}" "                castFailed = false;\n                if (cast)   // twow-repo#333 M2 shadow counter (CheatItem.Trace, log only): the cheat drink\n                    ai->NoteCheatItem(ai::cheat_item::Site::Drink);\n                SetDuration(drinkDuration);" "(3d) drink cast")
require_text("${use_h}" "                castFailed = false;\n                if (cast)   // twow-repo#333 M2 shadow counter (CheatItem.Trace, log only): the cheat food\n                    ai->NoteCheatItem(ai::cheat_item::Site::Food);\n                SetDuration(eatDuration);" "(3d) food cast")
require_text("${use_h}" "                        ai->AddAura(bot, 24005);\n                        ai->NoteCheatItem(ai::cheat_item::Site::Food);" "(3d) paired food aura")
require_text("${use_h}" "                        ai->AddAura(bot, 24355);\n                        ai->NoteCheatItem(ai::cheat_item::Site::Drink);" "(3d) paired drink aura")
count_text("${use_h}" "->NoteCheatItem(" 4 "(3d) four sites in UseItemAction.h")
# 3e. Rogue poison under the cheat, once per application.
require_text("${rogue_h}" "                                        if (!count)   // twow-repo#333 M2 shadow counter (CheatItem.Trace, log only)\n                                            ai->NoteCheatItem(ai::cheat_item::Site::Poison);" "(3e) poison site")
count_text("${rogue_h}" "->NoteCheatItem(" 1 "(3e) one site in RogueActions.h")
# 3f. Refresh: after its item-cheat return.
require_order("${factory}" "    if (!ai->HasCheat(BotCheatMask::item))\n        return;\n\n    ai->NoteCheatItem(ai::cheat_item::Site::Refresh);" "    InitAmmo();\n    InitFood();" "(3f) refresh site after the cheat check")
count_text("${factory}" "->NoteCheatItem(" 1 "(3f) one site in PlayerbotFactory.cpp")

# 4. Start proof: the M2-0 kit proves its trace keys in [ConfigEcho] (config_echo_source_contract checks the order).
require_text("${config_cpp}" "            {\"CheatItem.Trace\", Value(cheatItemTrace)},\n" "(4) echo entry CheatItem.Trace")
require_text("${config_cpp}" "            {\"EquipDiagnostics.Trace\", Value(equipDiagnosticsTrace)},\n" "(4) echo entry EquipDiagnostics.Trace")
require_text("${config_cpp}" "            {\"ProfessionUse.Trace\", Value(professionUseTrace)},\n" "(4) echo entry ProfessionUse.Trace")
