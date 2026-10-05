# Hotfix 8.25 (twow-repo#484): the two combo point switches exist with default 0 and are
# wired through FunserverComboPolicy.h (behaviour unit tested in combo_policy_test).
# Paths only from TW_CORE_ROOT.
if (NOT TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${label}: missing ${needle}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/src/game/World.h" world_h)
require_text("${world_h}" "CONFIG_BOOL_ROGUE_KEEP_COMBO_ON_SELECT," "World.h")
require_text("${world_h}" "CONFIG_BOOL_ROGUE_PROC_COMBO_TO_CURRENT_TARGET," "World.h")

# Default off in code and in the shipped config.
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world_cpp)
require_text("${world_cpp}" "setConfig(CONFIG_BOOL_ROGUE_KEEP_COMBO_ON_SELECT, \"Rogue.KeepComboPointsOnSelect\", false);" "World.cpp default")
require_text("${world_cpp}" "setConfig(CONFIG_BOOL_ROGUE_PROC_COMBO_TO_CURRENT_TARGET, \"Rogue.ProcComboPointsToCurrentTarget\", false);" "World.cpp default")
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" dist)
require_text("${dist}" "\nRogue.KeepComboPointsOnSelect = 0\n" "mangosd.conf.dist.in default")
require_text("${dist}" "\nRogue.ProcComboPointsToCurrentTarget = 0\n" "mangosd.conf.dist.in default")

# Selection rule goes through the policy with the switch.
file(READ "${TW_CORE_ROOT}/src/game/Handlers/MiscHandler.cpp" misc)
foreach (needle
    "#include \"FunserverComboPolicy.h\""
    "if (ShouldClearComboPointsOnSelect(sWorld.getConfig(CONFIG_BOOL_ROGUE_KEEP_COMBO_ON_SELECT),"
    "_player->ClearComboPoints(COMBO_CLEAR_SELECT);")
  require_text("${misc}" "${needle}" "MiscHandler.cpp")
endforeach()

# Proc redirect only behind the switch, only for aura-triggered points, traced.
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
foreach (needle
    "#include \"FunserverComboPolicy.h\""
    "if (fromProc && count > 0 && sWorld.getConfig(CONFIG_BOOL_ROGUE_PROC_COMBO_TO_CURRENT_TARGET))"
    "ComboProcRedirect const redirect = DecideComboProcRedirect(in);"
    "TraceComboPoints(\"proc_redirect\""
    "in.comboTargetValid = comboTarget && comboTarget->IsAlive() && IsValidAttackTarget(comboTarget);"
    "in.selectionValid = selection && selection->IsAlive() && IsValidAttackTarget(selection);")
  require_text("${player}" "${needle}" "Player.cpp")
endforeach()
string(FIND "${player}" "if (fromProc && count > 0 && sWorld.getConfig(CONFIG_BOOL_ROGUE_PROC_COMBO_TO_CURRENT_TARGET))" redirect_at)
string(FIND "${player}" "RemoveSpellsCausingAura(SPELL_AURA_RETAIN_COMBO_POINTS);" retain_at)
if (redirect_at EQUAL -1 OR retain_at LESS redirect_at)
  message(FATAL_ERROR "Player.cpp: the redirect must run before the combo point update")
endif()

file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellEffects.cpp" effects)
require_text("${effects}" "((Player*)m_caster)->SetGuidValue(PLAYER_FIELD_COMBO_TARGET, ((Player*)m_caster)->GetComboTargetGuid());" "SpellEffects.cpp combo target field")

file(READ "${TW_CORE_ROOT}/src/game/FunserverComboPolicy.h" policy)
foreach (needle
    "inline bool ShouldClearComboPointsOnSelect(bool keepOnSelectSwitch, bool isRogueOrDruid,"
    "inline ComboProcRedirect DecideComboProcRedirect(ComboProcRedirectInput const& in)"
    "if (!in.switchOn || !in.fromProc || !in.isRogue || in.procTargetIsComboTarget)")
  require_text("${policy}" "${needle}" "FunserverComboPolicy.h")
endforeach()

message(STATUS "COMBO_SWITCHES_CONTRACT=PASS")
