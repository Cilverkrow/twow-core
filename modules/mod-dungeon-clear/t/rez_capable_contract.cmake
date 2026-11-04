cmake_policy(VERSION 3.16)
# CLI-GRP 04.11.2026 (g1/CAUSE-DC-REVIVE-PFAD-20261104 F1+F3, OB-00 04.11.; Refs twow-repo#629): DungeonClear.RezCapableOnly,
# default 0. The post-combat rez elected its rezzer by CLASS and counted the druid, whose out-of-combat "revive" does not
# exist in 1.12; StayDeadInRun held the corpse for that druid too. 1 = the election (F1) and the release veto (F3) count
# only a member who can rez now (DcRezRecovery::CanRezNow; the pure rule DcRezDecision::CanRezNow, gtest
# TestPostCombatRez DcRezCapableTest). This pins the wiring as source text: both sites behind the switch through the one
# helper, the switch-0 path as the class rule, the mana POOL (not current mana: a drained rezzer drinks first), the
# map-object + dungeon gate on the cross-bot spell read (OB-10 #861), CanRezNow in CanRecover, the registry row and the conf key at 0. Every check
# is run again on mutated copies (negative probes): each mutation must fail the contract, so the pins cannot rot into
# vacuous matches.

set(PB_DIR "${DC_SOURCE_DIR}/../mod-playerbots")

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# The checks record failures in a global property instead of stopping, so the probes below can expect them.
function(rco_fail description)
  set_property(GLOBAL APPEND PROPERTY RCO_FAILS "${description}")
endfunction()

function(rco_require text_var needle description)
  string(FIND "${${text_var}}" "${needle}" offset)
  if(offset EQUAL -1)
    rco_fail("missing ${description}")
  endif()
endfunction()

function(rco_forbid text_var needle description)
  string(FIND "${${text_var}}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    rco_fail("${description}")
  endif()
endfunction()

function(rco_order text_var first second description)
  string(FIND "${${text_var}}" "${first}" a)
  string(FIND "${${text_var}}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    rco_fail("order: ${description}")
  endif()
endfunction()

function(rco_count text_var needle expected description)
  set(count 0)
  set(rest "${${text_var}}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${offset} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    rco_fail("${description}: found ${count}x, expected ${expected}")
  endif()
endfunction()

# The text between `first` and `second` (empty, and a failure, when either is missing).
function(rco_between text_var first second out_var)
  set(part "")
  string(FIND "${${text_var}}" "${first}" a)
  if(a EQUAL -1)
    rco_fail("missing section start ${first}")
  else()
    string(SUBSTRING "${${text_var}}" ${a} -1 tail)
    string(FIND "${tail}" "${second}" b)
    if(b EQUAL -1)
      rco_fail("missing section end ${second}")
    else()
      string(SUBSTRING "${tail}" 0 ${b} part)
    endif()
  endif()
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

set(SWITCH_READ "DcSettings::GetBool(bot, \"RezCapableOnly\")")
set(SELECT_GATE "        if (${SWITCH_READ})
            for (std::size_t i = 0; i < members.size(); ++i)
                if (!members[i].isDead && members[i].canRezClass)
                    members[i].canRezClass = DcRezRecovery::CanRezNow(players[i], true, bot->FindMap());")
set(VETO_TEST "member->IsAlive() &&
                CanRezNow(member, capableOnly, map))")
set(DUNGEON_GATE "        Map* map = member->FindMap();
        if (!map || map != callerMap || !map->IsDungeon())
            return DcRezDecision::CanRezNow(c, false);")

# Every pin, on the texts in the variables registry conf recovery recovery_h decision follower module.
function(rco_check_all)
  # Key: registry row (server-only, default 0), documented with 0 and the CAUSE it fixes.
  rco_require(registry "    { \"RezCapableOnly\",           DcType::Bool,   0,   0,     1,  false }," "registry RezCapableOnly default 0")
  rco_require(conf "\nDungeonClear.RezCapableOnly = 0\n" "conf RezCapableOnly = 0")
  rco_between(conf "#    DungeonClear.RezCapableOnly\n" "\nDungeonClear.RezCapableOnly = " conf_block)
  rco_require(conf_block "g1/CAUSE-DC-REVIVE-PFAD-20261104" "conf comment cites the CAUSE")
  rco_require(conf_block "OB-00 04.11." "conf comment cites the order")
  rco_require(conf_block "Default: 0" "conf comment default")

  # F1, selection: past the no-deaths return and the PostCombatRez gate, before the kernel, through the helper.
  rco_between(recovery "    DcRezRecovery::Plan EvaluateImpl(Player* bot, bool mutate)" "        plan.verdict = DcRezDecision::Decide(in, members);" eval)
  rco_require(eval "${SELECT_GATE}" "selection behind the switch through CanRezNow")
  rco_order(eval "            return plan;  // None / NoDeaths" "${SELECT_GATE}" "the spell reads only while someone is dead")
  rco_order(eval "        if (!DcRezRecovery::Enabled(bot))" "${SELECT_GATE}" "PostCombatRez gate first")
  # Switch 0: the snapshot's class rule, untouched.
  rco_between(recovery "    void BuildSnapshot(" "    bool AnyMemberEngaged(" snapshot)
  rco_require(snapshot "            m.canRezClass = IsRezClass(member);" "snapshot keeps the class rule")
  rco_forbid(snapshot "CanRezNow" "the snapshot (every tick, no corpse needed) reads the spells")

  # F3, veto: LivingRezzerOnMap behind the switch through the same helper; the module's veto still asks it.
  rco_between(recovery "    bool LivingRezzerOnMap(Player* bot)\n    {" "    bool IsElectedRezzer(Player* bot)" veto)
  rco_require(veto "        bool const capableOnly = ${SWITCH_READ};" "veto reads the switch")
  rco_require(veto "${VETO_TEST}" "veto behind the switch through CanRezNow")
  rco_require(module "                                     DcRezRecovery::LivingRezzerOnMap(bot), deadSeconds," "StayDeadInRunVeto through LivingRezzerOnMap")
  rco_require(recovery_h "    bool CanRezNow(Player* member, bool capableOnly, Map const* callerMap);" "helper declaration")

  # The one helper: switch 0 / non rez class returns before any spell read; the rung's own spell resolution, the
  # member's own spellbook, cooldown and cost; the verdict is the pure rule.
  rco_between(recovery "    bool CanRezNow(Player* member, bool capableOnly, Map const* callerMap)\n    {" "    bool LivingRezzerOnMap(Player* bot)" helper)
  rco_order(helper "        if (!c.isRezClass || !capableOnly)
            return DcRezDecision::CanRezNow(c, capableOnly);" "GetValue<uint32>(DcKey::Stock::SpellId, name)->Get()" "switch 0 before any spell read")
  rco_require(helper "char const* name = RezActionNameFor(member->getClass());" "the rung's rez name")
  rco_require(helper "member->HasSpell(spellId)" "the member's own spellbook")
  rco_require(helper "member->IsSpellReady(spellId)" "cooldown")
  rco_require(helper "Spell::CalculatePowerCost(info, member)" "cost of the resolved rank")
  rco_require(helper "member->GetMaxPower(Powers(info->powerType))" "the mana pool")
  rco_forbid(helper "member->GetPower(" "current mana in the capability (a drained rezzer must stay electable)")
  rco_require(helper "        return DcRezDecision::CanRezNow(c, capableOnly);\n    }" "the pure verdict")
  # The cross-bot "spell id" read only on the CALLER's map object (OB-10 #861: the callers filter by map ID, another
  # instance copy runs on another pool thread - the twow-repo#563 X5 class) and only on a dungeon map (sequential
  # object updates), after the switch-0 return.
  rco_order(helper "            return DcRezDecision::CanRezNow(c, capableOnly);" "${DUNGEON_GATE}" "switch 0 before the dungeon gate")
  rco_order(helper "${DUNGEON_GATE}" "GetValue<uint32>(DcKey::Stock::SpellId, name)->Get()" "map-object + dungeon gate before the spell read")
  rco_count(helper "GetValue<" 1 "value reads in the helper (only the gated spell id)")
  rco_count(recovery "${SWITCH_READ}" 3 "switch reads (election, CanRecover, veto)")
  # Every caller hands its own map object (the election and CanRecover the evaluating bot's, the veto its `map`);
  # no two-argument call is left.
  foreach(old IN ITEMS "CanRezNow(players[i], true)" "CanRezNow(member, capableOnly)" "CanRezNow(bot, capableOnly)")
    rco_forbid(recovery "${old}" "a CanRezNow call without the caller's map: ${old}")
  endforeach()

  # CanRecover (`dc on` / resume with corpses): the same helper, group and solo path.
  rco_between(recovery "    bool CanRecover(Player* bot)\n    {" "    char const* RezActionNameFor(uint8 playerClass)" recover)
  rco_require(recover "        bool const capableOnly = ${SWITCH_READ};" "CanRecover reads the switch")
  rco_require(recover "        Map const* const callerMap = bot->FindMap();" "CanRecover passes its own map object")
  rco_require(recover "            return bot->IsAlive() && CanRezNow(bot, capableOnly, callerMap);" "CanRecover solo through CanRezNow")
  rco_require(recover "            if (member->IsAlive() && CanRezNow(member, capableOnly, callerMap))" "CanRecover group through CanRezNow")
  rco_forbid(recover "IsRezClass(" "CanRecover by class")

  # The pure rule (gtest DcRezCapableTest).
  rco_require(decision "        if (!capableOnly || !c.isBot)
            return true;
        return c.spellId != 0 && c.spellReady && c.maxPower >= c.powerCost;" "pure rule")

  # One class->rez table, used by the rung; the druid entry stays "revive" (Rebirth = F2, out of scope).
  rco_require(follower "    char const* rezAction = DcRezRecovery::RezActionNameFor(bot->getClass());" "rung uses the shared table")
  rco_forbid(follower "char const* RezActionNameFor(uint8 playerClass)" "a second class->rez table in DcFollowerActions")
  rco_require(recovery "            case CLASS_DRUID:   return \"revive\";" "druid entry unchanged")

  # DC-only: no mod-playerbots file reads the key.
  foreach(src IN LISTS pb_sources)
    file(READ "${src}" text)
    string(FIND "${text}" "RezCapableOnly" hit)
    if(NOT hit EQUAL -1)
      rco_fail("${src} reads RezCapableOnly - the switch is DC-only")
    endif()
  endforeach()
endfunction()

read_dc("src/Ai/Dungeon/DungeonClear/Settings/DcSettingsRegistry.h" registry)
read_dc("conf/mod_dungeon_clear.conf.dist" conf)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcRezRecovery.cpp" recovery)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcRezRecovery.h" recovery_h)
read_dc("src/Ai/Dungeon/DungeonClear/Util/DcRezDecision.h" decision)
read_dc("src/Ai/Dungeon/DungeonClear/Action/DcFollowerActions.cpp" follower)
read_dc("src/DungeonClearModule.cpp" module)
file(GLOB_RECURSE pb_sources "${PB_DIR}/src/*.cpp" "${PB_DIR}/src/*.h")
list(LENGTH pb_sources pb_count)
if(pb_count EQUAL 0)
  message(FATAL_ERROR "rez capable: no mod-playerbots sources under ${PB_DIR}/src")
endif()

set_property(GLOBAL PROPERTY RCO_FAILS "")
rco_check_all()
get_property(fails GLOBAL PROPERTY RCO_FAILS)
if(fails)
  string(REPLACE ";" "\n  " fails "${fails}")
  message(FATAL_ERROR "rez capable:\n  ${fails}")
endif()

# Negative probes: one mutation of the real text each; the contract must fail on every one.
set(probe_count 0)
function(rco_probe name var from to)
  set(saved "${${var}}")
  string(FIND "${saved}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "rez capable: probe '${name}' does not apply (text not found) - update the probe")
  endif()
  string(REPLACE "${from}" "${to}" mutated "${saved}")
  set(${var} "${mutated}")
  set_property(GLOBAL PROPERTY RCO_FAILS "")
  rco_check_all()
  get_property(fails GLOBAL PROPERTY RCO_FAILS)
  if(NOT fails)
    message(FATAL_ERROR "rez capable: negative probe '${name}' passed - the contract does not pin it")
  endif()
  set_property(GLOBAL APPEND PROPERTY RCO_PROBES "${name}")
endfunction()

rco_probe("registry default 1" registry "{ \"RezCapableOnly\",           DcType::Bool,   0," "{ \"RezCapableOnly\",           DcType::Bool,   1,")
rco_probe("conf key 1" conf "\nDungeonClear.RezCapableOnly = 0\n" "\nDungeonClear.RezCapableOnly = 1\n")
rco_probe("conf without the CAUSE" conf "DungeonClear.RezCapableOnly\n#        BUG FIX (CLI-GRP 04.11.2026, g1/CAUSE-DC-REVIVE-PFAD-20261104" "DungeonClear.RezCapableOnly\n#        BUG FIX (CLI-GRP 04.11.2026")
rco_probe("selection without the switch" recovery "        if (${SWITCH_READ})\n            for" "        if (true)\n            for")
rco_probe("selection by class" recovery "members[i].canRezClass = DcRezRecovery::CanRezNow(players[i], true, bot->FindMap());" "members[i].canRezClass = IsRezClass(players[i]);")
rco_probe("selection with the member's own map" recovery "CanRezNow(players[i], true, bot->FindMap())" "CanRezNow(players[i], true, players[i]->FindMap())")
rco_probe("snapshot reads the spells" recovery "            m.canRezClass = IsRezClass(member);" "            m.canRezClass = DcRezRecovery::CanRezNow(member, true);")
rco_probe("veto by class" recovery "${VETO_TEST}" "member->IsAlive() &&\n                IsRezClass(member))")
rco_probe("veto without the switch" recovery "        bool const capableOnly = ${SWITCH_READ};\n        for" "        bool const capableOnly = true;\n        for")
rco_probe("module veto bypasses the helper" module "DcRezRecovery::LivingRezzerOnMap(bot), deadSeconds," "true, deadSeconds,")
rco_probe("switch 0 reads the spells" recovery "        if (!c.isRezClass || !capableOnly)" "        if (!c.isRezClass)")
rco_probe("no own-spellbook check" recovery "if (info && member->HasSpell(spellId))" "if (info)")
rco_probe("pure rule ignores mana" decision " && c.maxPower >= c.powerCost;" ";")
rco_probe("pure rule ignores the cooldown" decision "c.spellId != 0 && c.spellReady &&" "c.spellId != 0 &&")
rco_probe("pure rule ignores the spell" decision "return c.spellId != 0 && c.spellReady" "return c.spellReady")
rco_probe("current mana" recovery "member->GetMaxPower(Powers(info->powerType))" "member->GetPower(Powers(info->powerType))")
rco_probe("no dungeon gate" recovery "        if (!map || map != callerMap || !map->IsDungeon())\n            return DcRezDecision::CanRezNow(c, false);" "        if (!map || map != callerMap)\n            return DcRezDecision::CanRezNow(c, false);")
rco_probe("no map-object gate" recovery "        if (!map || map != callerMap || !map->IsDungeon())" "        if (!map || !map->IsDungeon())")
rco_probe("map-object gate after the read" recovery "        if (!map || map != callerMap || !map->IsDungeon())\n            return DcRezDecision::CanRezNow(c, false);\n" "        if (!map || !map->IsDungeon())\n            return DcRezDecision::CanRezNow(c, false);\n        (void)(map != callerMap);\n")
rco_probe("CanRecover with the member's map" recovery "        Map const* const callerMap = bot->FindMap();" "        Map const* const callerMap = nullptr;")
rco_probe("veto two-argument call" recovery "CanRezNow(member, capableOnly, map))" "CanRezNow(member, capableOnly))")
rco_probe("CanRecover group by class" recovery "            if (member->IsAlive() && CanRezNow(member, capableOnly, callerMap))" "            if (member->IsAlive() && IsRezClass(member))")
rco_probe("CanRecover solo by class" recovery "            return bot->IsAlive() && CanRezNow(bot, capableOnly, callerMap);" "            return bot->IsAlive() && IsRezClass(bot);")
rco_probe("second rez table" follower "bool DungeonClearRezPartyAction::Execute(Event& /*event*/)" "char const* RezActionNameFor(uint8 playerClass);\nbool DungeonClearRezPartyAction::Execute(Event& /*event*/)")

get_property(probes GLOBAL PROPERTY RCO_PROBES)
list(LENGTH probes probe_n)
message(STATUS "rez capable contract passed (${probe_n} negative probes failed as expected)")
