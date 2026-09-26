if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# #290: "spear" is the 10th named formation (with near, melee, line, circle,
# arrow, queue, chaos, far, shield) and reachable via `formation spear|wedge`.
file(READ "${PB_SOURCE_DIR}/strategy/values/Formations.cpp" formations)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

require_text("${formations}" "class SpearFormation : public MoveFormation" "spear formation class")
require_text("${formations}" "formation == \"spear\" || formation == \"wedge\"" "spear/wedge in FormationValue::Load")
require_text("${formations}" "value = new SpearFormation(ai);" "spear formation instance")
require_text("${formations}" "spear_formation::SlotOffset(" "spear geometry from the tested policy")
require_text("${formations}" "shield, arrow, spear, melee" "spear in the formation help list")

foreach(name melee queue chaos circle line shield arrow spear far)
  require_text("${formations}" "formation == \"${name}\"" "named formation ${name}")
endforeach()
require_text("${formations}" "formation == \"near\" || formation == \"default\"" "named formation near")

message(STATUS "SPEAR_FORMATION_SOURCE_CONTRACT=PASS")
