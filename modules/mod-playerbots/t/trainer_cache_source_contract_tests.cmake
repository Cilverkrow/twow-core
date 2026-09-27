function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(require_count text needle expected description)
  string(REGEX MATCHALL "${needle}" hits "${text}")
  list(LENGTH hits count)
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "${description}: expected ${expected}, found ${count}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AutoLearnSpellAction.cpp" learn)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #351: class trainers and class quests are collected once per class ...
require_text("${learn}" "std::call_once(built[cls]" "cache built once per class")
require_text("${learn}" "for (CreatureInfo const* co : LearnCacheFor(bot->getClass()).trainers)" "trainer pass uses the cache")
require_text("${learn}" "for (auto const& [questId, quest] : LearnCacheFor(bot->getClass()).quests)" "quest pass uses the cache")
# ... so the full scans exist exactly once (in the cache builder), not per level-up.
require_count("${learn}" "sCreatureStorage\.GetMaxEntry\(\)" 1 "full creature scans")
require_count("${learn}" "GetQuestTemplates\(\)" 1 "full quest scans")
# Tradeskill trainers stay excluded (plan-aware profession path only).
require_text("${learn}" "if (co->TrainerType != TRAINER_TYPE_CLASS && co->TrainerType != TRAINER_TYPE_PETS)" "class and pet trainers only")
require_text("${learn}" "[AutoLearn] timing bot=%u" "timing diagnostics")
require_text("${learn}" "[AutoLearn] cache class=%u" "before/after reference")
require_text("${config_template}" "AiPlayerbot.AutoLearn.TimingTrace = 0" "documented timing switch")
