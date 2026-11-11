cmake_policy(VERSION 3.16)
# twow-repo#563 HARM-RACE guard pair (OB-00 order 11.11.2026, inventory v2 Y:\tw\evidence\ws-00\563p4\harm-race-inventory-v2-20261111.md; OB-10 review
# of the design: G3 dropped, G9 in, G11 a slip). Same class as #697 / #707 / #710.
#
# What IsSafe really guarantees (corrects the texts of #707 / #710): PlayerbotAI::IsSafe compares map id AND instance id, and every continent
# region Map has its own instance id, so in steady state a player in the neighbouring region already fails IsSafe. The exposure left is a
# foreign read/write with NO guard or a GetMapId()-only filter. A region switch runs on the world thread after continents.wait() and far
# teleports after the map updates, so there is no transition window for bot threads. Each site below tests SameMapObject(bot, other) BEFORE
# the first read of the other player.
#
# Table (CHANGED = guarded here, LEFT = named and deliberately not changed):
#   CHANGED G1  ItemUsageValue.cpp        master quest slots / quest map / inventory via IsNeededForQuest(master); getQuestStatusMap()[..] is an insert
#   CHANGED G2  EmoteAction.cpp           write of another bot's "talk target": cross Map object always through QueueContextWriteTo
#   CHANGED G4  TradeStatusAction.cpp     trade partner (GetTradeData, money, items): return when the partner is on another Map object (NO cancel: TradeCancel deletes the partner's state)
#               ItemForSpellValue.cpp     trade data of the bot's / master's trade partner
#   CHANGED G5  WorldBuffTravelActions.cpp member->HasAura (container read) and PlayerbotAI::AddAura(member) (write)
#   CHANGED G6  GrindTargetValue.cpp      master filter was GetMapId() only, GetBotAI(master) follows
#   CHANGED G7  CcTargetValue.cpp         ai->IsTank(member)
#   CHANGED G8  PartyMemberValue::Check   the common candidate filter of the heal/buff member values
#   CHANGED G9  PullTriggers.cpp, ReachTargetActions.h, ShamanActions.h, AttackerCountValues.cpp   member loops, GetMapId() only
#   CHANGED G10 InviteToGroupAction.cpp   IsSafe(groupMaster, player): first argument was the leader (foreign, and null when offline) -> the asker
#   CHANGED G11 PlayerbotAI.cpp           `!IsInWorld() && GetMapId() != ...` was an &&/|| slip
#   LEFT   G3  BattleGroundJoinAction::gatherArenaTeam: inside #ifndef MANGOSBOT_ZERO, not compiled in the Turtle (ZERO) build
#   LEFT       WorldBuffTravelStrategy.h:168 (alive / map id / taxi flag / distance of a member: scalars)
#   LEFT       InviteToGroupAction.cpp:345 520 524 (level / map id of the invitee: scalars)
#   LEFT       world-hook managers (tick barrier, twow-repo#708), mod-dungeon-clear (instance maps = one Map object), mod-bot-brain (own bot only)
# Negative probes: the checkers must reject the old text of every site (the substitution must really change the text).

set(TAG "cross-region harm guards (#563 pair)")

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(region out text start_marker end_marker)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# the guard must be present and stand before the read (both must exist: the checker must not lose the site)
function(guard_before errors_var text guard read description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${guard}" guard_at)
  string(FIND "${text}" "${read}" read_at)
  if(guard_at EQUAL -1)
    list(APPEND errors "${description}: missing guard '${guard}'")
  elseif(read_at EQUAL -1)
    list(APPEND errors "${description}: expected read '${read}' not found (the checker lost the site)")
  elseif(NOT guard_at LESS read_at)
    list(APPEND errors "${description}: the guard '${guard}' must come before the read '${read}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

function(forbid errors_var text needle description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    list(APPEND errors "${description}: must not contain '${needle}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

function(require errors_var text needle description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    list(APPEND errors "${description}: missing '${needle}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the checkers (each returns the list of violations, empty = ok) ----
function(g1_violations out fn_text whole_text)
  set(errors "")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, ai->GetMaster())" "IsNeededForQuest(ai->GetMaster()" "G1 ItemUsageValue master quest read")
  forbid(errors "${whole_text}" "getQuestStatusMap()[" "G1 ItemUsageValue (operator[] inserts into the quest map)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g2_violations out fn_text)
  set(errors "")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, player)" "GetBotAI(player)->GetAiObjectContext()" "G2 EmoteAction talk target write")
  require(errors "${fn_text}" "QueueContextWriteTo(player" "G2 EmoteAction inbox write")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g4_trade_violations out fn_text)
  set(errors "")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, trader)" "GetBotAI(trader)" "G4 TradeStatusAction partner")
  # OB-10 review of #1030: the cross-Map branch must not cancel the trade: Player::TradeCancel deletes/nulls the PARTNER's trade data and sends on the
  # partner's session (a foreign write + delete = use-after-free against the partner's region thread). The text before `bool shouldTrade` is only
  # the null check and that branch (the later !shouldTrade cancel acts on a same-Map partner and is legitimate).
  string(FIND "${fn_text}" "bool shouldTrade = true;" cut_at)
  if(cut_at EQUAL -1)
    list(APPEND errors "G4 TradeStatusAction: the checker lost 'bool shouldTrade = true;'")
  else()
    string(SUBSTRING "${fn_text}" 0 ${cut_at} prelude)
    forbid(errors "${prelude}" "HandleCancelTradeOpcode" "G4 TradeStatusAction cross-Map branch (foreign trade data delete)")
    forbid(errors "${prelude}" "TradeCancel" "G4 TradeStatusAction cross-Map branch (foreign trade data delete)")
  endif()
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g4_spell_violations out fn_text)
  set(errors "")
  require(errors "${fn_text}" "if (trader && SameMapObject(bot, trader))" "G4 ItemForSpellValue bot trade partner")
  require(errors "${fn_text}" "if (master && SameMapObject(bot, master))" "G4 ItemForSpellValue master")
  require(errors "${fn_text}" "if (trader && SameMapObject(bot, trader))\n        {" "G4 ItemForSpellValue master trade partner")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g5_violations out fn_text)
  set(errors "")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, member)" "member->HasAura(spellId)" "G5 WorldBuffTravelActions member")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, member)" "AddAura(member, spellId)" "G5 WorldBuffTravelActions member")
  forbid(errors "${fn_text}" "member->GetMapId() != bot->GetMapId()" "G5 WorldBuffTravelActions (map id alone is not the Map object)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g6_violations out fn_text)
  set(errors "")
  require(errors "${fn_text}" "!SameMapObject(bot, master)" "G6 GrindTargetValue master filter")
  forbid(errors "${fn_text}" "master->GetMapId() != bot->GetMapId()" "G6 GrindTargetValue (map id alone)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g7_violations out fn_text)
  set(errors "")
  guard_before(errors "${fn_text}" "!SameMapObject(bot, member)" "ai->IsTank(member)" "G7 CcTargetValue member loop")
  forbid(errors "${fn_text}" "bot->GetMapId() != member->GetMapId()" "G7 CcTargetValue (map id alone)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g8_violations out fn_text)
  set(errors "")
  require(errors "${fn_text}" "player->FindMap() == bot->FindMap()" "G8 PartyMemberValue::Check")
  forbid(errors "${fn_text}" "player->GetMapId() == bot->GetMapId()" "G8 PartyMemberValue::Check (map id alone)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g9_violations out text guard old description)
  set(errors "")
  require(errors "${text}" "${guard}" "${description}")
  forbid(errors "${text}" "${old}" "${description} (map id alone)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g10_violations out fn_text)
  set(errors "")
  require(errors "${fn_text}" "!player || !PlayerbotAI::IsSafe(asker, player)" "G10 AllowedClassRoleNr member loop")
  require(errors "${fn_text}" "!SameMapObject(asker, player)" "G10 AllowedClassRoleNr member loop")
  forbid(errors "${fn_text}" "IsSafe(groupMaster" "G10 AllowedClassRoleNr (a foreign / null first argument)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

# every call of the static IsSafe form in the module must name the calling bot (or the asker of that function) as the first argument
function(static_issafe_violations out)
  set(errors "")
  file(GLOB_RECURSE all_src "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
  foreach(f ${all_src})
    file(READ "${f}" body)
    string(REGEX MATCHALL "PlayerbotAI::IsSafe\\([^,)]*," calls "${body}")
    foreach(call ${calls})
      string(REGEX REPLACE "PlayerbotAI::IsSafe\\(([^,)]*)," "\\1" first "${call}")
      string(STRIP "${first}" first)
      if(NOT first STREQUAL "bot" AND NOT first STREQUAL "asker")
        list(APPEND errors "static IsSafe call with the first argument '${first}' in ${f} (must be the calling bot)")
      endif()
    endforeach()
  endforeach()
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(g11_violations out fn_text)
  set(errors "")
  require(errors "${fn_text}" "!member->IsInWorld() || !SameMapObject(bot, member)" "G11 PlayerbotAI member loop")
  forbid(errors "${fn_text}" "!member->IsInWorld() && member->GetMapId() != bot->GetMapId()" "G11 PlayerbotAI member loop (&&/|| slip)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the real sources ----
read_source("strategy/values/ItemUsageValue.cpp" usage)
region(usage_fn "${usage}" "//QUEST" "IsNeededForQuest(bot, itemId, true)")
g1_violations(g1_errors "${usage_fn}" "${usage}")

read_source("strategy/actions/EmoteAction.cpp" emote)
region(emote_fn "${emote}" "Player* player = dynamic_cast<Player*>(target);" "context->GetValue<ObjectGuid>(\"talk target\")->Set(target->GetObjectGuid());")
g2_violations(g2_errors "${emote_fn}")

read_source("strategy/actions/TradeStatusAction.cpp" trade)
region(trade_fn "${trade}" "bool TradeStatusAction::Execute(Event& event)" "WorldPacket p(event.getPacket());")
g4_trade_violations(g4t_errors "${trade_fn}")

read_source("strategy/values/ItemForSpellValue.cpp" spell)
region(spell_fn "${spell}" "Player* trader = bot->GetTrader();" "// Workaround as some spells have no item mask")
g4_spell_violations(g4s_errors "${spell_fn}")

read_source("strategy/actions/WorldBuffTravelActions.cpp" wb)
region(wb_fn "${wb}" "void WorldBuffTravelApplyAction::ApplyBuffToSelfAndRealPlayers(uint32 spellId)" "\n}\n")
g5_violations(g5_errors "${wb_fn}")

read_source("strategy/values/GrindTargetValue.cpp" grind)
region(grind_fn "${grind}" "Player* master = GetMaster();" "// TEMP-DEBUG(grind-target)")
g6_violations(g6_errors "${grind_fn}")

read_source("strategy/values/CcTargetValue.cpp" cc)
region(cc_fn "${cc}" "Player *member = sObjectMgr.GetPlayer(itr->guid);" "float distance = sServerFacade.GetDistance2d(member, creature);")
g7_violations(g7_errors "${cc_fn}")

read_source("strategy/values/PartyMemberValue.cpp" pmv)
region(pmv_fn "${pmv}" "bool PartyMemberValue::Check(Unit* player)" "bool PartyMemberValue::IsTargetOfSpellCast")
g8_violations(g8_errors "${pmv_fn}")

read_source("strategy/triggers/PullTriggers.cpp" pull)
g9_violations(g9_pull_errors "${pull}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 PullTriggers")
read_source("strategy/actions/ReachTargetActions.h" reach)
g9_violations(g9_reach_errors "${reach}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 ReachTargetActions.h")
read_source("strategy/shaman/ShamanActions.h" shaman)
g9_violations(g9_shaman_errors "${shaman}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 ShamanActions.h")
read_source("strategy/values/AttackerCountValues.cpp" acv)
g9_violations(g9_acv_errors "${acv}" "!player->IsInWorld() || !SameMapObject(bot, player)" "player->IsInWorld() || player->GetMapId() != bot->GetMapId()" "G9 AttackerCountValues")

read_source("strategy/actions/InviteToGroupAction.cpp" invite)
region(invite_fn "${invite}" "LfgAction::AllowedClassRoleNr(Player* player, uint8 groupSize)" "bool LfgAction::Execute(Event& event)")
g10_violations(g10_errors "${invite_fn}")
static_issafe_violations(static_errors)

read_source("PlayerbotAI.cpp" pai)
region(pai_fn "${pai}" "Player* member = gref->getSource();\n            // HARM-RACE guard pair: was" "if (!member->IsInGroup(player, true))")
g11_violations(g11_errors "${pai_fn}")

foreach(set_name g1 g2 g4t g4s g5 g6 g7 g8 g9_pull g9_reach g9_shaman g9_acv g10 static g11)
  if(${set_name}_errors)
    string(REPLACE ";" "\n  - " listed "${${set_name}_errors}")
    message(FATAL_ERROR "${TAG}: ${set_name}:\n  - ${listed}")
  endif()
endforeach()

# ---- negative probes ----
function(expect_rejected name errs)
  if(NOT errs)
    message(FATAL_ERROR "${TAG}: negative probe '${name}' was accepted - the checker no longer sees it")
  endif()
endfunction()

function(mutate out text from to probe_no)
  string(REPLACE "${from}" "${to}" mutated "${text}")
  if("${mutated}" STREQUAL "${text}")
    message(FATAL_ERROR "${TAG}: probe ${probe_no} did not change the text (layout moved?)")
  endif()
  set(${out} "${mutated}" PARENT_SCOPE)
endfunction()

mutate(p1_fn "${usage_fn}" "!SameMapObject(bot, ai->GetMaster()) || " "" 1)
g1_violations(e1 "${p1_fn}" "${usage}")
expect_rejected("G1 master quest read without the Map object test" "${e1}")
mutate(p1b "${usage}" "auto const qIt = player->getQuestStatusMap().find(quest->GetQuestId());" "QuestStatusData& qData = player->getQuestStatusMap()[quest->GetQuestId()];" 1)
g1_violations(e1b "${usage_fn}" "${p1b}")
expect_rejected("G1 operator[] on the quest map" "${e1b}")

mutate(p2_fn "${emote_fn}" "sPlayerbotAIConfig.x3cInboxWrites || !SameMapObject(bot, player)" "sPlayerbotAIConfig.x3cInboxWrites" 2)
g2_violations(e2 "${p2_fn}")
expect_rejected("G2 direct write to a bot on another Map object" "${e2}")

mutate(p4_fn "${trade_fn}" "!SameMapObject(bot, trader)" "false" 3)
g4_trade_violations(e3 "${p4_fn}")
expect_rejected("G4 trade partner without the Map object test" "${e3}")
mutate(p4c_fn "${trade_fn}" "    if (!SameMapObject(bot, trader))\n        return false;" "    if (!SameMapObject(bot, trader))\n    {\n        WorldPacket cancel;\n        bot->GetSession()->HandleCancelTradeOpcode(cancel);\n        return false;\n    }" 3)
g4_trade_violations(e3c "${p4c_fn}")
expect_rejected("G4 cross-Map branch cancels the trade (deletes the partner's trade data)" "${e3c}")

mutate(p4s_fn "${spell_fn}" "if (trader && SameMapObject(bot, trader))" "if (trader)" 4)
g4_spell_violations(e4 "${p4s_fn}")
expect_rejected("G4 ItemForSpellValue trade partner unguarded" "${e4}")

mutate(p5_fn "${wb_fn}" "!SameMapObject(bot, member)" "member->GetMapId() != bot->GetMapId()" 5)
g5_violations(e5 "${p5_fn}")
expect_rejected("G5 WorldBuff member on map id only" "${e5}")

mutate(p6_fn "${grind_fn}" "!SameMapObject(bot, master)" "master->GetMapId() != bot->GetMapId()" 6)
g6_violations(e6 "${p6_fn}")
expect_rejected("G6 GrindTargetValue master on map id only" "${e6}")

mutate(p7_fn "${cc_fn}" "!SameMapObject(bot, member)" "bot->GetMapId() != member->GetMapId()" 7)
g7_violations(e7 "${p7_fn}")
expect_rejected("G7 CcTargetValue member on map id only" "${e7}")

mutate(p8_fn "${pmv_fn}" "player->FindMap() && player->FindMap() == bot->FindMap()" "player->GetMapId() == bot->GetMapId()" 8)
g8_violations(e8 "${p8_fn}")
expect_rejected("G8 PartyMemberValue::Check on map id only" "${e8}")

mutate(p9a "${pull}" "!member->IsInWorld() || !SameMapObject(bot, member)" "!member->IsInWorld() || member->GetMapId() != bot->GetMapId()" 9)
g9_violations(e9a "${p9a}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 PullTriggers")
expect_rejected("G9 PullTriggers on map id only" "${e9a}")
mutate(p9b "${reach}" "!member->IsInWorld() || !SameMapObject(bot, member)" "!member->IsInWorld() || member->GetMapId() != bot->GetMapId()" 10)
g9_violations(e9b "${p9b}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 ReachTargetActions.h")
expect_rejected("G9 ReachTargetActions on map id only" "${e9b}")
mutate(p9c "${shaman}" "!member->IsInWorld() || !SameMapObject(bot, member)" "!member->IsInWorld() || member->GetMapId() != bot->GetMapId()" 11)
g9_violations(e9c "${p9c}" "!member->IsInWorld() || !SameMapObject(bot, member)" "member->IsInWorld() || member->GetMapId() != bot->GetMapId()" "G9 ShamanActions.h")
expect_rejected("G9 ShamanActions on map id only" "${e9c}")
mutate(p9d "${acv}" "!player->IsInWorld() || !SameMapObject(bot, player)" "!player->IsInWorld() || player->GetMapId() != bot->GetMapId()" 12)
g9_violations(e9d "${p9d}" "!player->IsInWorld() || !SameMapObject(bot, player)" "player->IsInWorld() || player->GetMapId() != bot->GetMapId()" "G9 AttackerCountValues")
expect_rejected("G9 AttackerCountValues on map id only" "${e9d}")

mutate(p10_fn "${invite_fn}" "!player || !PlayerbotAI::IsSafe(asker, player)" "!PlayerbotAI::IsSafe(groupMaster, player)" 13)
g10_violations(e10 "${p10_fn}")
expect_rejected("G10 IsSafe(groupMaster, player) (foreign / null first argument)" "${e10}")

# the module-wide static IsSafe check must see a foreign first argument: probe the checker on a synthetic text
string(REGEX MATCHALL "PlayerbotAI::IsSafe\\([^,)]*," probe_calls "PlayerbotAI::IsSafe(groupMaster, player)")
if(NOT probe_calls)
  message(FATAL_ERROR "${TAG}: probe 14: the static IsSafe call regex no longer matches a call")
endif()
string(REGEX REPLACE "PlayerbotAI::IsSafe\\(([^,)]*)," "\\1" probe_first "${probe_calls}")
string(STRIP "${probe_first}" probe_first)
if(probe_first STREQUAL "bot" OR probe_first STREQUAL "asker")
  message(FATAL_ERROR "${TAG}: probe 14: the first-argument extraction accepted 'groupMaster'")
endif()

mutate(p11_fn "${pai_fn}" "!member->IsInWorld() || !SameMapObject(bot, member)" "!member->IsInWorld() && member->GetMapId() != bot->GetMapId()" 15)
g11_violations(e11 "${p11_fn}")
expect_rejected("G11 the &&/|| slip" "${e11}")

message(STATUS "${TAG} contract passed")
