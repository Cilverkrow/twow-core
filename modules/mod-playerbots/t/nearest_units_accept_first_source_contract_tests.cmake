# twow-repo#541 (audit A03, cards 47 and 16): behind AiPlayerbot.Perf.NearestUnitsAcceptFirst (default 0)
# NearestUnitsValue::Calculate runs AcceptUnit before the LOS raycast, only with ignoreLos == false and
# only for subclasses that declare a pure AcceptUnit (AcceptUnitBeforeLos). "nearest friendly players"
# then visits only the world container (players never live in the grid container), and "nearest npcs"
# rejects players before the hostility check. Same lists, same order, fewer raycasts.
# Switch off: legacy statement, VisitAllObjects and the original AcceptUnit expression, unchanged.
# This is the "AcceptUnit with ignoreLos == false" purity test: exact opt-in allowlist, no subclasses of
# the opted-in classes, every opted-in AcceptUnit body pinned verbatim plus a forbidden-token scan.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_core path out_var)
  file(READ "${CORE_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A03: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A03: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A03: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A03: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A03: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#541 A03: ${description}: '${needle}' found ${n}x, expected ${expected}")
  endif()
endfunction()

function(require_equal actual_var expected_var description)
  set(found_text "${${actual_var}}")
  set(pinned_text "${${expected_var}}")
  if(NOT found_text STREQUAL pinned_text)
    message(FATAL_ERROR "#541 A03: ${description} changed, review its purity and update the pin.\n--- found:\n${found_text}\n--- expected:\n${pinned_text}")
  endif()
endfunction()

# Purity: an opted-in AcceptUnit writes nothing, draws no random numbers, touches no AI values or chat.
# Second layer only; the exact body pins below are the primary guard (callees are not scanned).
set(impure_tokens "urand" "rand_" "GetValue" "AI_VALUE" "context->" "Set(" "->Set" "Reset(" "Remove" "Add"
  "push_back" "erase(" "insert(" "static " "++" "sLog" "Tell" "IsWithinLOS")
function(require_pure body who)
  foreach(tok IN LISTS impure_tokens)
    forbid_text("${body}" "${tok}" "impure token in ${who} AcceptUnit")
  endforeach()
  # ai-> only for the own bot.
  string(REPLACE "ai->GetBot()" "" stripped "${body}")
  forbid_text("${stripped}" "ai->" "PlayerbotAI call other than GetBot() in ${who} AcceptUnit")
endfunction()

read_source("strategy/values/NearestUnitsValue.h" units_h)
read_source("strategy/values/NearestFriendlyPlayersValue.h" friendly_h)
read_source("strategy/values/NearestFriendlyPlayersValue.cpp" friendly_cpp)
read_source("strategy/values/NearestNpcsValue.h" npcs_h)
read_source("strategy/values/NearestNpcsValue.cpp" npcs_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_core("src/game/Maps/GridDefines.h" grid_defines)
read_core("src/game/Maps/CellImpl.h" cell_impl)
read_core("src/game/Maps/Cell.h" cell_h)

# 1. Switch: default off, documented.
require_text("${config_h}" "bool nearestUnitsAcceptFirst = false;" "member default off")
require_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool nearestUnitsAcceptFirst = false;" "after the #541 switches")
require_text("${config_cpp}" "nearestUnitsAcceptFirst = config.GetBoolDefault(\"AiPlayerbot.Perf.NearestUnitsAcceptFirst\", false);" "config default 0")
count_text("${config_cpp}" "nearestUnitsAcceptFirst =" 1 "single config load")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.NearestUnitsAcceptFirst = 0\n" "documented key, value 0")

# 2. Calculate: gate (switch, LOS required, opt-in), accept-first branch, legacy statement verbatim.
region("${units_h}" "virtual std::list<ObjectGuid> Calculate() override" "virtual void FindUnits(std::list<Unit*> &targets) = 0;" calc)
set(gate "bool const acceptFirst = sPlayerbotAIConfig.nearestUnitsAcceptFirst && !ignoreLos && AcceptUnitBeforeLos();")
set(fast "if (AcceptUnit(unit) && sServerFacade.IsWithinLOSInMap(bot, unit))")
set(legacy "else if ((ignoreLos || sServerFacade.IsWithinLOSInMap(bot, unit)) && AcceptUnit(unit))")
require_text("${calc}" "${gate}" "gate")
require_order("${calc}" "FindUnits(targets);" "${gate}" "gate after the search")
require_order("${calc}" "${gate}" "for(std::list<Unit *>::iterator i = targets.begin();" "gate once, before the loop")
require_order("${calc}" "if(ai->IsSafe(unit))" "if (acceptFirst)" "IsSafe stays first")
require_order("${calc}" "if (acceptFirst)" "${fast}" "accept-first branch")
require_order("${calc}" "${fast}" "${legacy}" "legacy statement in the else branch")
require_text("${calc}" "${legacy}\n                        results.push_back(unit->GetObjectGuid());" "legacy push unchanged")
count_text("${calc}" "IsWithinLOSInMap" 2 "exactly two LOS calls")
count_text("${calc}" "AcceptUnit(unit)" 2 "exactly two AcceptUnit calls")
count_text("${calc}" "acceptFirst" 2 "gate read once and tested once")
forbid_text("${calc}" "AcceptUnit(unit) && (ignoreLos" "global swap of the legacy statement")

# 3. Base opt-out and purity contract comment.
require_text("${units_h}" "virtual bool AcceptUnitBeforeLos() const { return false; }" "base default false")
require_text("${units_h}" "purity contract" "purity contract comment")

# 4. Reviewed opt-ins only, each inside the right class.
region("${units_h}" "class NearestStealthedUnitsValue" "class NearestStealthedSingleUnitValue" stealth_cls)
require_text("${stealth_cls}" "bool AcceptUnitBeforeLos() const override { return true; }" "stealthed opt-in")
count_text("${units_h}" "AcceptUnitBeforeLos() const override" 1 "one opt-in in NearestUnitsValue.h")
region("${npcs_h}" "class NearestNpcsValue" "class NearestVehiclesValue" npcs_cls)
require_text("${npcs_cls}" "bool AcceptUnitBeforeLos() const override { return true; }" "npcs opt-in")
count_text("${npcs_h}" "AcceptUnitBeforeLos() const override" 1 "vehicles not opted in")
require_text("${friendly_h}" "bool AcceptUnitBeforeLos() const override { return true; }" "friendly players opt-in")
count_text("${friendly_h}" "AcceptUnitBeforeLos() const override" 1 "one opt-in in NearestFriendlyPlayersValue.h")

# Opt-in allowlist, direct-subclass allowlist, and no subclass of an opted-in class (it would inherit
# AcceptUnitBeforeLos() == true with a possibly impure AcceptUnit override).
file(GLOB_RECURSE pb_sources LIST_DIRECTORIES false "${PB_SOURCE_DIR}/*.h" "${PB_SOURCE_DIR}/*.cpp")
set(opt_in_files "")
set(subclass_files "")
foreach(f IN LISTS pb_sources)
  file(READ "${f}" t)
  file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${f}")
  string(FIND "${t}" "AcceptUnitBeforeLos() const override" o)
  if(NOT o EQUAL -1)
    list(APPEND opt_in_files "${rel}")
  endif()
  string(FIND "${t}" "public NearestUnitsValue" s)
  if(NOT s EQUAL -1)
    list(APPEND subclass_files "${rel}")
  endif()
  foreach(opted NearestNpcsValue NearestFriendlyPlayersValue NearestStealthedUnitsValue)
    string(FIND "${t}" "public ${opted}" d)
    if(NOT d EQUAL -1)
      message(FATAL_ERROR "#541 A03: ${rel} derives from the opted-in ${opted}; it would inherit AcceptUnitBeforeLos() == true, review its AcceptUnit purity first")
    endif()
  endforeach()
endforeach()
list(SORT opt_in_files)
list(SORT subclass_files)
set(expected_opt_in "strategy/values/NearestFriendlyPlayersValue.h;strategy/values/NearestNpcsValue.h;strategy/values/NearestUnitsValue.h")
if(NOT opt_in_files STREQUAL expected_opt_in)
  message(FATAL_ERROR "#541 A03: unreviewed AcceptUnitBeforeLos opt-in: ${opt_in_files}")
endif()
set(expected_subclasses "strategy/values/NearestCorpsesValue.h;strategy/values/NearestFriendlyPlayersValue.h;strategy/values/NearestNonBotPlayersValue.h;strategy/values/NearestNpcsValue.h;strategy/values/NearestUnitsValue.h;strategy/values/PossibleRpgTargetsValue.h;strategy/values/PossibleTargetsValue.h")
if(NOT subclass_files STREQUAL expected_subclasses)
  message(FATAL_ERROR "#541 A03: NearestUnitsValue subclass set changed, review its AcceptUnit purity: ${subclass_files}")
endif()

# 5. Purity of every opted-in AcceptUnit: exact body pinned, plus the token scan.
region("${friendly_cpp}" "bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)" "\n}" friendly_accept)
set(friendly_expected "bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)\n{\n    ObjectGuid guid = unit->GetObjectGuid();\n    return guid.IsPlayer() && guid != ai->GetBot()->GetObjectGuid();")
require_equal(friendly_accept friendly_expected "NearestFriendlyPlayersValue::AcceptUnit (players only, the world-only visit relies on it)")
require_pure("${friendly_accept}" "NearestFriendlyPlayersValue")

region("${npcs_cpp}" "bool NearestNpcsValue::AcceptUnit(Unit* unit)" "\n}" npcs_accept)
set(npcs_expected "bool NearestNpcsValue::AcceptUnit(Unit* unit)\n{\n    if (sPlayerbotAIConfig.nearestUnitsAcceptFirst && dynamic_cast<Player*>(unit))\n        return false;\n\n    return !sServerFacade.IsHostileTo(unit, bot) && !dynamic_cast<Player*>(unit);")
require_equal(npcs_accept npcs_expected "NearestNpcsValue::AcceptUnit (gated player-first test + original expression)")
require_pure("${npcs_accept}" "NearestNpcsValue")

region("${stealth_cls}" "bool AcceptUnit(Unit* unit) override" "\n        }" stealth_accept)
set(stealth_expected "bool AcceptUnit(Unit* unit) override
        {
            if (!unit || !unit->IsAlive() || !sServerFacade.IsHostileTo(unit, bot))
                return false;

            return unit->HasAuraType(SPELL_AURA_MOD_STEALTH) || unit->HasAuraType(SPELL_AURA_MOD_INVISIBILITY);

            /*uint32 dispelMask = 0;
            dispelMask |= GetDispellMask(DispelType(DISPEL_STEALTH));
            dispelMask |= GetDispellMask(DispelType(DISPEL_INVISIBILITY));

            return unit->HasMechanicMaskOrDispelMaskAura(dispelMask, 0, bot);*/")
require_equal(stealth_accept stealth_expected "NearestStealthedUnitsValue::AcceptUnit")
require_pure("${stealth_accept}" "NearestStealthedUnitsValue")

# 6. Friendly players: world container only behind the switch; other searches untouched.
region("${friendly_cpp}" "void NearestFriendlyPlayersValue::FindUnits(std::list<Unit*> &targets)" "\n}" friendly_find)
require_order("${friendly_find}" "if (sPlayerbotAIConfig.nearestUnitsAcceptFirst)" "Cell::VisitWorldObjects(bot, searcher, range);" "gated world-only visit")
require_text("${friendly_find}" "if (sPlayerbotAIConfig.nearestUnitsAcceptFirst)\n        Cell::VisitWorldObjects(bot, searcher, range);\n    else\n        Cell::VisitAllObjects(bot, searcher, range);" "legacy visit in else")
require_text("${friendly_find}" "AnyFriendlyUnitInObjectRangeCheck u_check(bot, range);" "unchanged check")
count_text("${friendly_find}" "Cell::Visit" 2 "two visits only")
region("${npcs_cpp}" "void NearestNpcsValue::FindUnits(std::list<Unit*> &targets)" "\n}" npcs_find)
forbid_text("${npcs_find}" "VisitWorldObjects" "npcs need the grid container")
require_text("${npcs_find}" "Cell::VisitAllObjects(bot, searcher, range);" "npcs search unchanged")
region("${stealth_cls}" "void FindUnits(std::list<Unit*>& targets) override" "\n        }" stealth_find)
forbid_text("${stealth_find}" "VisitWorldObjects" "stealthed units need the grid container")

# 7. Core facts the world-only visit relies on.
region("${grid_defines}" "typedef TYPELIST_4(GameObject" "AllGridObjectTypes;" grid_types)
forbid_text("${grid_types}" "Player" "Player in the grid container")
require_text("${grid_defines}" "TYPELIST_4(Player, Creature/*pets*/" "Player in the world container")
region("${cell_impl}" "inline void Cell::VisitAllObjects(const WorldObject *center_obj" "\n}" visit_all)
require_order("${visit_all}" "cell.Visit(p, gnotifier" "cell.Visit(p, wnotifier" "grid pass before world pass")
require_text("${visit_all}" "TypeContainerVisitor<T, WorldTypeMapContainer > wnotifier(visitor);" "world pass visitor")
region("${cell_impl}" "inline void Cell::VisitWorldObjects(const WorldObject *center_obj" "\n}" visit_world)
require_text("${visit_world}" "TypeContainerVisitor<T, WorldTypeMapContainer > gnotifier(visitor);" "world container visitor")
forbid_text("${visit_world}" "GridTypeMapContainer" "grid container in the world-only visit")
require_text("${visit_world}" "cell.SetNoCreate();" "same no-load handling")
require_text("${cell_h}" "static void VisitWorldObjects(const WorldObject *obj, T &visitor, float radius, bool dont_load = true);" "same no-load default")
require_text("${cell_h}" "static void VisitAllObjects(const WorldObject *obj, T &visitor, float radius, bool dont_load = true);" "same no-load default")

message(STATUS "nearest_units_accept_first source contract passed")
