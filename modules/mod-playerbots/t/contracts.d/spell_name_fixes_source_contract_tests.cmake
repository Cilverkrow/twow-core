# twow-repo#602 ROT-01 spell-name audit (OB-00 28.10.2026, bug fix, no switch): spell actions whose name never resolved through the "spell id"
# value for a bot that has learned the spell, checked against the image's spell data (sql/base spell_template + skill_line_ability):
# Barkskin (the action said "barskin"), Turtle's Hand of Freedom / Protection / Sacrifice (the actions said "blessing of ..."), and the
# warlock stones (each rank is its own spell "<stone> (Minor)" .. "(Major)", the bare name only one of them). This contract pins the pure
# names, the actions setting them (the action names stay, so creators / strategy nodes / triggers are untouched) and the stone rank pick.
# The names are tested in t/spell_name_policy_tests.cpp.

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
    message(FATAL_ERROR "#602 spell names: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("SpellNamePolicy.h" policy)
read_source("strategy/druid/DruidActions.h" druid)
read_source("strategy/paladin/PaladinActions.h" paladin)
read_source("strategy/warlock/WarlockActions.h" warlock)

# 1. Pure names (lower case, as SpellIdValue matches them).
require_text("${policy}" "constexpr char const* kBarkskin = \"barkskin\";" "Barkskin")
require_text("${policy}" "constexpr char const* kHandOfFreedom = \"hand of freedom\";" "Hand of Freedom")
require_text("${policy}" "constexpr char const* kHandOfProtection = \"hand of protection\";" "Hand of Protection")
require_text("${policy}" "constexpr char const* kHandOfSacrifice = \"hand of sacrifice\";" "Hand of Sacrifice")
require_text("${policy}" "return { stone + \" (major)\", stone + \" (greater)\", stone, stone + \" (lesser)\", stone + \" (minor)\" };" "stone ranks, highest first")

# 2. The actions keep their names and set the spell that resolves.
require_text("${druid}" "CastBarskinAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, \"barskin\") { SetSpellName(ai::spell_names::kBarkskin); }" "Barkskin")
require_text("${paladin}" "CastBlessingOfFreedomAction(PlayerbotAI* ai) : CastBuffSpellAction(ai, \"blessing of freedom\") { SetSpellName(ai::spell_names::kHandOfFreedom); }" "Hand of Freedom (self)")
require_text("${paladin}" "CastBlessingOfFreedomOnPartyAction(PlayerbotAI* ai) : CastSpellAction(ai, \"blessing of freedom\") { SetSpellName(ai::spell_names::kHandOfFreedom); }" "Hand of Freedom (party)")
require_text("${paladin}" "CastBlessingOfSacrificeAction(PlayerbotAI* ai) : BuffOnPartyAction(ai, \"blessing of sacrifice\") { SetSpellName(ai::spell_names::kHandOfSacrifice); }" "Hand of Sacrifice")
require_text("${paladin}" "CastBlessingOfProtectionOnPartyAction(PlayerbotAI* ai) : CastProtectSpellAction(ai, \"blessing of protection\") { SetSpellName(ai::spell_names::kHandOfProtection); }" "Hand of Protection")
string(FIND "${paladin}" "BUFF_ACTION(CastBlessingOfFreedomAction, \"blessing of freedom\");" old_freedom)
if(NOT old_freedom EQUAL -1)
  message(FATAL_ERROR "#602 spell names: the old BUFF_ACTION for blessing of freedom is back")
endif()

# 3. The stones: one base class picks the highest known rank before isUseful / isPossible; the four stones use it.
require_text("${warlock}" "#include \"playerbot/SpellNamePolicy.h\"" "the policy include")
require_text("${warlock}" "        bool isUseful() override { PickKnownRank(); return CastSpellAction::isUseful(); }\n        bool isPossible() override { PickKnownRank(); return CastSpellAction::isPossible(); }" "the rank pick before both checks")
require_text("${warlock}" "            for (std::string const& name : ai::spell_names::StoneRankNames(stone))\n            {\n                if (AI_VALUE2(uint32, \"spell id\", name))\n                {\n                    SetSpellName(name);\n                    return;\n                }\n            }\n            SetSpellName(stone);" "the first known rank, else the bare name")
foreach(s healthstone soulstone firestone spellstone)
  require_text("${warlock}" ": CastCreateStoneAction(ai, \"create ${s}\") {}" "create ${s} on the rank pick")
endforeach()

message(STATUS "#602 spell names source contract passed")
