#include "playerbot/playerbot.h"
#include "RosterProfessionTrainerAction.h"

#include "playerbot/PersistentRosterProfessionTrainingPolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"

#include <ctime>
#include <sstream>

using namespace ai;

bool RosterProfessionTrainerAction::Execute(Event& event)
{
    uint32 const pair = GetPlannedPair();
    if (!pair)
    {
        TraceDecision("rejected", "missing_or_invalid_plan", nullptr);
        return false;
    }

    bool const completed = TrainerAction::Execute(event);
    TraceDecision(completed ? "accepted" : "rejected",
        completed ? "local_trainer_completed" : "no_eligible_local_trainer", nullptr);
    return completed;
}

uint32 RosterProfessionTrainerAction::PlannedPairFor(Player* bot)
{
    if (!bot || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return 0;

    uint32 const pair = sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow());
    return ai::profession::IsValid(pair) ? pair : 0;
}

uint32 RosterProfessionTrainerAction::GetPlannedPair() const
{
    return PlannedPairFor(bot);
}

uint32 RosterProfessionTrainerAction::GetTrainerSpellSkill(TrainerSpell const* spell)
{
    if (!spell)
        return 0;

    if (spell->reqSkill)
        return spell->reqSkill;

#ifdef MANGOSBOT_ZERO
    uint32 const learnedSpell = spell->learnedSpell;
#else
    uint32 const learnedSpell = spell->learnedSpell.empty() ? 0 : spell->learnedSpell.front();
#endif
    SpellEntry const* const info = learnedSpell ? sServerFacade.LookupSpellInfo(learnedSpell) : nullptr;
    if (!info)
        return 0;

    // This is the same skill identity used by TrainableSpellMapValue for an
    // initial profession spell with no reqSkill. The trainer remains the
    // authority for every rank, prerequisite and faction decision.
    return info->EffectMiscValue[1];
}

bool RosterProfessionTrainerAction::AllowsTradeSkillTrainer(Creature const* creature) const
{
    if (!creature || creature->GetCreatureInfo()->TrainerType != TRAINER_TYPE_TRADESKILLS)
        return false;

    uint32 const pair = GetPlannedPair();
    if (!ai::profession_training::IsEligible(true, bot->GetLevel(),
        sPlayerbotAIConfig.professionTrainingStartLevel,
        static_cast<ai::profession::Pair>(pair)))
    {
        TraceDecision("rejected", "plan_or_level_not_eligible", creature);
        return false;
    }

    if (!ai::profession_training::IsWithinLocalTrainerRadius(
            bot->GetDistance(creature), sPlayerbotAIConfig.professionTrainingLocalTrainerRadius))
    {
        TraceDecision("rejected", "trainer_outside_local_radius", creature);
        return false;
    }

    return true;
}

bool RosterProfessionTrainerAction::AllowsTrainerSpell(Creature const* creature, TrainerSpell const* spell) const
{
    if (!AllowsTradeSkillTrainer(creature))
        return false;

    // Do not turn this filter into a second trainer implementation. The
    // server's regular state check stays authoritative for level, ranks,
    // prerequisites, faction and already learned spells. TrainerAction::Iterate
    // performs the same check immediately before purchase as a defence in
    // depth against a state change between filtering and learning.
    uint32 const reqLevel = spell && spell->isProvidedReqLevel ? spell->reqLevel : 0;
    if (!spell || bot->GetTrainerSpellState(spell, reqLevel) != TRAINER_SPELL_GREEN)
        return false;

    uint32 const skill = GetTrainerSpellSkill(spell);
    return ai::profession_training::IsAllowedSkill(
        static_cast<ai::profession::Pair>(GetPlannedPair()), skill);
}

bool RosterProfessionTrainerAction::UsesFreeTraining() const
{
    return sPlayerbotAIConfig.professionTrainingFreeForPersistentRoster &&
        GetPlannedPair() != 0;
}

void RosterProfessionTrainerAction::TraceDecision(char const* state, char const* reason, Creature const* trainer) const
{
    traceGate.Emit(bot, GetPlannedPair(), "action", state, reason,
        trainer ? trainer->GetEntry() : 0, trainer ? bot->GetDistance(trainer) : -1.0f);
}

void RosterProfessionTraceGate::Emit(Player* bot, uint32 pair, char const* stage, char const* state,
    char const* reason, uint32 trainerEntry, float distance)
{
    if (!sPlayerbotAIConfig.professionTrainingTrace || !bot)
        return;

    std::ostringstream key;
    key << stage << ':' << state << ':' << reason << ':' << pair << ':' << trainerEntry;

    std::time_t const now = std::time(nullptr);
    auto const known = nextEmitAt.find(key.str());
    bool const knownKey = known != nextEmitAt.end();
    if (!ai::profession_training::ShouldEmitTrace(knownKey, now, knownKey ? known->second : 0))
        return;

    // Bounded: a bot meets only a handful of trainers within one cooldown.
    if (nextEmitAt.size() > 32)
        nextEmitAt.clear();
    nextEmitAt[key.str()] = now + sPlayerbotAIConfig.professionTrainingTraceCooldownSeconds;

    // BASIC level on purpose: live runs LogLevel 1, where DEBUG is dropped and
    // the acceptance measurement would be blind. The trace switch and the
    // cooldown keep this far below per-tick volume.
    sLog.outBasic("[PersistentRosterProfessionTraining] stage=%s state=%s reason=%s guid=%u level=%u pair=%u trainer=%u map=%u zone=%u distance=%.1f retry_seconds=%u",
        stage, state, reason, bot->GetGUIDLow(), bot->GetLevel(), pair, trainerEntry,
        bot->GetMapId(), bot->GetZoneId(), distance,
        sPlayerbotAIConfig.professionTrainingTraceCooldownSeconds);
}
