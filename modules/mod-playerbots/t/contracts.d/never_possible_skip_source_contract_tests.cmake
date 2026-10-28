# twow-repo#602 ROT-01 failure census v3 (OB-00 28.10.2026): AiPlayerbot.Rotation.NeverPossibleSkip, code default 1 (0 = rollback), makes a spell
# action whose spell the bot does not know IMPOSSIBLE at once and pushes no reach prerequisite (hunt2: 22.8 % of the engine time of all
# action checks went to such checks). IMPOSSIBLE stays IMPOSSIBLE (the engine still pushes the node's alternatives; the never-possible
# audit found an isUseful early-out would lose them). This contract pins the dist key, the config member and read (unknown modes = the
# default 1), the parsed mode in [ConfigEcho] (owner rule #615), the pure policy, the CastSpellAction wiring (the "spell id" value, not
# the spellId member, so a learned spell is seen; isPossible after the mount branch; getPrerequisites first), the opt-out of the six
# runtime-name actions and "mount", and the [NeverPossible] minute line. The rules are tested in t/never_possible_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#602 NeverPossibleSkip: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#602 NeverPossibleSkip: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("NeverPossiblePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/actions/GenericSpellActions.h" spell_h)
read_source("strategy/actions/GenericSpellActions.cpp" spell_cpp)
read_source("strategy/actions/PullActions.h" pull_h)
read_source("strategy/paladin/PaladinActions.h" paladin_h)
read_source("strategy/shaman/ShamanActions.h" shaman_h)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.NeverPossibleSkip = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 rotationNeverPossibleSkip = 1;" "config member, default 1")
require_text("${config_cpp}" "rotationNeverPossibleSkip = config.GetIntDefault(\"AiPlayerbot.Rotation.NeverPossibleSkip\", 1);   // twow-repo#602 failure census v3\n    if (rotationNeverPossibleSkip > 2)\n        rotationNeverPossibleSkip = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"Rotation.MeleeSkipAttacking\", Value(rotationMeleeSkipAttacking)},\n            {\"Rotation.NeverPossibleSkip\", Value(rotationNeverPossibleSkip)},\n" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only) and its rule.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 NeverPossibleSkip: NeverPossiblePolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "return applies && !keepsUnknown && knownSpellId == 0;" "skip only an unknown spell without a runtime name")

# 3. CastSpellAction: the opt-out hook, the helper on the "spell id" value, isPossible after the mount branch, getPrerequisites first.
require_text("${spell_h}" "        virtual bool KeepsUnknownSpell() const { return false; }" "the opt-out hook, default off")
require_text("${spell_cpp}" "#include \"playerbot/NeverPossiblePolicy.h\"" "the policy include")
require_text("${spell_cpp}" "    if (!ai::never_possible::Applies(sPlayerbotAIConfig.rotationNeverPossibleSkip, bot->GetGUIDLow()) || KeepsUnknownSpell() || spellName == \"mount\")\n        return false;" "switch first, opt-outs and mount keep the checks")
require_text("${spell_cpp}" "    return ai::never_possible::Skip(true, false, AI_VALUE2(uint32, \"spell id\", spellName));" "the spell id value decides (a learned spell is seen)")
string(FIND "${spell_cpp}" "bool CastSpellAction::isPossible()" ip)
string(SUBSTRING "${spell_cpp}" ${ip} 2500 isp)
require_order("${isp}" "if (spellName == \"mount\")" "if (NeverPossible())" "the mount branch before the early-out")
require_text("${isp}" "    if (NeverPossible())\n    {\n        ai::never_possible::possible.fetch_add(1, std::memory_order_relaxed);\n        return RotEval(false, ai::rot_cast::EvalNoSpellId);\n    }\n\n    Unit* spellTarget = GetTarget();" "isPossible false at once, before the target and reach checks")
string(FIND "${spell_cpp}" "NextAction** CastSpellAction::getPrerequisites()" gp)
string(SUBSTRING "${spell_cpp}" ${gp} 900 pre)
require_text("${pre}" "    if (NeverPossible())\n    {\n        ai::never_possible::prereq.fetch_add(1, std::memory_order_relaxed);\n        return Action::getPrerequisites();\n    }\n\n    // Set the reach action as the cast spell prerequisite when needed" "no reach prerequisite for an unknown spell")

# 4. The six runtime-name actions opt out (the audit: shoot / throw rename in isPossible, pull picks its spell, blessings pick the
#    blessing, the shaman imbue picks the imbue, vehicle spells use "vehicle spell id").
require_text("${spell_h}" "CastShootAction(PlayerbotAI* ai) : CastSpellAction(ai, \"shoot\"), rangedWeapon(nullptr), weaponDelay(0), needsAmmo(false) {}\n        bool KeepsUnknownSpell() const override { return true; }" "shoot")
require_text("${spell_h}" "            SetSpellName(spell, \"vehicle spell id\", !GetSpellID());\n        }\n        bool KeepsUnknownSpell() const override { return true; }" "vehicle spells")
require_text("${pull_h}" "PullAction(PlayerbotAI* ai, std::string name = \"pull action\");\n        bool KeepsUnknownSpell() const override { return true; }" "pull")
require_text("${paladin_h}" "CastBlessingAction(PlayerbotAI* ai, std::string name, bool greater) : CastSpellAction(ai, name), greater(greater) {}\n        bool KeepsUnknownSpell() const override { return true; }" "blessing (self)")
require_text("${paladin_h}" "CastBlessingOnPartyAction(PlayerbotAI* ai, std::string name, bool greater) : CastSpellAction(ai, name), greater(greater) {}\n        bool KeepsUnknownSpell() const override { return true; }" "blessing on party")
require_text("${shaman_h}" "CastShamanWeaponImbueAction(PlayerbotAI* ai) : CastEnchantItemAction(ai, \"shaman weapon imbue\") {}\n        bool KeepsUnknownSpell() const override { return true; }" "shaman imbue")

# 5. The [NeverPossible] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/NeverPossiblePolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationNeverPossibleSkip || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[NeverPossible] mode=%u possible=%llu prereq=%llu\", sPlayerbotAIConfig.rotationNeverPossibleSkip,\n        (unsigned long long)ai::never_possible::possible.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::never_possible::prereq.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportNeverPossible();" "the minute report is called")

message(STATUS "#602 NeverPossibleSkip source contract passed")
