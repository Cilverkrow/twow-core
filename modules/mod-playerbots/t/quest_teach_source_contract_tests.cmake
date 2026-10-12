# twow-repo#578 (b) (owner 11.10.2026): AiPlayerbot.AutoLearnQuestTeachEffects (default 0). A teaching RewSpell (LEARN_SPELL)
# of a quest with RewSpellCast = 0 teaches its learned spells instead of being stored; retired quests teach nothing.

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
    message(FATAL_ERROR "#578 quest teach: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#578 quest teach: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#578 quest teach: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#578 quest teach: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/AutoLearnSpellAction.cpp" action)
read_source("QuestTeachPolicy.h" policy)

require_text("${config_h}" "bool autoLearnQuestTeachEffects = false;" "switch default off")
require_text("${config_cpp}" "autoLearnQuestTeachEffects = config.GetBoolDefault(\"AiPlayerbot.AutoLearnQuestTeachEffects\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.AutoLearnQuestTeachEffects = 0" "documented")

# The RewSpell branch decides by the policy; the old priest list stays one of its inputs.
between("${action}" "void AutoLearnSpellAction::LearnQuestSpells(" "\n}\n" learn_fn)
between("${learn_fn}" "else if (quest->GetRewSpell() > 0)" "LearnSpell(quest->GetRewSpell(), out)" rew_branch)
require_text("${rew_branch}" "ai::quest_teach::ForRewSpell(sPlayerbotAIConfig.autoLearnQuestTeachEffects," "decision behind the switch")
require_text("${rew_branch}" "IsTeachingSpellListedAsSpell(quest->GetRewSpell())" "priest list kept")
require_text("${rew_branch}" "ai::quest_teach::HasLearnEffect(rewSpell->Effect[0], rewSpell->Effect[1], rewSpell->Effect[2])" "all three effects")
require_order("${rew_branch}" "if (mode == ai::quest_teach::RewSpellMode::Skip)\n                continue;" "if (mode == ai::quest_teach::RewSpellMode::TeachEffects)" "retired quests skipped first")
require_order("${rew_branch}" "if (mode == ai::quest_teach::RewSpellMode::TeachEffects)" "LearnSpellFromSpell(quest->GetRewSpell(), out)" "teaching spell -> its targets")
# The RewSpellCast branch is untouched (house pattern, 270 quests).
require_order("${learn_fn}" "if (quest->GetRewSpellCast() > 0 &&" "if (LearnSpellFromSpell(quest->GetRewSpellCast(), out))" "cast branch as before")

# Policy: switch off = old behaviour; retired ids and titles.
require_text("${policy}" "if (switchOn && retired)\n            return RewSpellMode::Skip;" "skip only with the switch")
require_text("${policy}" "if (listedTeachingSpell || (switchOn && hasLearnEffect))" "learn effects only with the switch")
require_text("${policy}" "questId == 60011 || questId == 60012 || questId == 80388" "known retired quests")
require_text("${policy}" "constexpr std::uint32_t LearnSpellEffect = 36;" "SPELL_EFFECT_LEARN_SPELL")

message(STATUS "quest_teach source contract passed")
