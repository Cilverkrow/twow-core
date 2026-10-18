# twow-repo#596 D (soak 17.10.2026, bot Zinkbolt: "zone escape" 126 ms about once a minute): AiPlayerbot.Perf.
# BindAreaFromHomebind (default 0) - the home bind's area from Player::GetHomebindAreaId instead of a terrain lookup at
# the (often far) bind position, in the zone-escape trigger and the hearthstone check.

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
    message(FATAL_ERROR "#596 bind area: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#596 bind area: forbidden ${description}: ${needle}")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("HomeBindArea.h" helper)
read_source("strategy/triggers/ZoneEscapeTriggers.cpp" escape)
read_source("strategy/actions/UseItemAction.cpp" useitem)
file(READ "${PB_SOURCE_DIR}/../../../../src/game/Objects/Player.h" player_h)

require_text("${config_h}" "bool perfBindAreaFromHomebind = false;" "switch default off")
require_text("${config_cpp}" "perfBindAreaFromHomebind = config.GetBoolDefault(\"AiPlayerbot.Perf.BindAreaFromHomebind\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Perf.BindAreaFromHomebind = 0" "documented")
require_text("${player_h}" "uint16 GetHomebindAreaId() const { return m_homebindAreaId; }" "core getter")

# The helper: the stored area only with the switch, the terrain lookup otherwise; the level by area id, as getAreaLevel.
require_text("${helper}" "if (sPlayerbotAIConfig.perfBindAreaFromHomebind && bot && bot->GetHomebindAreaId())" "stored area behind the switch")
require_text("${helper}" "return bind.GetArea();" "terrain lookup without the switch")
require_text("${helper}" "if (!sPlayerbotAIConfig.perfBindAreaFromHomebind)\n            return bind.getAreaLevel();" "level as before without the switch")
require_text("${helper}" "return area ? sTravelMgr.GetAreaLevel(area->ID) : 0;" "level by area id")

# Both hot callers go through the helper; no bind terrain lookup is left there.
require_text("${escape}" "if (AreaTableEntry const* area = ai::home_bind::Area(bot, bind))" "zone escape: bind area")
require_text("${escape}" "ai::home_bind::AreaLevel(bot, bind)" "zone escape: bind level")
forbid_text("${escape}" "bind.GetArea()" "a terrain lookup of the bind in zone escape")
forbid_text("${escape}" "bind.getAreaLevel()" "a terrain lookup of the bind level in zone escape")
require_text("${useitem}" "ai::home_bind::AreaLevel(bot, bind)" "hearthstone check: bind level")
forbid_text("${useitem}" "bind.getAreaLevel()" "a terrain lookup of the bind level in the hearthstone check")

message(STATUS "bind_area source contract passed")
