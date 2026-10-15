# twow-repo#551 (owner 11.10.2026): AiPlayerbot.Park.Seats (default 0). Parked bots sit on inn chairs, dismount, keep the
# first ring around the innkeeper free; tele targets next to an inn park in it.

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
    message(FATAL_ERROR "#551 park seats: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#551 park seats: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#551 park seats: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#551 park seats: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()
function(count_text text needle out_var)
  string(REPLACE "${needle}" "" without "${text}")
  string(LENGTH "${text}" len_all)
  string(LENGTH "${without}" len_without)
  string(LENGTH "${needle}" len_needle)
  math(EXPR n "(${len_all} - ${len_without}) / ${len_needle}")
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("ParkPolicy.h" policy)

require_text("${config_h}" "bool parkSeats = false;" "switch default off")
require_text("${config_cpp}" "parkSeats = config.GetBoolDefault(\"AiPlayerbot.Park.Seats\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Park.Seats = 0" "documented")

# Seats only with the switch, chair seat first, fallback rings otherwise.
between("${mgr}" "// The bot's own place at the spot: rings around it" "parkedBots[bot->GetGUIDLow()] = entry;" place)
require_order("${place}" "if (sPlayerbotAIConfig.parkSeats)" "std::vector<ai::park::Seat> const& seats = SeatsFor(spot, spotKey);" "seats behind the switch")
require_order("${place}" "if (slot < seats.size())" "place = ai::park::FallbackPlace(ParkPoint(spot), slot, uint32(seats.size()));" "seat before the fallback")
require_text("${place}" "entry.seatHeight = seatHeight;" "seat stored with the entry")

# Seat gathering: CHAIR objects of the spot's map, near the spot, once per spot.
between("${mgr}" "std::vector<ai::park::Seat> const& RandomPlayerbotMgr::SeatsFor(" "\n}\n" seats_fn)
require_text("${seats_fn}" "info->type != GAMEOBJECT_TYPE_CHAIR" "chairs only")
require_text("${seats_fn}" "ai::park::SeatNearSpot(center," "near the innkeeper")
require_order("${seats_fn}" "if (cached != parkSeatCache.end())\n        return cached->second;" "sObjectMgr.DoGOData(collect);" "scan once per spot")

# Sitting: chair stand state at the seat, out of combat only; without a seat the old floor sit.
between("${mgr}" "case ai::park::Stage::Parked:" "case ai::park::Stage::BindFailed:" parked_head)
between("${mgr}" "// twow-repo#551 (Park.Seats): on its chair like a player" "bot->SetStandState(UNIT_STAND_STATE_SIT);" sit)
require_text("${sit}" "uint8 const chairState = uint8(UNIT_STAND_STATE_SIT_LOW_CHAIR + entry.seatHeight);" "chair height")
require_text("${sit}" "!bot->IsInCombat() && !bot->IsMoving() && bot->IsAlive()" "never in combat")
require_order("${sit}" "bot->NearTeleportTo(entry.inn.coord_x, entry.inn.coord_y, entry.inn.coord_z, entry.inn.orientation);" "bot->SetStandState(chairState);" "back on the seat before sitting")

# Tele targets next to an inn go into the inn; dismount and no mount strategy while parked.
require_order("${mgr}" "spot = WorldLocation(tele->mapId, tele->x, tele->y, tele->z, tele->o);" "int const snapped = ai::park::SnapToInn(ParkPoint(spot), innPoints);" "tele snap after the tele point")
require_text("${mgr}" "via = snappedInn ? \"tele_inn\" : \"tele\";" "visible in the log")
count_text("${mgr}" "if (sPlayerbotAIConfig.parkSeats)   // twow-repo#551 (Park.Seats): nobody rides into the inn" dismounts)
if (NOT dismounts EQUAL 2)
  message(FATAL_ERROR "#551 park seats: both park sites dismount (found ${dismounts})")
endif()
require_text("${mgr}" "        ai->ChangeStrategy(\"-mount\", BotState::BOT_STATE_NON_COMBAT);
        if (bot->IsMounted())
            bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);" "mount strategy dropped and dismounted")

# Policy: the first ring stays free, a 60 yd snap, 15 yd chair radius.
require_text("${policy}" "constexpr std::uint32_t FallbackRingSkip = 8;" "first ring skipped")
require_text("${policy}" "constexpr float TeleSnapYards = 60.0f;" "snap distance")
require_text("${policy}" "constexpr float SeatRadiusYards = 15.0f;" "chair radius")

# Acceptance signal (OB-30, 14.10.): one [Park] state=seated line per park, seat or fallback, with place and stand state.
require_order("${mgr}" "bot->SetStandState(chairState);" "[Park] state=seated bot=%u slot=%u seat=1 stand=%u" "seated line after sitting down")
require_text("${mgr}" "[Park] state=seated bot=%u slot=%u seat=0 stand=%u" "fallback place logged too")
require_text("${mgr}" "if (!entry.seatedLogged)" "once per park")

message(STATUS "park_seats source contract passed")
