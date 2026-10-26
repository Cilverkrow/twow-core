cmake_policy(VERSION 3.16)
# twow-repo#575 (PATH-01 wave 0): behind AiPlayerbot.CustomRaceStartZones (default 0) goblins quest on Blackstone Island and
# high elves in the Thalassian Highlands / Alah'Thalas. Every site goes through StartZonePolicy.h; off keeps the old
# behaviour apart from Alah'Thalas 2040 joining the random-teleport filter.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#575 wave 0: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#575 wave 0: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#575 wave 0: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotFactory.cpp" factory)
read_source("strategy/actions/ReleaseSpiritAction.h" repop)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("TravelNode.cpp" travel_node)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("strategy/actions/MovementActions.cpp" movement)
read_source("PlayerbotAI.cpp" botai)

# 1. Switch: default 0 in code and conf, documented as "nach Abnahme an".
require_text("${config_h}" "    bool customRaceStartZones = false;  // twow-repo#575 (PATH-01 wave 0)" "member default off")
require_text("${config_cpp}" "customRaceStartZones = config.GetBoolDefault(\"AiPlayerbot.CustomRaceStartZones\", false);" "load default off")
require_text("${conf_dist}" "\nAiPlayerbot.CustomRaceStartZones = 0\n" "documented off")
require_text("${conf_dist}" "# twow-repo#575 (PATH-01 wave 0, BEHAVIOUR CHANGE, \"nach Abnahme an\"" "acceptance note")

# 2. E1 factory, E2 repop, E3 teleports.
require_text("${factory}" "bool const proxyStart = ai::start_zone::FactoryUsesProxyStart(race, sPlayerbotAIConfig.customRaceStartZones);" "E1 switch")
require_text("${factory}" "    if (proxyStart && race == RACE_GOBLIN)\n" "E1 goblin block gated")
require_text("${factory}" "    if (proxyStart && race == RACE_HIGH_ELF)\n" "E1 high-elf block gated")
require_text("${repop}" "bool useHomebindOverride = ai::start_zone::RepopUsesHomebind(bot->getRace(), bot->GetLevel(), sPlayerbotAIConfig.customRaceStartZones);" "E2")
forbid_text("${repop}" "bool useHomebindOverride = bot->getRace() == RACE_GOBLIN" "E2 old race-only rule")
require_text("${rnd_mgr}" "if (ai::start_zone::TeleportSkipsZone(zoneId, bot->getRace(), sPlayerbotAIConfig.customRaceStartZones))\n            return true;" "E3")
forbid_text("${rnd_mgr}" "if (zoneId == 5536 || zoneId == 5225)" "E3 old filter without 2040")

# 3. E4 boot tagging (box test only, no zone lookup: deadlock note) and E5 faction filter in both GetDestinations loops.
require_text("${travel_h}" "void SetStartZoneTeam(uint8 team) { startZoneTeam = team; }" "E4 tag setter")
require_text("${travel_cpp}" "static ai::start_zone::StartTeam StartZoneTeamOf(Points const& points, Dests const& dests)" "E4 fold")
require_text("${travel_cpp}" "    if (!sPlayerbotAIConfig.customRaceStartZones)\n        return IsInBlackstoneOrThalassian(guidP.getMapId(), guidP.getX(), guidP.getY());" "E4 off = old exclusion")
string(REGEX MATCHALL "StartZoneTeamOf\\(guidpMap\\.at\\(entry\\), (locs|dests)\\)" folds "${travel_cpp}")
list(LENGTH folds fold_count)
if(NOT fold_count EQUAL 2)
  message(FATAL_ERROR "#575 wave 0: expected the E4 fold in both loaders, found ${fold_count}")
endif()
string(REGEX MATCHALL "if \\(SkipStartZonePoint\\(guidP, startTeam\\)\\)" skips "${travel_cpp}")
list(LENGTH skips skip_count)
if(NOT skip_count EQUAL 2)
  message(FATAL_ERROR "#575 wave 0: expected SkipStartZonePoint in both loaders, found ${skip_count}")
endif()
forbid_text("${travel_cpp}" "GetZoneAndAreaId(" "zone lookup in the boot loaders")
string(REGEX MATCHALL "DestinationAllows\\(ai::start_zone::StartTeam\\(dest->GetStartZoneTeam\\(\\)\\), info\\.GetTeam\\(\\) == ALLIANCE\\)" allows "${travel_cpp}")
list(LENGTH allows allow_count)
if(NOT allow_count EQUAL 2)
  message(FATAL_ERROR "#575 wave 0: expected the E5 filter in both GetDestinations loops, found ${allow_count}")
endif()

# 4. Area-level exemption: danger check and zone-below search.
# twow-repo#601 S3b: the danger check moved with the main loop's other post-IsActive deferrals into PostActiveDeferralOf.
require_text("${choose}" "if (danger == route_danger::Reason::TargetZoneLevel &&\n            !ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones" "danger exemption")
require_text("${choose}" "ai::quest_search::ZoneBelowBot(WorldPosition(bot).getAreaLevelOrParent(), bot->GetLevel()) &&\n            !ai::start_zone::ZoneLevelExempt(" "zone-below exemption")

# 5. E7 scripted taxi and E8 gates in the graph cost.
require_text("${travel_node}" "!(sPlayerbotAIConfig.customRaceStartZones && ai::start_zone::IsScriptedTaxi(pathObject)))" "E7 known-node skip")
require_text("${travel_node}" "if (ai::start_zone::FlightGated(sPlayerbotAIConfig.customRaceStartZones, pathObject, bot->GetLevel()))\n                return -1;" "E8 flight gate")
require_text("${travel_node}" "ai::start_zone::WalkOutGated(sPlayerbotAIConfig.customRaceStartZones, bot->GetLevel()" "E8 walk gate")
require_order("${movement}" "if (sPlayerbotAIConfig.customRaceStartZones && ai::start_zone::IsScriptedTaxi(entry))" "unit = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_FLIGHTMASTER);" "E7 before the flight-master search")
require_text("${movement}" "flew = bot->ActivateTaxiPathTo(entry, 0, true);" "E7 takes the path like the gossip script")

# 6. Q1 quest flights and S1 ship, both off without the switch; 8.29 stays the safety net.
require_text("${botai}" "if (startZones && freeToTravel && !HasRealPlayerMaster() && bot->getRace() == sz::GoblinRace && bot->GetMapId() == 1)" "Q1 gated")
require_text("${botai}" "bot->IsCurrentQuest(sz::QuestBadNews, 0), bot->IsCurrentQuest(sz::QuestSolutions, 0));" "Q1 by quest log")
require_text("${botai}" "sz::IslandLeaveAllowed(sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), startZones)" "8.29 roster rule kept off")
# F2 (option B): on, no 15-minute idle exit below level 10; off, 8.29's leave rule unchanged.
require_text("${botai}" "gi::ShouldLeaveIsland(bot->GetZoneId(), bot->GetLevel(),
                        sz::IslandLeaveIdle(startZones, questProgress.IdleSeconds(now)))" "8.29 leave rule, idle exit off with the switch")
require_order("${botai}" "uint32 questFlight = 0;" "else if ((islandCandidate || HasRealPlayerMaster()) && freeToTravel)" "Q1 before 8.29")
# Wave-0 A/B 16.10.: the walk to a machine waits while the bot is in a death series or a death loop (island scorpid
# field); 8.29 is guarded only with the switch on, off it is unchanged. F4: on the island, with the switch, the walk follows
# the travel graph; the straight MovePoint stays the fallback.
require_text("${botai}" "else if (!IsCautious() && !IsInDeathLoop() &&
                    (questFlight != sz::IslandPath || !WalkToMachineByGraph(machine.x, machine.y, machine.z, now)))
                    bot->GetMotionMaster()->MovePoint(0, machine.x, machine.y, machine.z, MOVE_PATHFINDING);" "Q1 walk guarded, graph walk on the island")
require_text("${botai}" "else if (!startZones || (!IsCautious() && !IsInDeathLoop()))
                    {
                        // F4: on, along the travel graph round the field; off, 8.29's straight walk.
                        if (!startZones || !WalkToMachineByGraph(gi::IslandMachineX, gi::IslandMachineY, gi::IslandMachineZ, now))
                            bot->GetMotionMaster()->MovePoint(0, gi::IslandMachineX, gi::IslandMachineY, gi::IslandMachineZ, MOVE_PATHFINDING);" "8.29 walk guarded and graph walk only with the switch")
# F4: the graph walk - gated, never blocking on the graph lock, unitless route, walk links only, one log line.
require_text("${botai}" "if (!sz::UsesGraphWalk(sPlayerbotAIConfig.customRaceStartZones, bot->GetDistance(x, y, z)))\n        return false;" "F4 gated by the switch")
require_text("${botai}" "if (!graphLock.try_lock_for(std::chrono::milliseconds(50)))\n            return fallback(\"lock\");" "F4 graph lock never blocks")
require_text("${botai}" "TravelNodeRoute nodeRoute = sTravelNodeMap.getRoute(starts.front(), goals.front(), nullptr);" "F4 unitless route")
require_text("${botai}" "link->second.getPathType() != TravelNodePathType::walk ||\n                !link->second.getComplete())" "F4 complete walk links only")
require_text("${botai}" "auto const link = nodes[i]->getPaths()->find(nodes[i + 1]);" "F4 find, no operator[] (would write the graph)")
forbid_text("${botai}" "getPathTo(nodes[" "F4 operator[] lookup (would write the graph)")
require_text("${botai}" "if (!(prefix.getPathType() & PATHFIND_NORMAL) || prefix.getPath().size() < 2)" "F4 navmesh prefix to the first node")
require_text("${botai}" "bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)\n        return true;" "F4 re-plan unless our point walk runs")
require_text("${botai}" "[Travel] state=machine_walk bot=%u level=%u nodes=%u points=%u yd=%.0f" "F4 log line")
require_text("${botai}" "[Travel] state=machine_walk_fallback bot=%u level=%u reason=%s" "F4 fallback line")
# F2: no 8.29 exit while 41170 waits for its hand-in (L10), the hub rescue is marked and cleared by progress.
require_text("${botai}" "!sz::IslandLeaveBlocked(startZones, bot->GetLevel(), bot->IsCurrentQuest(sz::QuestSolutions, 0)))" "F2 41170 keeps the goblin")
require_text("${botai}" "islandHubRescuePending = false;   // twow-repo#575 (PATH-01 F2): progress after an island hub rescue" "F2 progress clears the hub mark")
require_text("${rnd_mgr}" "botAI->MarkIslandHubRescue();" "F2 hub rescue marked")
# F2: the rescue keeps a goblin on its island (hub / machine) without a rebind; the hearthstone follows once it has left.
require_text("${rnd_mgr}" "sz::IslandRescue const island = sz::IslandRescueOf(sPlayerbotAIConfig.customRaceStartZones, bot->getRace(), level," "F2 rescue gated")
require_order("${rnd_mgr}" "sz::IslandRescueOf(" "targets.push_back(WorldLocation(1, gi::PortMachineX, gi::PortMachineY, gi::PortMachineZ, 0.0f));" "F2 before the Durotar targets")
require_text("${rnd_mgr}" "!ai::start_zone::RescueKeepsHomebind(sPlayerbotAIConfig.customRaceStartZones," "F2 no rebind at a start-zone target")
require_text("${botai}" "if (startZones && bot->getRace() == sz::GoblinRace && freeToTravel && !HasRealPlayerMaster() &&\n                sz::RebindOffIsland(true," "F2 rebind off the island gated")
require_text("${botai}" "[Travel] state=island_rebind bot=%u level=%u to_x=%.0f to_y=%.0f area=%u" "F2 rebind log line")
# The switch proves itself in [ConfigEcho] (config_echo_source_contract checks order and parse line).
require_text("${config_cpp}" "            {\"CustomRaceStartZones\", Value(customRaceStartZones)},\n" "echo entry")
require_text("${botai}" "sz::ShipCandidate(startZones, bot->getRace(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY())" "S1 gated")
require_text("${botai}" "bot->TeleportTo(sz::AuberdineMap, sz::AuberdineDockX, sz::AuberdineDockY, sz::AuberdineDockZ, bot->GetOrientation());" "S1 move")

# 7. Wave 2: area team overrides (owner 14.10.: Stormbreaker Point is a Horde camp), empty = AreaTable only.
read_source("WorldPosition.cpp" world_pos)
require_text("${config_h}" "    std::map<uint32, uint32> areaTeamOverrides;  // twow-repo#575 (PATH-01 wave 2)" "override member")
require_text("${config_cpp}" "split(config.GetStringDefault(\"AiPlayerbot.AreaTeamOverrides\", \"\"), ',')" "override list, default empty")
require_text("${conf_dist}" "\nAiPlayerbot.AreaTeamOverrides =\n" "documented empty")
require_text("${world_pos}" "uint32 areaTeam = ai::area_team::TeamOf(teamOverrides, area->Id, area->Team);" "area override in isEnemyHomeZoneFor")
require_text("${world_pos}" "areaTeam = ai::area_team::TeamOf(teamOverrides, zone->Id, zone->Team);" "zone override in isEnemyHomeZoneFor")

message(STATUS "start_zone source contract passed")
