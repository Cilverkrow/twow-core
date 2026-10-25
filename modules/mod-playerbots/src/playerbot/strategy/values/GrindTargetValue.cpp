
#include "playerbot/playerbot.h"
#include "GrindTargetValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "AttackersValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "Formulas.h"
#include "playerbot/GrindCapPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "playerbot/QuestFirstOrderPolicy.h"   // twow-repo#601 decision order (Grind.QuestFirstOrder)

using namespace ai;

Unit* GrindTargetValue::Calculate()
{
    uint32 memberCount = 1;
    Group* group = bot->GetGroup();
    if (group)
        memberCount = group->GetMembersCount();

    Unit* target = NULL;
    uint32 assistCount = 0;
    // twow-repo#601 S2 R4: round 0's R4 skips; stays 0 when round 0 returns early (an attacker, no candidate at all).
    uint32 round0GapSkipped = 0;
    while (!target && assistCount < memberCount)
    {
        target = FindTargetForGrinding(assistCount++, round0GapSkipped);
    }

    // twow-repo#601 S2 R4 [GrindWindow], once per value pass after every assist round: r4_skips= round 0's skips in one relaxed
    // add (first assist round only, as before); r4_empty= (OB-10 18.10.2026, recheck: per value pass) one more when R4
    // skipped a candidate in round 0 and no round of the pass returned a target - a later round's target is no empty pass.
    if (round0GapSkipped)
    {
        PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::R4Skips), round0GapSkipped);
        if (ai::quest_grind_mode::GapR4EmptyPass(round0GapSkipped, target != nullptr))
            PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::R4Empty));
    }

    // #421 measuring point: grey targets engaged per bot and hour.
    Creature* creature = target ? dynamic_cast<Creature*>(target) : nullptr;
    // twow-repo#601 / #614: grey by level (QuestSearchPolicy.h GreyByLevel); XP::Gain is 0 for every untouched mob.
    if (creature && creature->GetObjectGuid() != lastGreyTarget &&
        ai::quest_search::GreyByLevel(creature->GetLevel(), MaNGOS::XP::GetGrayLevel(bot->GetLevel())))
    {
        lastGreyTarget = creature->GetObjectGuid();
        greyEngagements.Add(uint32(time(nullptr)));
    }
    uint32 const now = uint32(time(nullptr));
    if (greyEngagements.Due(now))
    {
        sLog.outBasic("[GreyTargets] bot=%u level=%u engaged=%u zone=%u window_s=%u",
            bot->GetGUIDLow(), bot->GetLevel(), greyEngagements.count, bot->GetZoneId(), now - greyEngagements.windowStart);
        greyEngagements.Reset();
    }

    return target;
}

Unit* GrindTargetValue::FindTargetForGrinding(int assistCount, uint32& round0GapSkipped)
{
    uint32 memberCount = 1;
    Group* group = bot->GetGroup();
    Player* master = GetMaster();

    if (master && (master == bot || master->GetMapId() != bot->GetMapId() || master->IsBeingTeleported() || !GetBotAI(master)))
        master = nullptr;

    // TEMP-DEBUG(grind-target): the existing "debug grind" strategy only reaches a
    // real player master via TellPlayer, which silently no-ops for random bots (no
    // master to tell) - mirror the same verdict to a file so masterless random bots
    // are traceable too, tagging the reaction (hostile/neutral/friendly) to see if
    // neutral starting-zone wildlife is being systematically skipped. Remove once
    // the "bots not picking fights" investigation concludes.
    auto logGrind = [&](Unit* u, const std::string& reason)
    {
        if (ai->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
            ai->TellPlayer(GetMaster(), chat->formatWorldobject(u) + " " + reason);

        if (sPlayerbotAIConfig.hasLog("grind_target.csv"))
        {
            std::ostringstream out;
            out << sPlayerbotAIConfig.GetTimestampUtcStr() << "+00,";
            out << bot->GetName() << ",\"" << u->GetName() << "\"," << u->GetEntry() << ",";
            out << (int)sServerFacade.IsHostileTo(bot, u) << "," << (int)sServerFacade.IsFriendlyTo(bot, u) << ",";
            out << bot->GetDistance(u) << ",\"" << reason << "\"";
            sPlayerbotAIConfig.log("grind_target.csv", out.str().c_str());
        }
    };

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;

        if (!bot->InBattleGround() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit)))
        {
            logGrind(unit, "(hostile) ignored (out of free range).");
            continue;
        }

        logGrind(unit, "(hostile) selected.");
        return unit;
    }

    std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >("possible targets");

    if (targets.empty())
        return NULL;

    float distance = 0;
    Unit* result = NULL;

    bool travelTargetWorking = AI_VALUE(bool, "travel target working");
    bool travelTargetTraveling = AI_VALUE(bool, "travel target traveling");
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    bool isGrindTravelDest = travelTarget && typeid(travelTarget->GetDestination()) == typeid(GrindTravelDestination);
    // twow-repo#601 S2 R5 (GrindMode 1 only, OB-10 rev. 2): the typeid above compares a TravelDestination pointer with the class
    // and is always false, so the 20 % "not needed for active quest" skip below also fires at a grind destination. Fixed for
    // the gap filler's population only (profile 1 after the A2 measurement); GrindMode 0 / 2 keep the old test.
    bool const gapGrind = ai->IsQuestGapGrind();
    if (gapGrind)
        isGrindTravelDest = travelTarget && dynamic_cast<GrindTravelDestination*>(travelTarget->GetDestination()) != nullptr;

    struct MemberInfo {
        Player* player;
        float x, y;
    };
    std::vector<MemberInfo> groupMembers;
    if (group)
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        groupMembers.reserve(groupSlot.size());
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = sObjectMgr.GetPlayer(itr->guid);
            if (member && sServerFacade.IsAlive(member))
                groupMembers.push_back({ member, member->GetPositionX(), member->GetPositionY() });
        }
    }

    std::unordered_map<uint32, bool> needForQuestCache;

    // #307: low-level roster bots grind at most GrindCap.LowLevelMargin levels
    // above themselves and skip creatures that killed them repeatedly.
    bool const rosterOnItsOwn = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
    // #422: a bot in a death series grinds nothing above its level.
    int const maxLevelsAbove = ai::death_series::GrindMargin(rosterOnItsOwn && ai->IsCautious(),
        grind_cap::MaxLevelsAbove(rosterOnItsOwn, bot->GetLevel(),
            sPlayerbotAIConfig.grindCapLowLevelBelow, sPlayerbotAIConfig.grindCapLowLevelMargin));
    time_t const now = time(nullptr);
    // twow-repo#601 GrindMode 2 (owner 17.10.2026): a quest-first roster bot on its own picks no "attack anything" target
    // that is neither attacking it or its group (the attackers above stay) nor needed for an active quest.
    bool const noGrind = ai->IsQuestNoGrind();
    uint32 noGrindSkipped = 0;
    uint32 noGrindEntry = 0;
    // twow-repo#601 decision order (AiPlayerbot.Grind.QuestFirstOrder, owner 20.10.2026): the same population picks only a
    // quest objective or a creature in its way (attackers above stay), so "attack anything" no longer wins over the quest work.
    // GrindMode 2 keeps its own, stricter rule (no path blockers) and GrindMode 1 its gap window (CLI-Deep review: the R4
    // window comes after this skip, so it must not run for the gap filler). Off: false, nothing else read.
    bool const questOrder = !noGrind && !gapGrind && ai->IsQuestFirstOrder();
    uint32 questOrderSkipped = 0;
    // twow-repo#601 S2 R4 (GrindMode 1, OB-10 rev. 2): the creature level window [max(gray + 1, L - 5), L + 1] of the gap
    // filler's population, once per pass (0 / 0 otherwise), and its skips for [GrindWindow] r4_skips=.
    uint32 const gapLow = gapGrind ? ai::quest_grind_mode::GapWindowLowLevel(bot->GetLevel(), MaNGOS::XP::GetGrayLevel(bot->GetLevel())) : 0;
    uint32 const gapHigh = gapGrind ? ai::quest_grind_mode::GapWindowHighLevel(bot->GetLevel()) : 0;
    uint32 gapSkipped = 0;

    for (std::list<ObjectGuid>::iterator tIter = targets.begin(); tIter != targets.end(); tIter++)
    {
        Unit* unit = ai->GetUnit(*tIter);
        if (!unit)
            continue;

        // Hotfix 8.2: a creature this bot could not reach is skipped for five minutes.
        if (ai->IsUnreachableTarget(unit->GetObjectGuid()))
            continue;

#ifdef MANGOSBOT_TWO 
        if (bot->GetMapId() == 609)
        {
            switch (unit->GetEntry())
            {
            case 28605: //Havenshire Stallion
            case 28606: //Havenshire Mare
            case 28607: //Havenshire Cotl
            case 28891: //Scarlet Miner
            case 28819: //Scarlet Miner
            case 28769: //Shadowy tormentor
            case 28768: //Dark Rider of Archeus
                continue;
            case 29080: //Scarlet Champion
            case 29029: //Scarlet Inquisitor
            case 29000: //Scarlet Commander Rodrick
            case 28940: //Scarlet Crusader
            case 28939: //Scarlet Preacher
            case 28936: //Scarlet Commander
            case 28898: //Scarlet Captain
            case 28896: //Scarlet Infantryman
            case 28895: //Scarlet Medic
            case 28892: //Scarlet Peasant
            case 28610: //Scarlet Marksman
                if (AI_VALUE2(bool, "need quest objective", "12680,0"))
                    continue;
                if (AI_VALUE2(bool, "need quest objective", "12701"))
                    continue;
                if (AI_VALUE2(bool, "need quest objective", "12727"))
                    continue;
                if (AI_VALUE2(bool, "need quest objective", "12754"))
                    continue;                
                break;
            case 29102: //HearthglenCrusader
            case 29103: //Tirisfal Crusader
            case 29104: //Scarlet Ballista
                if (AI_VALUE2(bool, "need quest objective", "12779"))
                {
                    if (AI_VALUE2(bool, "trigger active", "in vehicle") && bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
                        return unit;
                    continue;
                }
                break;
            case 28834: //Scarlet Fleet Defender
            case 28850: //Scarlet Land Canon
            case 28856: //Scarlet Fleet Guardian
                if (AI_VALUE2(bool, "need quest objective", "12701,0"))
                {
                    if (AI_VALUE2(bool, "trigger active", "in vehicle::Scarlet Cannon") && !urand(0, 5))
                        return unit;
                    continue;
                }
            }
        }
#endif

        if (abs(bot->GetPositionZ() - unit->GetPositionZ()) > sPlayerbotAIConfig.spellDistance)
        {
            logGrind(unit, "ignored (to far above/below).");
            continue;
        }

        if (!bot->InBattleGround() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit))) //Do not grind mobs far away from master.
        {
            logGrind(unit, "ignored (out of free range).");
            continue;
        }

        if (!bot->InBattleGround() && master &&
            (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
             ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
            sServerFacade.GetDistance2d(master, unit) > sPlayerbotAIConfig.proximityDistance)
        {
            logGrind(unit, "ignored (far from master).");
            continue;
        }

        if (!bot->InBattleGround() && (int)unit->GetLevel() - (int)bot->GetLevel() > maxLevelsAbove && !unit->GetObjectGuid().IsPlayer())
        {
            logGrind(unit, std::to_string((int)unit->GetLevel() - (int)bot->GetLevel()) + " levels above bot).");
            continue;
        }

        if (rosterOnItsOwn && sPlayerbotAIConfig.grindAvoidMaxDeaths && !unit->GetObjectGuid().IsPlayer() &&
            grind_cap::Avoids().IsAvoided(bot->GetGUIDLow(), unit->GetEntry(), uint32(now)))
        {
            logGrind(unit, "ignored (killed this bot repeatedly).");
            continue;
        }

        Creature* creature = dynamic_cast<Creature*>(unit);
        if (creature && creature->GetCreatureInfo() && creature->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, "can fight elite") &&
            !AI_VALUE2(bool, "trigger active", "in vehicle"))
        {
            logGrind(unit, "ignored (can not fight elites currently).");
            continue;
        }

        if (!AttackersValue::IsValid(unit, bot, nullptr, false, false))
        {
            logGrind(unit, "ignored (is pet or evading/unkillable).");
            continue;
        }

        if (!PossibleAttackTargetsValue::IsPossibleTarget(unit, bot, sPlayerbotAIConfig.sightDistance, false))
        {
            logGrind(unit, "ignored (tapped, cced or out of range).");
            continue;
        }

        if (creature && creature->IsCritter() && urand(0, 10))
        {
            logGrind(unit, "ignored (ignore critters).");
            continue;
        }

        float newdistance = sServerFacade.GetDistance2d(bot, unit);

        uint32 entry = unit->GetEntry();
        bool needForQuest = false;
        if (entry)
        {
            auto cacheIt = needForQuestCache.find(entry);
            if (cacheIt != needForQuestCache.end())
            {
                needForQuest = cacheIt->second;
            }
            else
            {
                needForQuest = AI_VALUE2(bool, "need for quest", std::to_string(entry));
                needForQuestCache[entry] = needForQuest;
            }
        }

        // twow-repo#601 GrindMode 2: only quest objectives stay (players and other creatures are skipped; a creature
        // that attacks is taken by the attacker loop above). Off: TargetAllowed(false, ...) is true, nothing changes.
        if (!ai::quest_grind_mode::TargetAllowed(noGrind, false, needForQuest))
        {
            if (!noGrindSkipped++)
                noGrindEntry = entry;
            logGrind(unit, "ignored (GrindMode 2: not needed for a quest).");
            continue;
        }

        // twow-repo#601 decision order (QuestFirstOrder): a quest objective or a creature in the bot's way (the "attack before
        // being attacked" rule of AttackAnythingAction) stays; the way test only for a candidate without quest need. Off:
        // questOrder is false, nothing is tested.
        if (questOrder && !ai::quest_first_order::TargetAllowed(true, needForQuest, !needForQuest &&
                ai::quest_first_order::BlocksPath(unit->IsPlayer(), bot->isInFront(unit, M_PI_F * 0.5f),
                    bot->GetDistance(unit) <= unit->GetAttackDistance(bot) * 1.5f, unit->CanAttackOnSight(bot), unit->GetLevel(), bot->GetLevel())))
        {
            ++questOrderSkipped;
            if (!assistCount)
                PlayerbotAI::NoteQuestOrder(uint8(ai::quest_first_order::Event::Skipped));
            logGrind(unit, "ignored (QuestFirstOrder: not needed for a quest, not in the way).");
            continue;
        }

        if (entry && !needForQuest)
        {
            // twow-repo#601 S2 R4 (GrindMode 1, OB-10 rev. 2): a plain grind candidate of the gap filler's population (no quest
            // need, not on the bot) needs its level inside the window; attackers (loop above), creatures on the bot and quest
            // mobs - grey ones too - stay. Before any urand of the candidate. GrindMode 0 / 2: gapGrind is false.
            if (gapGrind && ai::quest_grind_mode::GapTargetSkipped(true, false, unit->GetVictim() == bot, gapLow, gapHigh, unit->GetLevel()))
            {
                ++gapSkipped;
                logGrind(unit, "ignored (GrindMode 1: level outside the window).");
                continue;
            }
            // Was < 99 (99% skip chance for anything not tied to an active quest
            // objective) - with grind_target.csv logging live, this was confirmed as
            // the dominant reason bots weren't engaging: 23,863 "no grind target
            // found" against only 664 successful selections in one run, and the
            // rejected candidates were overwhelmingly real, appropriate-level,
            // killable mobs (Rat, Ragged Young Wolf, Duskbat, Defias Cutpurse, etc.),
            // not grey/no-XP ones - bots were holding out for quest-specific kills
            // almost exclusively instead of grinding on anything nearby. Lowered so
            // bots still lean toward quest-relevant kills when traveling somewhere
            // specific, but no longer skip everything else 99% of the time.
            if (urand(0, 100) < 20 && travelTargetWorking && !isGrindTravelDest)
            {
                logGrind(unit, "ignored (not needed for active quest).");
                continue;
            }
            // #421: a roster bot on its own never grinds grey creatures it does
            // not need (Deygo, L14, on level-1 wolves); others keep the old odds.
            // twow-repo#601 / #614 (AiPlayerbot.Grind.GreyByLevel, L1-b 19.10.2026): "no XP" = grey by the mob's level, no kill
            // reward or a zero XP template - for the #421 skip of roster bots on their own and the old 50-in-51 skip of every other
            // bot. Off: XP::Gain as before, which is 0 for every mob nobody has damaged yet, so almost every untouched non-quest
            // mob was skipped as "not xp".
            else if (creature && (sPlayerbotAIConfig.grindGreyByLevel ?
                    ai::quest_search::NoXpTarget(
                        ai::quest_search::GreyByLevel(creature->GetLevel(), MaNGOS::XP::GetGrayLevel(bot->GetLevel())),
                        creature->HasUnitState(UNIT_STAT_NO_KILL_REWARD), creature->GetCreatureInfo()->xp_multiplier == 0) :
                    !MaNGOS::XP::Gain(bot, creature)) &&
                (ai::quest_search::SkipGreyTarget(rosterOnItsOwn, true, false, creature->GetVictim() == bot) || urand(0, 50)))
            {
                logGrind(unit, "ignored (not xp and not needed for quest).");
                continue;
            }
            else if (urand(0, 100) < 75)
            {
                logGrind(unit, "increased distance (not needed for quest).");
                newdistance += 20;
            }
        }

        if (!bot->InBattleGround() && GetTargetingPlayerCount(unit) > assistCount)
        {
            logGrind(unit, std::to_string(GetTargetingPlayerCount(unit)) + " bots already targeting).");
            newdistance =+ GetTargetingPlayerCount(unit) * 5;
        }

        if (group)
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = sObjectMgr.GetPlayer(itr->guid);
                if (!member || !sServerFacade.IsAlive(member))
                    continue;

                newdistance = sServerFacade.GetDistance2d(member, unit);
                if (!result || newdistance < distance)
                {
                    distance = newdistance;
                    result = unit;
                }
            }
        }
        else
        {
            if (!result || (newdistance < distance && urand(0, abs(distance - newdistance)) > sPlayerbotAIConfig.sightDistance * 0.1))
            {
                distance = newdistance;
                result = unit;
            }
        }
    }

    if (result)
    {
        logGrind(result, "selected.");

        // twow-repo#601 decision order (QuestFirstOrder): what the pick is - quest objective or a creature in the way.
        if (questOrder)
        {
            auto const cached = needForQuestCache.find(result->GetEntry());
            PlayerbotAI::NoteQuestOrder(uint8(ai::quest_first_order::PickEvent(cached != needForQuestCache.end() && cached->second)));
        }
    }
    else if (sPlayerbotAIConfig.hasLog("grind_target.csv"))
    {
        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampUtcStr() << "+00,";
        out << bot->GetName() << ",\"none\",0,0,0,0,\"no grind target found\"";
        sPlayerbotAIConfig.log("grind_target.csv", out.str().c_str());
    }
    if (!result && ai->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
    {
        ai->TellPlayer(GetMaster(), "No grind target found.");
    }

    // twow-repo#601 GrindMode 2: nothing left after the skips above - the bot stands idle, [QuestIdle] (once per pass:
    // only the first assist round counts). At the destination = opportunistic, without a travel target the grind
    // strategy's "no target" kill. A bot still on its way is not counted (review fix): AttackAnythingAction asks for this
    // value before its "traveling -> no attack" test, so that bot is questing, not idle.
    if (!result && noGrindSkipped && !assistCount &&
        ai::quest_grind_mode::TargetNoteDue(travelTargetWorking, travelTargetTraveling))
        ai->NoteQuestIdle(uint8(ai::quest_grind_mode::TargetReason(travelTargetWorking)),
            ai::quest_grind_mode::TargetText("grind_target", noGrindEntry));

    // twow-repo#601 decision order (QuestFirstOrder): a pass left empty by the skips (round 0 only) - with a travel target the
    // bot goes on with its quest work; without one it has no quest work at all (a quest search defect, no_quest=).
    if (!result && questOrderSkipped && !assistCount)
    {
        ai::quest_first_order::Event const empty = ai::quest_first_order::EmptyEvent(AI_VALUE(bool, "travel target active"));
        PlayerbotAI::NoteQuestOrder(uint8(empty));
        // The per-bot episode line (OB-00 20.10.): no quest work at all, once per episode.
        if (empty == ai::quest_first_order::Event::NoQuest)
            ai->NoteQuestOrderEpisode(uint8(empty));
    }

    // twow-repo#601 S2 R4: round 0's skips go to the caller, which counts r4_skips= / r4_empty= once per value pass after its
    // assist rounds (recheck: a later round may still return a target). Nothing counted here.
    if (!assistCount)
        round0GapSkipped = gapSkipped;

    return result;
}

int GrindTargetValue::GetTargetingPlayerCount( Unit* unit )
{
    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    int count = 0;
    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player *member = sObjectMgr.GetPlayer(itr->guid);
        if( !member || !sServerFacade.IsAlive(member) || member == bot)
            continue;

        PlayerbotAI* ai = GetBotAI(member);
        if ((ai && *ai->GetAiObjectContext()->GetValue<Unit*>("current target") == unit) ||
            (!ai && member->GetSelectionGuid() == unit->GetObjectGuid()))
            count++;
    }

    return count;
}