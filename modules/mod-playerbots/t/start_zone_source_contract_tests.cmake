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
require_text("${choose}" "if (danger == route_danger::Reason::TargetZoneLevel &&\n                        !ai::start_zone::ZoneLevelExempt(sPlayerbotAIConfig.customRaceStartZones" "danger exemption")
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
require_text("${botai}" "gi::ShouldLeaveIsland(bot->GetZoneId(), bot->GetLevel(), questProgress.IdleSeconds(now))" "8.29 leave rule")
require_order("${botai}" "uint32 questFlight = 0;" "else if ((islandCandidate || HasRealPlayerMaster()) && freeToTravel)" "Q1 before 8.29")
# Wave-0 A/B 16.10.: the walk to a machine waits while the bot is in a death series or a death loop (island scorpid
# field); 8.29 is guarded only with the switch on, off it is unchanged.
require_text("${botai}" "else if (!IsCautious() && !IsInDeathLoop())
                    bot->GetMotionMaster()->MovePoint(0, machine.x, machine.y, machine.z, MOVE_PATHFINDING);" "Q1 walk guarded")
require_text("${botai}" "else if (!startZones || (!IsCautious() && !IsInDeathLoop()))
                        bot->GetMotionMaster()->MovePoint(0, gi::IslandMachineX, gi::IslandMachineY, gi::IslandMachineZ, MOVE_PATHFINDING);" "8.29 walk guarded only with the switch")
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
