# Hotfix 8.35 (twow-repo#544): deaths without a killer feed the danger map (switch, default off).
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/DangerMapPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)
foreach (pair
    "ai_cpp|bool const environment = !killer && bot->getAttackers().empty() && sPlayerbotAIConfig.dangerMapCountEnvironmentDeaths;"
    "ai_cpp|death.killerLevel = environment ? danger_map::EnvironmentKillerLevel"
    "ai_cpp|[DangerMap] state=environment bot=%u"
    "policy|constexpr std::uint8_t EnvironmentKillerLevel = 255;"
    "config_cpp|AiPlayerbot.DangerMap.CountEnvironmentDeaths\", false"
    "config_dist|AiPlayerbot.DangerMap.CountEnvironmentDeaths = 0")
  string(FIND "${pair}" "|" bar)
  string(SUBSTRING "${pair}" 0 ${bar} var)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${pair}" ${start} -1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "environment deaths: missing ${needle}")
  endif()
endforeach()
message(STATUS "ENV_DEATH_CONTRACT=PASS")
