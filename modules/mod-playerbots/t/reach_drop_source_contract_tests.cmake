# twow-repo#630 follow-up (AiPlayerbot.Rotation.ReachDropEngagement, default 0; owner repro 25.10.2026 on C): the drop of
# Rotation.ReachPlayerMaster also clears every other value naming the target (a stealthed rogue's "attack target" kept it in
# combat, without follow), and a rogue following a real player outside combat, dungeons and battlegrounds drops stealth.

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
    message(FATAL_ERROR "#630 drop engagement: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#630 drop engagement: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/ReachTargetActions.h" reach)
read_source("strategy/rogue/RogueTriggers.h" rogue_triggers)
read_source("strategy/rogue/RogueStrategy.cpp" rogue_strategy)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" config_template)

# Key: mode 0/1/2, default 0, an unknown mode is off; documented.
require_text("${config_cpp}" "rotationReachDropEngagement = config.GetIntDefault(\"AiPlayerbot.Rotation.ReachDropEngagement\", 0);" "key, default 0")
require_text("${config_cpp}" "    if (rotationReachDropEngagement > 2)
        rotationReachDropEngagement = 0;" "unknown mode off")
require_text("${config_h}" "uint32 rotationReachDropEngagement = 0;" "member default 0")
require_text("${config_template}" "AiPlayerbot.Rotation.ReachDropEngagement = 0" "key documented")
# The rogue's unstealth has its own switch (pre-review: an A/B must tell the two effects apart).
require_text("${config_cpp}" "rotationFollowUnstealth = config.GetIntDefault(\"AiPlayerbot.Rotation.FollowUnstealth\", 0);" "FollowUnstealth key, default 0")
require_text("${config_cpp}" "    if (rotationFollowUnstealth > 2)
        rotationFollowUnstealth = 0;" "FollowUnstealth unknown mode off")
require_text("${config_h}" "uint32 rotationFollowUnstealth = 0;" "FollowUnstealth member default 0")
require_text("${config_template}" "AiPlayerbot.Rotation.FollowUnstealth = 0" "FollowUnstealth documented")

# The drop: #630's own clear first, then the whole engagement - only with a real player master and the mode.
require_order("${reach}" "                        SET_AI_VALUE(Unit*, \"current target\", nullptr);" "                    ai->DropUnreachableEngagement(target);" "#630 clear, then the rest")
require_text("${ai_cpp}" "    if (!target || !HasRealPlayerMaster() || !ai::cohort::Applies(sPlayerbotAIConfig.rotationReachDropEngagement, bot->GetGUIDLow()))
        return;" "real player master and the mode only")
require_text("${ai_cpp}" "    held.attackTarget = attackTarget->Get() == guid;
    if (held.attackTarget)
        attackTarget->Set(ObjectGuid());" "the attack command's target (counted as an attacker) goes")
require_text("${ai_cpp}" "        if (PullStrategy* pull = PullStrategy::Get(this))
            pull->OnPullEnded();
        pullTarget->Set(nullptr);" "a pull on the target ends")
require_text("${ai_cpp}" "    if (held.oldTarget)
        oldTarget->Set(nullptr);" "old target")
require_text("${ai_cpp}" "    if (held.selection)
        bot->SetSelectionGuid(ObjectGuid());" "selection")
require_text("${ai_cpp}" "    if (held.victim)
        bot->AttackStop();" "swing")
require_text("${ai_cpp}" "    aiObjectContext->GetValue<bool>(\"has attackers\")->Reset();" "cached attacker values reset")
require_text("${ai_cpp}" "[ReachDrop] bot=%u target_entry=%u attack=%u pull=%u old=%u selection=%u victim=%u cohort=%c" "log line")
string(FIND "${ai_cpp}" "if (lastUnreachableLog == uint32(time(nullptr)))" budget)
if(NOT budget EQUAL -1)
  message(FATAL_ERROR "#630 drop engagement: the [ReachDrop] line must log every drop (a watch counts them), not only within the [Unreachable] budget")
endif()

# The rogue: unstealth while following a real player outside combat, dungeons and battlegrounds; above follow with the mode.
require_order("${rogue_triggers}" "            if (ai->HasAura(\"stealth\", bot) && ai->HasRealPlayerMaster() &&
                ai::cohort::Applies(sPlayerbotAIConfig.rotationFollowUnstealth, bot->GetGUIDLow()))" "            if (ai->HasAura(\"stealth\", bot) && !bot->InBattleGround())" "the follow rule (stealth checked first) before the stock 10 yd rule")
require_text("${rogue_triggers}" "facts.masterMoving = master && sServerFacade.isMoving(master);" "the player's client movement flags, not IsStopped")
require_text("${rogue_triggers}" "sServerFacade.GetDistance2d(bot, master) > sPlayerbotAIConfig.followDistance + ai::reach_drop::CatchUpYards;" "catching up beyond the follow distance")
require_text("${rogue_triggers}" "                if (ai::reach_drop::UnstealthFollowing(facts))
                    return true;" "only a positive verdict returns; else the stock rule")
require_text("${rogue_triggers}" "facts.inDungeon = bot->GetMap()->IsDungeon();" "dungeons keep the stock rule")
require_text("${rogue_triggers}" "facts.nonCombat = ai->IsStateActive(BotState::BOT_STATE_NON_COMBAT);" "outside the combat engine only")
require_text("${rogue_strategy}" "new NextAction(\"unstealth\", dropStealth ? ACTION_HIGH + 1 : ACTION_NORMAL)" "unstealth above follow with the mode")
require_text("${rogue_strategy}" "bool const dropStealth = ai::cohort::Applies(sPlayerbotAIConfig.rotationFollowUnstealth, ai->GetBot()->GetGUIDLow());" "priority from FollowUnstealth")
