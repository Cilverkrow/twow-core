
#include "playerbot/playerbot.h"
#include "TrainerValues.h"
#include "SharedValueContext.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "playerbot/PersistentRosterProfessionTrainingPolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/RidingStagesBotPolicy.h"

using namespace ai;

namespace
{
    // twow-repo#295: the fields the trainable spell map below compares to merge equal
    // spells of different trainer lists, for the mount trainers filed by race.
    bool IsSameTrainerSpell(TrainerSpell const* a, TrainerSpell const* b)
    {
        return a->spell == b->spell && a->spellCost == b->spellCost && a->reqSkill == b->reqSkill &&
            a->reqSkillValue == b->reqSkillValue && a->reqLevel == b->reqLevel &&
#ifndef MANGOSBOT_TWO
            a->learnedSpell == b->learnedSpell &&
#else
            a->learnedSpell[0] == b->learnedSpell[0] &&
#endif
            a->conditionId == b->conditionId;
    }
}

trainableSpellMap* TrainableSpellMapValue::Calculate()
{
    trainableSpellMap* spellMap = new trainableSpellMap;

    //           template, trainer
    std::unordered_map <uint32, std::vector<CreatureInfo const*>> trainerTemplateIds;

    //Select all trainer lists and their trainers.
    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* creatureInfo = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!creatureInfo)
            continue;

        if (!creatureInfo->TrainerType && !creatureInfo->TrainerClass)
            continue;

        if(creatureInfo->TrainerTemplateId)
            trainerTemplateIds[creatureInfo->TrainerTemplateId].push_back(creatureInfo);
        else
            trainerTemplateIds[id].push_back(creatureInfo);
    }

    for (auto& [templateOrEntryId, trainers] : trainerTemplateIds)
    {
        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(templateOrEntryId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(templateOrEntryId);

        if (!trainer_spells)
            continue;

        CreatureInfo const* firstTrainer = trainers.front();

        TrainerType trainerType = (TrainerType)firstTrainer->TrainerType;

        uint32 spellRequirement;
        if (trainerType == TRAINER_TYPE_CLASS || trainerType == TRAINER_TYPE_PETS)
            spellRequirement = firstTrainer->TrainerClass;
        else if (trainerType == TRAINER_TYPE_MOUNTS)
            spellRequirement = firstTrainer->TrainerRace;

        for (auto& [id, trainerSpell] : trainer_spells->spellList)
        {
            const TrainerSpell* sameTrainerSpell = &trainerSpell;
            for (auto& [otherTrainerSpell, trainers] : (*spellMap)[trainerType][spellRequirement])
            {
                if (otherTrainerSpell->spell != trainerSpell.spell)
                    continue;

                if (otherTrainerSpell->spellCost != trainerSpell.spellCost)
                    continue;

                if (otherTrainerSpell->reqSkill != trainerSpell.reqSkill)
                    continue;

                if (otherTrainerSpell->reqSkillValue != trainerSpell.reqSkillValue)
                    continue;

                if (otherTrainerSpell->reqLevel != trainerSpell.reqLevel)
                    continue;

#ifndef MANGOSBOT_TWO
                if (otherTrainerSpell->learnedSpell != trainerSpell.learnedSpell)
#else
                if (otherTrainerSpell->learnedSpell[0] != trainerSpell.learnedSpell[0])
#endif
                    continue;

                if (otherTrainerSpell->conditionId != trainerSpell.conditionId)
                    continue;

                sameTrainerSpell = otherTrainerSpell;
                break;
            }

            if (trainerType == TRAINER_TYPE_TRADESKILLS)
            {
                if (trainerSpell.reqSkill)
                    spellRequirement = trainerSpell.reqSkill;
                else
                {
                    // exist, already checked at loading
#ifdef MANGOSBOT_ZERO
                    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(trainerSpell.learnedSpell);
#else
                    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(trainerSpell.learnedSpell[0]);
#endif

                    spellRequirement = spell->EffectMiscValue[1];
                }
            }

            for (auto& trainer : trainers)
                (*spellMap)[trainerType][spellRequirement][sameTrainerSpell].push_back(trainer->Entry);
        }
    }

    // twow-repo#295: all racial riding trainers share trainer template 1, filed above under
    // the race of its first trainer (3690, Tauren). With riding stages every mount trainer
    // goes under its own race, so each race finds its riding trainer and its train cost.
    if (sWorld.getConfig(CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED))
    {
        auto mounts = spellMap->find(TRAINER_TYPE_MOUNTS);
        if (mounts != spellMap->end())
            mounts->second = riding_stages::MountTrainersByRace(mounts->second,
                [](int32 entry, uint32 filedRace)
                {
                    CreatureInfo const* trainer = sCreatureStorage.LookupEntry<CreatureInfo>(uint32(entry));
                    return trainer ? uint32(trainer->TrainerRace) : filedRace;
                },
                IsSameTrainerSpell);
    }

    return spellMap;
}

std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->getClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                uint32 reqLevel = 0;

                reqLevel = trainerSpell->isProvidedReqLevel ? trainerSpell->reqLevel : std::max(reqLevel, trainerSpell->reqLevel);
                TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell, reqLevel);
                if (state != TRAINER_SPELL_GREEN)
                    continue;

                bool const persistentRosterBot =
                    sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow());
                uint32 const pair = persistentRosterBot
                    ? sRandomPlayerbotMgr.GetProfessionPair(bot->GetGUIDLow())
                    : 0;
                bool const allowedRosterProfession = trainerType == TRAINER_TYPE_TRADESKILLS &&
                    profession_training::IsEligibleProfessionTraining(
                        persistentRosterBot, bot->GetLevel(),
                        sPlayerbotAIConfig.professionTrainingStartLevel,
                        static_cast<profession::Pair>(pair), requirement);

                // A registered roster bot has exactly one persisted primary
                // pair. Filter every tradeskill rank here, not only the
                // level-three starting spell, so an unplanned trainer cannot
                // become a travel destination later.
                if (trainerType == TRAINER_TYPE_TRADESKILLS &&
                    persistentRosterBot && !allowedRosterProfession)
                    continue;

                // Initial professions stay unavailable below level ten for
                // generic bots. Persistent roster bots may instead start the
                // one stored profession plan from their configured start
                // level, but never an unplanned primary profession.
#ifdef MANGOSBOT_ZERO
                bool const isInitialProfession = sSpellMgr.IsProfessionSpell(trainerSpell->learnedSpell) &&
                    sSpellMgr.GetSpellRank(trainerSpell->learnedSpell) == 1;
#else
                bool const isInitialProfession = sSpellMgr.IsProfessionSpell(trainerSpell->learnedSpell[0]) &&
                    sSpellMgr.GetSpellRank(trainerSpell->learnedSpell[0]) == 1;
#endif
                if (bot->GetLevel() < 10 && isInitialProfession)
                {
                    if (!allowedRosterProfession)
                        continue;
                }

                trainableSpells.push_back(trainerSpell);
            }
        }
    }   

    return trainableSpells;
}

std::string TrainableSpellsValue::Format()
{
    std::vector<std::string> vec;  
    for (auto t : value) {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(t->spell);
        if (!spell)
            continue;
        vec.push_back(chat->formatSpell(spell));
    } 
    
    return sPlayerbotHelpMgr.makeList(vec, "[<part>]");
}

std::vector<int32> AvailableTrainersValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells = AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier());;
    std::vector<int32> retTrainers;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->getClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                if (std::find(trainableSpells.begin(), trainableSpells.end(), trainerSpell) == trainableSpells.end())
                    continue;

                for (auto& trainer : trainers)
                {
                    if(std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                        retTrainers.push_back(trainer);
                }
            }
        }
    }

    return retTrainers;
}

uint32 TrainCostValue::Calculate()
{
    uint32 TotalCost = 0;

    for (auto& spells : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier()))
        TotalCost += spells->spellCost;
   
    return TotalCost;
}
