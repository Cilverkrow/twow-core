if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# #389 formations v2 (was #290 spear): the grid shapes are registered with
# their aliases, use the tested policy and the configured spacing/caps; the
# ten old formations (incl. the old "circle") stay reachable.
file(READ "${PB_SOURCE_DIR}/strategy/values/Formations.cpp" formations)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

require_text("${formations}" "class GridFormation : public MoveFormation" "grid formation class")
require_text("${formations}" "sPlayerbotAIConfig.formationSpacing" "configured slot spacing")
require_text("${formations}" "formation_grid::MaxExtentFor(" "configured radius cap")

foreach(pair "spear|wedge|WEDGE" "ring|schutzring|CIRCLE" "vanguard|vorhut|VANGUARD"
    "rearguard|nachhut|REARGUARD" "triangle|dreieck|TRIANGLE" "block|rectangle|BLOCK"
    "column|kolonne|COLUMN")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 name)
  list(GET parts 1 alias)
  list(GET parts 2 shape)
  require_text("${formations}" "formation == \"${name}\" || formation == \"${alias}\"" "${name}/${alias} in FormationValue::Load")
  require_text("${formations}" "new GridFormation(ai, \"${name}\", formation_grid::Shape::${shape})" "${name} grid instance")
endforeach()
require_text("${formations}" "spear, melee, far, ring, vanguard, rearguard, triangle, block, column, dragonslayer" "v2 names in the help list")
require_text("${formations}" "formation == \"dragonslayer\" || formation == \"dragon\"" "dragonslayer in FormationValue::Load")
require_text("${formations}" "new GridFormation(ai, \"dragonslayer\", formation_grid::Shape::DRAGONSLAYER)" "dragonslayer grid instance")
require_text("${formations}" "formation == \"giantkiller\" || formation == \"giant\"" "giantkiller in FormationValue::Load")
require_text("${formations}" "new GridFormation(ai, \"giantkiller\", formation_grid::Shape::GIANTKILLER)" "giantkiller grid instance")
require_text("${formations}" "roles.leaderIsTank = ai->IsTank(followTarget);" "leader role for the giant killer tip")
require_text("${formations}" "dragonslayer, giantkiller, default" "giantkiller in the help list")
require_text("${formations}" "formation_grid::SlotsForRoles(shape, roles" "slots from the role split")

foreach(name melee queue chaos circle line shield arrow far)
  require_text("${formations}" "formation == \"${name}\"" "named formation ${name}")
endforeach()
require_text("${formations}" "formation == \"near\" || formation == \"default\"" "named formation near")

# Owner 2026-09-28: line, shield and arrow rebuilt on the grid. The old
# outline classes (5 yd apart, rows in front of the leader, arrow lines
# stacked on top of each other) are gone; shield/arrow get the tank count.
foreach(pair "line|LINE" "shield|SHIELD" "arrow|ARROW")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 name)
  list(GET parts 1 shape)
  require_text("${formations}" "new GridFormation(ai, \"${name}\", formation_grid::Shape::${shape})" "${name} grid instance")
endforeach()
require_text("${formations}" "uint32(tanks.size())" "tank count for shield and arrow")
foreach(old "class LineFormation" "class ShieldFormation" "new ArrowFormation(ai)")
  string(FIND "${formations}" "${old}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "Old outline formation still in use: ${old}")
  endif()
endforeach()

foreach(key Spacing CircleMaxRadius MaxExtent)
  require_text("${config}" "\"AiPlayerbot.Formation.${key}\"" "config key AiPlayerbot.Formation.${key}")
endforeach()

message(STATUS "FORMATION_V2_SOURCE_CONTRACT=PASS")
