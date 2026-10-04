# Hotfix 8.24 (twow-repo#484): [ComboTrace] diagnostics for lost combo points.
# Text contract: every core path that drops a player's combo points names its reason, the
# trace is logging only (real players, rogue/druid, throttled) and the clear/retarget
# behaviour itself is unchanged. Paths only from TW_CORE_ROOT.
if (NOT TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "${label}: missing ${needle}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/src/game/Objects/Unit.h" unit_h)
foreach (needle
    "enum ComboClearReason : uint8"
    "COMBO_CLEAR_OTHER       = 0,"
    "COMBO_CLEAR_FINISHER    = 1,"
    "COMBO_CLEAR_SELECT      = 2,"
    "COMBO_CLEAR_TARGET_DIED = 3,"
    "COMBO_CLEAR_DEATH       = 4,"
    "COMBO_CLEAR_DUEL        = 5,"
    "void ClearComboPointHolders(ComboClearReason reason = COMBO_CLEAR_OTHER);")
  require_text("${unit_h}" "${needle}" "Unit.h")
endforeach()

file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.h" player_h)
foreach (needle
    "void ClearComboPoints(ComboClearReason reason = COMBO_CLEAR_OTHER);"
    "void AddComboPoints(Unit* target, int8 count, uint32 sourceSpellId = 0, bool fromProc = false);"
    "void TraceComboPoints(char const* reason, uint8 pointsBefore, ObjectGuid const& comboTarget, ObjectGuid const& newTarget = ObjectGuid(), uint32 sourceSpellId = 0);")
  require_text("${player_h}" "${needle}" "Player.h")
endforeach()

# Every reason is passed at its call site.
file(READ "${TW_CORE_ROOT}/src/game/Spells/Spell.cpp" spell_cpp)
require_text("${spell_cpp}" "((Player*)m_caster)->ClearComboPoints(COMBO_CLEAR_FINISHER);" "Spell.cpp finisher")
file(READ "${TW_CORE_ROOT}/src/game/Handlers/MiscHandler.cpp" misc_cpp)
require_text("${misc_cpp}" "_player->ClearComboPoints(COMBO_CLEAR_SELECT);" "MiscHandler.cpp select")
file(READ "${TW_CORE_ROOT}/src/game/Spells/SpellEffects.cpp" effects_cpp)
require_text("${effects_cpp}" "((Player*)m_caster)->AddComboPoints(unitTarget, damage, m_spellInfo->Id, m_triggeredByAuraSpell != nullptr);" "SpellEffects.cpp proc source")
file(READ "${TW_CORE_ROOT}/src/game/Objects/Unit.cpp" unit_cpp)
foreach (needle
    "ClearComboPointHolders(COMBO_CLEAR_TARGET_DIED);"
    "void Unit::ClearComboPointHolders(ComboClearReason reason)"
    "plr->ClearComboPoints(reason);")
  require_text("${unit_cpp}" "${needle}" "Unit.cpp")
endforeach()

file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player_cpp)
foreach (needle
    "ClearComboPoints(COMBO_CLEAR_DEATH);"
    "ClearComboPoints(COMBO_CLEAR_DUEL);"
    "TraceComboPoints(fromProc ? \"proc_other_target\" : \"retarget\", uint8(m_comboPoints), m_comboTargetGuid,"
    "void Player::AddComboPoints(Unit* target, int8 count, uint32 sourceSpellId, bool fromProc)"
    "{ \"other\", \"finisher\", \"select\", \"target_died\", \"death\", \"duel\" }"
    "[ComboTrace] player=%u reason=%s cp=%u target=%s new_target=%s spell=%u selection=%s map=%u"
    "[ComboTrace] player=%u suppressed=%u window_s=%u"
    "if (!pointsBefore || (GetClass() != CLASS_ROGUE && GetClass() != CLASS_DRUID))"
    "if (!GetSession() || !GetSession()->GetSocket())"
    "if (m_comboTraceLines >= 30)")
  require_text("${player_cpp}" "${needle}" "Player.cpp")
endforeach()

# Behaviour unchanged: ClearComboPoints still zeroes, updates and clears the target; the
# retarget branch still moves the points to the new target.
string(FIND "${player_cpp}" "void Player::ClearComboPoints(ComboClearReason reason)" clear_at)
string(FIND "${player_cpp}" "void Player::TraceComboPoints(" trace_at)
if (clear_at EQUAL -1 OR trace_at EQUAL -1 OR trace_at LESS clear_at)
  message(FATAL_ERROR "Player.cpp: ClearComboPoints / TraceComboPoints not found in order")
endif()
math(EXPR clear_len "${trace_at} - ${clear_at}")
string(SUBSTRING "${player_cpp}" ${clear_at} ${clear_len} clear_body)
foreach (needle
    "RemoveSpellsCausingAura(SPELL_AURA_RETAIN_COMBO_POINTS);"
    "m_comboPoints = 0;"
    "SetComboPoints();"
    "target->RemoveComboPointHolder(GetGUIDLow());"
    "m_comboTargetGuid.Clear();")
  require_text("${clear_body}" "${needle}" "ClearComboPoints behaviour")
endforeach()
string(FIND "${player_cpp}" "void Player::AddComboPoints(Unit* target, int8 count, uint32 sourceSpellId, bool fromProc)" add_at)
math(EXPR add_len "${clear_at} - ${add_at}")
string(SUBSTRING "${player_cpp}" ${add_at} ${add_len} add_body)
foreach (needle
    "m_comboTargetGuid = target->GetObjectGuid();"
    "m_comboPoints = count;"
    "target->AddComboPointHolder(GetGUIDLow());")
  require_text("${add_body}" "${needle}" "AddComboPoints behaviour")
endforeach()

message(STATUS "COMBO_TRACE_CONTRACT=PASS")
