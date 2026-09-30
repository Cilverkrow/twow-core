function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)

# Hotfix 8.1: [TankPath] diagnostic, tank strategies only, one sample per second,
# summaries only in a group or an instance.
require_text("${ai_cpp}" "UpdateTankPathDiag(uint32(time(nullptr)));" "diagnostic called from UpdateAI")
require_text("${ai_cpp}" "HasStrategy(\"tank rogue\", BotState::BOT_STATE_COMBAT) || HasStrategy(\"tank shaman\", BotState::BOT_STATE_COMBAT)" "tank strategies only")
require_text("${ai_cpp}" "if (now - tankPathLastSample < ai::tank_path_diag::SampleSeconds)" "sample rate limit")
require_text("${ai_cpp}" "if (group || bot->GetMap()->IsDungeon())" "summary only in a group or an instance")
require_text("${ai_cpp}" "pSpellInfo->Effect[i] == SPELL_EFFECT_ATTACK_ME || pSpellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_TAUNT" "taunts by effect")
require_text("${ai_cpp}" "[TankPath] state=summary bot=%u" "summary line")
