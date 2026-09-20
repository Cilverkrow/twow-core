#include "playerbot/playerbot.h"
#include "RosterProfessionTrainerAction.h"

#include "playerbot/PersistentRosterProfessionTrainingPolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"

using namespace ai;

bool RosterProfessionTrainerAction::Execute(Event& event)
{
    uint32 const pair = GetPlannedPair();
    if (sPlayerbotAIConfig.professionTrainingTrace && pair)
        sLog.outDebug("[PersistentRosterProfessionTraining] trainer-attempt bot=%u level=%u pair=%u free=%u",
            bot->GetGUIDLow(), bot->GetLevel(), pair,
            sPlayerbotAIConfig.professionTrainingFreeForPersistentRoster ? 1u : 0u);

    return TrainerAction::Execute(event);
}

uint32 RosterProfessionTrainerAction::GetPlannedPair() const
{
    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return 0;

    uint32 const pair = sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow());
    return ai::profession::IsValid(pair) ? pair : 0;
}

uint32 RosterProfessionTrainerAction::GetTrainerSpellSkill(TrainerSpell const* spell) const
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
    return ai::profession_training::IsEligible(true, bot->GetLevel(),
        sPlayerbotAIConfig.professionTrainingStartLevel,
        static_cast<ai::profession::Pair>(pair));
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
