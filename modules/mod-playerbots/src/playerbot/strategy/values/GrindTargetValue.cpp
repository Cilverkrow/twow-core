
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
#include "playerbot/GrindNeighbourPolicy.h"   // twow-repo#602 #2 (Grind.NeighbourAware)
#include "playerbot/SafePullPolicy.h"   // twow-repo#656 (Grind.SafePull)
#include "playerbot/CohortPolicy.h"
#include "playerbot/QuestLoopPolicy.h"   // twow-repo#601 turn_in hold (QuestLoop.RelationWorkNoGrind)
#include "playerbot/GrindIdlePolicy.h"   // twow-repo#601 (ActivityTrace.GrindIdleDetail)
#include "playerbot/TravelMgr.h"
#include "CreatureGroups.h"
#include "World.h"

#include <chrono>

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

    if (master && (master == bot || !SameMapObject(bot, master) || master->IsBeingTeleported() || !GetBotAI(master)))
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
            out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
            out << bot->GetName() << ",\"" << u->GetName() << "\"," << u->GetEntry() << ",";
            out << (int)sServerFacade.IsHostileTo(bot, u) << "," << (int)sServerFacade.IsFriendlyTo(bot, u) << ",";
            out << bot->GetDistance(u) << ",\"" << reason << "\"";
            sPlayerbotAIConfig.log("grind_target.csv", out.str().c_str());
        }
    };

    // twow-repo#601 (AiPlayerbot.ActivityTrace.GrindIdleDetail, OB-00 20.11.2026): round 0 of the pass (the bot's own pick) counts
    // its candidates and why each was refused into the bot's tally (GrindIdlePolicy.h), read by the [GrindIdleDetail] start line.
    // Counters only, no decision reads them. Off (or an assist round): no tally, nothing is written.
    ai::grind_idle::PassTally* const tally = !assistCount && sPlayerbotAIConfig.activityTrace &&
        sPlayerbotAIConfig.activityTraceGrindIdleDetail ? &ai->GrindPassTally() : nullptr;
    if (tally)
        tally->Start(uint32(time(nullptr)));
    auto const skip = [tally](ai::grind_idle::Skip why)
    {
        if (tally)
            tally->Note(why);
    };

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;

        // twow-repo#703 4b (Rotation.UnreachableLock): a locked unit is skipped unless the bot may fight back (MayDefend).
        if (ai->UnreachableLockBlocks(unit, ai::unreachable_giveup::Path::GrindAttacker))
        {
            logGrind(unit, "(hostile) ignored (unreachable lock).");
            continue;
        }

        if (!bot->InBattleGround() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit)))
        {
            logGrind(unit, "(hostile) ignored (out of free range).");
            continue;
        }

        if (tally)
            tally->picked = true;
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
    // WS30-ELITE-QUEST-01 (QuestElite.Defer, default 0): both elite entry sets once per scan, only for a bot the rule judges (key 0 /
    // other bots: empty, nothing read). deferred: E2 (elite targets of deferred quests count as not needed); pursued: the rule-1b
    // solo bot may pick the elite targets of a quest the rule lets it pursue (the old "can fight elite" skip is group only).
    std::vector<int32> deferredElite;
    std::vector<int32> pursuedElite;
    if (sPlayerbotAIConfig.questEliteDefer && ai->EliteRuleJudges())
    {
        deferredElite = AI_VALUE(std::vector<int32>, "deferred elite entries");
        pursuedElite = AI_VALUE(std::vector<int32>, "pursued elite entries");
    }

    // #307: low-level roster bots grind at most GrindCap.LowLevelMargin levels
    // above themselves and skip creatures that killed them repeatedly.
    bool const rosterOnItsOwn = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
    // twow-repo#601 turn_in hold (AiPlayerbot.QuestLoop.RelationWorkNoGrind, CLI-Deep 28.10.2026): working at a quest giver / taker
    // whose turn-in / accept is still open, a roster bot on its own (no group) skips candidates without quest need, so "attack
    // anything" no longer wins over the hand-in (41747 / 62634). Attackers were taken above. Off: false, nothing else read.
    bool const relationWorkOpen = ai::cohort::Applies(sPlayerbotAIConfig.questLoopRelationWorkNoGrind, bot->GetGUIDLow()) &&
        travelTargetWorking && travelTarget && !group && rosterOnItsOwn && QuestRelationOpen(bot, travelTarget->GetDestination());
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
    // twow-repo#602 #2 (AiPlayerbot.Grind.NeighbourAware, adds analysis 25.10.): the candidates of the solo replace decision below, kept
    // for the neighbour score after the loop (GrindNeighbourPolicy.h). Solo free bots out of a battleground without a real master, round
    // 0 only (a grouped bot overwrites newdistance with each member's distance). Off: nothing is built.
    uint32 const neighbourMode = sPlayerbotAIConfig.grindNeighbourAware;
    bool const neighbourScored = ai::grind_neighbour::Computes(neighbourMode) && !assistCount && !group && !bot->InBattleGround() &&
        !ai->HasRealPlayerMaster() && sRandomPlayerbotMgr.IsFreeBot(bot);
    std::vector<NeighbourCandidate> neighbourCandidates;
    // twow-repo#656 (AiPlayerbot.Grind.SafePull, #653 lever 1): the same population and candidates, risk-ordered after the loop
    // (SafePullPolicy.h). Off: nothing is built.
    uint32 const safeMode = sPlayerbotAIConfig.grindSafePull;
    bool const safeScored = ai::safe_pull::Computes(safeMode) && !assistCount && !group && !bot->InBattleGround() &&
        !ai->HasRealPlayerMaster() && sRandomPlayerbotMgr.IsFreeBot(bot);
    std::vector<NeighbourCandidate> safeCandidates;
    // Lever 2 (AiPlayerbot.GrindAvoid.Lever, CLI-ROT 22.11.2026, default 0): the second GrindAvoid threshold (GrindCapPolicy.h) for roster
    // bots on their own, round 0. Applied: a lever-avoided candidate is skipped; shadow: counted and kept. Off: nothing is judged.
    uint32 const leverMode = sPlayerbotAIConfig.grindAvoidLever;
    bool const leverScored = rosterOnItsOwn && !assistCount && !group &&
        grind_cap::LeverComputes(leverMode, sPlayerbotAIConfig.grindAvoidLeverMaxDeaths);
    bool const leverApplies = leverScored && grind_cap::LeverApplies(leverMode, bot->GetGUIDLow());
    uint32 leverSkipped = 0, leverWould = 0, leverReached = 0, leverWouldReached = 0;

    for (std::list<ObjectGuid>::iterator tIter = targets.begin(); tIter != targets.end(); tIter++)
    {
        Unit* unit = ai->GetUnit(*tIter);
        if (!unit)
            continue;
        if (tally && tally->candidates < 0xFFFF)
            ++tally->candidates;

        // Hotfix 8.2: a creature this bot could not reach is skipped for five minutes.
        if (ai->IsUnreachableTarget(unit->GetObjectGuid()))
        {
            skip(ai::grind_idle::Skip::Unreachable);
            continue;
        }

        // twow-repo#703 4b (Rotation.UnreachableLock): also the same-entry siblings near the give-up spot, for 1800 s.
        if (ai->UnreachableLockBlocks(unit, ai::unreachable_giveup::Path::GrindCandidate))
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
            skip(ai::grind_idle::Skip::Height);
            logGrind(unit, "ignored (to far above/below).");
            continue;
        }

        if (!bot->InBattleGround() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit))) //Do not grind mobs far away from master.
        {
            skip(ai::grind_idle::Skip::FreeRange);
            logGrind(unit, "ignored (out of free range).");
            continue;
        }

        if (!bot->InBattleGround() && master &&
            (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
             ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
            sServerFacade.GetDistance2d(master, unit) > sPlayerbotAIConfig.proximityDistance)
        {
            skip(ai::grind_idle::Skip::FreeRange);
            logGrind(unit, "ignored (far from master).");
            continue;
        }

        if (!bot->InBattleGround() && (int)unit->GetLevel() - (int)bot->GetLevel() > maxLevelsAbove && !unit->GetObjectGuid().IsPlayer())
        {
            skip(ai::grind_idle::Skip::LevelAbove);
            logGrind(unit, std::to_string((int)unit->GetLevel() - (int)bot->GetLevel()) + " levels above bot).");
            continue;
        }

        if (rosterOnItsOwn && sPlayerbotAIConfig.grindAvoidMaxDeaths && !unit->GetObjectGuid().IsPlayer() &&
            grind_cap::Avoids().IsAvoided(bot->GetGUIDLow(), unit->GetEntry(), uint32(now)))
        {
            skip(ai::grind_idle::Skip::Avoided);
            logGrind(unit, "ignored (killed this bot repeatedly).");
            continue;
        }

        // Lever 2: the second threshold, only for a candidate the base rule above kept.
        bool leverWouldThis = false;
        if (leverScored && !unit->GetObjectGuid().IsPlayer())
        {
            grind_cap::LeverVerdict const lever = grind_cap::JudgeLever(false,
                grind_cap::LeverAvoids().IsAvoided(bot->GetGUIDLow(), unit->GetEntry(), uint32(now)), leverApplies);
            if (lever == grind_cap::LeverVerdict::Skip)
            {
                ++leverSkipped;
                skip(ai::grind_idle::Skip::Avoided);
                logGrind(unit, "ignored (killed this bot, lever).");
                continue;
            }
            if (lever == grind_cap::LeverVerdict::WouldSkip)
            {
                ++leverWould;
                leverWouldThis = true;
            }
        }

        Creature* creature = dynamic_cast<Creature*>(unit);
        if (creature && creature->GetCreatureInfo() && creature->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, "can fight elite") &&
            !std::binary_search(pursuedElite.begin(), pursuedElite.end(), int32(creature->GetEntry())) &&   // WS30-ELITE-QUEST-01 rule 1b
            !AI_VALUE2(bool, "trigger active", "in vehicle"))
        {
            skip(ai::grind_idle::Skip::Elite);
            logGrind(unit, "ignored (can not fight elites currently).");
            continue;
        }

        if (!AttackersValue::IsValid(unit, bot, nullptr, false, false))
        {
            skip(ai::grind_idle::Skip::Invalid);
            logGrind(unit, "ignored (is pet or evading/unkillable).");
            continue;
        }

        if (!PossibleAttackTargetsValue::IsPossibleTarget(unit, bot, sPlayerbotAIConfig.sightDistance, false))
        {
            skip(ai::grind_idle::Skip::Tapped);
            logGrind(unit, "ignored (tapped, cced or out of range).");
            continue;
        }

        if (creature && creature->IsCritter() && urand(0, 10))
        {
            skip(ai::grind_idle::Skip::Critter);
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
            // WS30-ELITE-QUEST-01 (QuestElite.Defer, OB-10 E2): an elite target of a deferred quest that no other quest needs counts
            // as not needed for the target choice (GrindMode 2 / QuestFirstOrder filters and the plain-grind tests below). Attackers
            // are taken by the attacker loop above, unchanged. Empty value for every bot the rule does not judge.
            if (needForQuest && !deferredElite.empty())
            {
                if (std::binary_search(deferredElite.begin(), deferredElite.end(), int32(entry)))
                {
                    needForQuest = false;
                    PlayerbotAI::NoteEliteQuest(uint8(ai::elite_quest::Event::TargetSkipped), bot->GetGUIDLow());
                }
            }
        }

        // twow-repo#601 GrindMode 2: only quest objectives stay (players and other creatures are skipped; a creature
        // that attacks is taken by the attacker loop above). Off: TargetAllowed(false, ...) is true, nothing changes.
        if (!ai::quest_grind_mode::TargetAllowed(noGrind, false, needForQuest))
        {
            if (!noGrindSkipped++)
                noGrindEntry = entry;
            skip(ai::grind_idle::Skip::Rule);
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
            skip(ai::grind_idle::Skip::Rule);
            logGrind(unit, "ignored (QuestFirstOrder: not needed for a quest, not in the way).");
            continue;
        }

        if (entry && !needForQuest)
        {
            if (ai::quest_loop::RelationWorkGrindSkipped(relationWorkOpen, needForQuest))
            {
                skip(ai::grind_idle::Skip::Rule);
                logGrind(unit, "ignored (QuestLoop.RelationWorkNoGrind: the hand-in / accept at the NPC first).");
                continue;
            }
            // twow-repo#601 S2 R4 (GrindMode 1, OB-10 rev. 2): a plain grind candidate of the gap filler's population (no quest
            // need, not on the bot) needs its level inside the window; attackers (loop above), creatures on the bot and quest
            // mobs - grey ones too - stay. Before any urand of the candidate. GrindMode 0 / 2: gapGrind is false.
            if (gapGrind && ai::quest_grind_mode::GapTargetSkipped(true, false, unit->GetVictim() == bot, gapLow, gapHigh, unit->GetLevel()))
            {
                ++gapSkipped;
                skip(ai::grind_idle::Skip::Rule);
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
                skip(ai::grind_idle::Skip::NotNeeded);
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
                skip(ai::grind_idle::Skip::Grey);
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
            if (tally && tally->crowded < 0xFFFF)
                ++tally->crowded;
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
            // Lever 2: candidates that reach the solo replace decision, and those among them the lever would skip (would_empty).
            if (leverScored)
            {
                ++leverReached;
                if (leverWouldThis)
                    ++leverWouldReached;
            }
            // twow-repo#656: the same record for the risk-ordered pick (Grind.SafePull).
            if (safeScored)
                safeCandidates.push_back({ unit, newdistance, sServerFacade.GetDistance2d(bot, unit) });
            // twow-repo#602 #2: every candidate that reaches the solo replace decision, with its score distance (quest +20 included).
            if (neighbourScored)
                neighbourCandidates.push_back({ unit, newdistance, sServerFacade.GetDistance2d(bot, unit) });
            if (!result || (newdistance < distance && urand(0, abs(distance - newdistance)) > sPlayerbotAIConfig.sightDistance * 0.1))
            {
                distance = newdistance;
                result = unit;
            }
        }
    }

    // twow-repo#656: the risk-ordered pick (applied: the lowest-risk candidate, or no target while the bot is not ready for a risky
    // one; shadow: only counted). Before the neighbour rule, on the same candidates (a null pick stays null there).
    if (safeScored)
        result = SafePick(result, safeCandidates, targets, ai::safe_pull::Applies(safeMode, bot->GetGUIDLow()));

    // twow-repo#602 #2: the distance pick above is final unless the neighbour rule finds a candidate with fewer neighbours and a lower
    // score (applied: replaced; shadow: only counted). Demote only - a pass with a pick keeps a pick.
    if (neighbourScored)
        result = NeighbourPick(result, neighbourCandidates, targets, ai::grind_neighbour::Applies(neighbourMode, bot->GetGUIDLow()));

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
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
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

    // Lever 2: the pass's counters and the last-pick mark for the [Lever2] death line, on the final pick (after the safe-pull and
    // neighbour rules). would_change = a shadow pick the lever avoids (the base rule already removed its own); would_empty = every
    // candidate at the decision was lever-avoided (shadow) or the lever emptied the pass (applied).
    if (leverScored)
    {
        bool const pickWould = !leverApplies && result && !result->GetObjectGuid().IsPlayer() &&
            grind_cap::LeverAvoids().IsAvoided(bot->GetGUIDLow(), result->GetEntry(), uint32(now));
        bool const empty = leverApplies ? (leverSkipped && !leverReached) : (leverReached && leverWouldReached == leverReached);
        grind_cap::LeverCounters& lc = grind_cap::leverCounters[ai::cohort::InB(bot->GetGUIDLow()) ? 1 : 0];
        lc.passes.fetch_add(1, std::memory_order_relaxed);
        if (result)
            lc.picks.fetch_add(1, std::memory_order_relaxed);
        if (leverSkipped)
        {
            lc.filtered.fetch_add(leverSkipped, std::memory_order_relaxed);
            lc.changed.fetch_add(1, std::memory_order_relaxed);
        }
        if (leverWould)
            lc.wouldFilter.fetch_add(leverWould, std::memory_order_relaxed);
        if (pickWould)
            lc.wouldChange.fetch_add(1, std::memory_order_relaxed);
        if (empty)
            lc.wouldEmpty.fetch_add(1, std::memory_order_relaxed);
        if (result)
        {
            ai->lever2GrindAt = uint32(now);
            ai->lever2GrindWould = pickWould || (leverApplies && leverSkipped);
        }
    }

    // twow-repo#601 S2 R4: round 0's skips go to the caller, which counts r4_skips= / r4_empty= once per value pass after its
    // assist rounds (recheck: a later round may still return a target). Nothing counted here.
    if (!assistCount)
        round0GapSkipped = gapSkipped;
    // twow-repo#601 (ActivityTrace.GrindIdleDetail): the pass's pick, after the safe-pull and neighbour rules (round 0 only).
    if (tally)
        tally->picked = result != nullptr;

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
// twow-repo#602 #2 (AiPlayerbot.Grind.NeighbourAware): W of every candidate = the weighted creatures of this pass's "possible targets"
// list (the list stage 2 already holds - no new grid search, no raycast) that would join a fight with it: same-template assist,
// on sight, an AGGRO_TOGETHER group, or the call for help along its run to a ranged bot (GrindNeighbourPolicy.h). The distance pick
// is replaced by the candidate with the lowest score among those with a strictly lower W and a strictly lower score, outside the
// snapshot edge and inside the level guard; applies = false only counts it (shadow). Every other case returns the distance pick.
Unit* GrindTargetValue::NeighbourPick(Unit* pick, std::vector<NeighbourCandidate> const& candidates, std::list<ObjectGuid> const& targets, bool applies)
{
    using namespace ai::grind_neighbour;
    if (!pick || candidates.empty())
        return pick;

    auto const started = std::chrono::steady_clock::now();
    uint32 const cohort = ai::cohort::InB(bot->GetGUIDLow()) ? 1 : 0;
    uint32 const cls = bot->getClass() < MaxClass ? bot->getClass() : 0;
    Counters& count = counters[cohort];
    count.passes.fetch_add(1, std::memory_order_relaxed);
    count.candidates.fetch_add(candidates.size(), std::memory_order_relaxed);

    struct Entry
    {
        Creature* creature;
        uint32 faction;
        CreatureGroup* group;    // only an AGGRO_TOGETHER group
        float attackDistance;    // 0 = does not attack on sight
        bool assists;            // may answer CallAssistance / CallForHelp against the bot
        uint32 halfW;
    };
    std::vector<Entry> entries;
    entries.reserve(std::min<size_t>(targets.size(), MaxEntries));
    uint32 const botLevel = bot->GetLevel();
    uint32 const grayLevel = MaNGOS::XP::GetGrayLevel(botLevel);
    for (ObjectGuid const& guid : targets)
    {
        if (entries.size() >= MaxEntries)
        {
            count.capped.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        Creature* creature = ai->GetCreature(guid);
        // CanAssistTo / AggressorAI ignore a creature already in combat; owned, critter, totem and evading ones never join.
        if (!creature || !sServerFacade.IsAlive(creature) || creature->IsInCombat() || creature->GetCharmerOrOwnerGuid() ||
            creature->IsPet() || creature->IsTotem() || creature->IsCritter() || creature->IsInEvadeMode())
            continue;
        CreatureGroup* group = creature->GetCreatureGroup();
        Entry entry;
        entry.creature = creature;
        entry.faction = creature->GetFactionTemplateId();
        entry.group = group && group->HasGroupFlag(OPTION_AGGRO_TOGETHER) ? group : nullptr;
        entry.attackDistance = creature->CanAttackOnSight(bot) && creature->CanInitiateAttack() ? creature->GetAttackDistance(bot) : 0.0f;
        entry.assists = creature->IsHostileTo(bot) && !creature->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_ASSIST) &&
            !creature->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO);
        CreatureInfo const* info = creature->GetCreatureInfo();
        entry.halfW = HalfWeight(creature->GetLevel() <= grayLevel,
            (info && info->Rank > CREATURE_ELITE_NORMAL) || creature->GetLevel() >= botLevel + 3);
        if (!entry.group && entry.attackDistance <= 0.0f && !entry.assists)
            continue;
        entries.push_back(entry);
    }

    float const assistRadius = sWorld.getConfig(CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS);
    float const pullRange = ai->IsRanged(bot) ? sPlayerbotAIConfig.spellDistance : 0.0f;
    std::vector<uint32> halfWs(candidates.size(), 0);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        Creature* x = dynamic_cast<Creature*>(candidates[i].unit);
        if (!x)
            continue;   // a player candidate: no camp
        CreatureGroup* xGroup = x->GetCreatureGroup();
        if (xGroup && !xGroup->HasGroupFlag(OPTION_AGGRO_TOGETHER))
            xGroup = nullptr;
        CreatureInfo const* xInfo = x->GetCreatureInfo();
        float const callForHelp = xInfo ? xInfo->call_for_help_range : 0.0f;
        float const botDistance = candidates[i].botDistance;
        float ux = 0.0f, uy = 0.0f;
        if (botDistance > 0.1f)
        {
            ux = (bot->GetPositionX() - x->GetPositionX()) / botDistance;
            uy = (bot->GetPositionY() - x->GetPositionY()) / botDistance;
        }
        uint32 w = 0;
        for (Entry const& e : entries)
        {
            if (e.creature == x)
                continue;
            float const dx = e.creature->GetPositionX() - x->GetPositionX();
            float const dy = e.creature->GetPositionY() - x->GetPositionY();
            float const dz = e.creature->GetPositionZ() - x->GetPositionZ();
            float const distSq = dx * dx + dy * dy + dz * dz;
            std::atomic<std::uint64_t>* kind = nullptr;
            if (xGroup && e.group == xGroup)
                kind = &count.nbGroup;
            else if (e.assists && e.faction == x->GetFactionTemplateId() && Within(distSq, assistRadius + AssistPad))
                kind = &count.nbAssist;
            else if (e.attackDistance > 0.0f && Within(distSq, e.attackDistance + SightPad))
                kind = &count.nbSight;
            else if (e.assists && callForHelp > 0.0f && e.creature->IsFriendlyTo(x))
            {
                float const chaseSq = ChaseDistSq(dx, dy, ux, uy, botDistance, pullRange);
                if (chaseSq >= 0.0f && Within(chaseSq, callForHelp + CallForHelpPad))
                    kind = &count.nbCallForHelp;
            }
            if (!kind)
                continue;
            kind->fetch_add(1, std::memory_order_relaxed);
            w += e.halfW;
        }
        halfWs[i] = std::min(w, HalfCap);
    }

    // The distance pick is one of the candidates (the solo replace decision only takes recorded ones).
    size_t p = candidates.size();
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].unit == pick)
            p = i;
    Unit* best = nullptr;
    if (p < candidates.size())
    {
        count.picks.fetch_add(1, std::memory_order_relaxed);
        classPicks[cohort][cls].fetch_add(1, std::memory_order_relaxed);
        count.pickHalfW.fetch_add(halfWs[p], std::memory_order_relaxed);
        float const sight = sPlayerbotAIConfig.sightDistance;
        if (Edge(candidates[p].botDistance, sight))
            count.edgePick.fetch_add(1, std::memory_order_relaxed);
        else
        {
            Creature* pickCreature = dynamic_cast<Creature*>(pick);
            bool const pickElite = pickCreature && pickCreature->GetCreatureInfo() &&
                pickCreature->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL;
            size_t b = candidates.size();
            bool guarded = false;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (i == p || !Replaces(candidates[i].distance, halfWs[i], candidates[p].distance, halfWs[p]))
                    continue;
                if (Edge(candidates[i].botDistance, sight))
                {
                    count.edgeSkipped.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                Creature* c = dynamic_cast<Creature*>(candidates[i].unit);
                bool const elite = c && c->GetCreatureInfo() && c->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL;
                if (!GuardOk(int32(candidates[i].unit->GetLevel()), elite, int32(pick->GetLevel()), pickElite))
                {
                    guarded = true;
                    continue;
                }
                if (b == candidates.size() ||
                    candidates[i].distance + Penalty(halfWs[i]) < candidates[b].distance + Penalty(halfWs[b]))
                    b = i;
            }
            if (guarded)
                count.guarded.fetch_add(1, std::memory_order_relaxed);
            if (b < candidates.size())
            {
                best = candidates[b].unit;
                count.newHalfW.fetch_add(halfWs[b], std::memory_order_relaxed);
                count.extraYd.fetch_add(uint64(std::max(0.0f, candidates[b].botDistance - candidates[p].botDistance) + 0.5f),
                    std::memory_order_relaxed);
                (applies ? count.changed : count.wouldChange).fetch_add(1, std::memory_order_relaxed);
                (applies ? classChanged : classWould)[cohort][cls].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Churn: the pick changes between passes while the last one is still a candidate (the value is recomputed every 2 s until
    // engaged); the distance pick's own churn is the baseline.
    Unit* const rulePick = best ? best : pick;
    auto isCandidate = [&](ObjectGuid const& guid)
    {
        return !guid.IsEmpty() && std::any_of(candidates.begin(), candidates.end(),
            [&](NeighbourCandidate const& c) { return c.unit->GetObjectGuid() == guid; });
    };
    if (pick->GetObjectGuid() != lastDistancePick && isCandidate(lastDistancePick))
        count.churnDistance.fetch_add(1, std::memory_order_relaxed);
    if (rulePick->GetObjectGuid() != lastNeighbourPick && isCandidate(lastNeighbourPick))
        count.churn.fetch_add(1, std::memory_order_relaxed);
    lastDistancePick = pick->GetObjectGuid();
    lastNeighbourPick = rulePick->GetObjectGuid();

    count.micros.fetch_add(uint64(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()),
        std::memory_order_relaxed);
    return applies && best ? best : pick;
}

// twow-repo#656 (AiPlayerbot.Grind.SafePull, #653 lever 1): the risk of every stage-2 candidate = the idle creatures of this pass's
// "possible targets" list (no new grid search, no raycast) that would join a fight with it - linked to it (AGGRO_TOGETHER group,
// same-faction assist, call for help along its run to a ranged bot; weight 2) or seeing the bot at the candidate, at the ranged fight
// spot or on the approach line bot -> spot (weight 1); each creature once (SafePullPolicy.h). The pick is the lowest risk, then the
// lowest score distance, inside the level guard. A bot that is not ready (HP / mana) for a pick with risk >= WaitRisk gets no target
// for at most MaxWaitS. applies = false only counts (shadow) and returns the distance pick. The last pick is kept on the bot for the
// [SafePull] death line.
Unit* GrindTargetValue::SafePick(Unit* pick, std::vector<NeighbourCandidate> const& candidates, std::list<ObjectGuid> const& targets, bool applies)
{
    using namespace ai::safe_pull;
    if (!pick || candidates.empty())
        return pick;

    auto const started = std::chrono::steady_clock::now();
    uint32 const cohort = ai::cohort::InB(bot->GetGUIDLow()) ? 1 : 0;
    uint32 const cls = bot->getClass() < MaxClass ? bot->getClass() : 0;
    Counters& count = counters[cohort];
    count.passes.fetch_add(1, std::memory_order_relaxed);
    count.candidates.fetch_add(candidates.size(), std::memory_order_relaxed);

    struct Entry
    {
        Creature* creature;
        uint32 faction;
        CreatureGroup* group;    // only an AGGRO_TOGETHER group
        float attackDistance;    // 0 = does not attack on sight
        bool assists;            // may answer CallAssistance / CallForHelp against the bot
    };
    std::vector<Entry> entries;
    entries.reserve(std::min<size_t>(targets.size(), MaxEntries));
    for (ObjectGuid const& guid : targets)
    {
        if (entries.size() >= MaxEntries)
        {
            count.capped.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        Creature* creature = ai->GetCreature(guid);
        // As the neighbour rule: a creature already in combat, owned, critter, totem or evading never joins.
        if (!creature || !sServerFacade.IsAlive(creature) || creature->IsInCombat() || creature->GetCharmerOrOwnerGuid() ||
            creature->IsPet() || creature->IsTotem() || creature->IsCritter() || creature->IsInEvadeMode())
            continue;
        CreatureGroup* group = creature->GetCreatureGroup();
        Entry entry;
        entry.creature = creature;
        entry.faction = creature->GetFactionTemplateId();
        entry.group = group && group->HasGroupFlag(OPTION_AGGRO_TOGETHER) ? group : nullptr;
        entry.attackDistance = creature->CanAttackOnSight(bot) && creature->CanInitiateAttack() ? creature->GetAttackDistance(bot) : 0.0f;
        entry.assists = creature->IsHostileTo(bot) && !creature->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_ASSIST) &&
            !creature->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO);
        if (!entry.group && entry.attackDistance <= 0.0f && !entry.assists)
            continue;
        entries.push_back(entry);
    }

    float const assistRadius = sWorld.getConfig(CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS);
    float const pullRange = ai->IsRanged(bot) ? sPlayerbotAIConfig.spellDistance : 0.0f;
    float const sight = sPlayerbotAIConfig.sightDistance;
    float const bx = bot->GetPositionX();
    float const by = bot->GetPositionY();

    // The risk of candidate i; `last` (optional) receives the counted creatures, `kinds` counts the hits per kind (once per pass).
    auto riskOf = [&](size_t i, LastPick* last, bool kinds) -> uint32
    {
        Creature* x = dynamic_cast<Creature*>(candidates[i].unit);
        if (!x)
            return 0;   // a player candidate: no camp
        CreatureGroup* xGroup = x->GetCreatureGroup();
        if (xGroup && !xGroup->HasGroupFlag(OPTION_AGGRO_TOGETHER))
            xGroup = nullptr;
        CreatureInfo const* xInfo = x->GetCreatureInfo();
        float const callForHelp = xInfo ? xInfo->call_for_help_range : 0.0f;
        float const botDistance = candidates[i].botDistance;
        float ux = 0.0f, uy = 0.0f;
        if (botDistance > 0.1f)
        {
            ux = (bx - x->GetPositionX()) / botDistance;
            uy = (by - x->GetPositionY()) / botDistance;
        }
        float const offset = SpotOffset(botDistance, pullRange);
        float const sx = x->GetPositionX() + ux * offset;
        float const sy = x->GetPositionY() + uy * offset;
        uint32 risk = 0;
        for (Entry const& e : entries)
        {
            if (e.creature == x)
                continue;
            float const ex = e.creature->GetPositionX();
            float const ey = e.creature->GetPositionY();
            float const dx = ex - x->GetPositionX();
            float const dy = ey - x->GetPositionY();
            float const dz = e.creature->GetPositionZ() - x->GetPositionZ();
            float const distSq = dx * dx + dy * dy + dz * dz;
            uint32 weight = 0;
            std::atomic<std::uint64_t>* kind = nullptr;
            if (xGroup && e.group == xGroup)
            {
                weight = LinkWeight;
                kind = &count.rkLink;
            }
            else if (e.assists && e.faction == x->GetFactionTemplateId() && Within(distSq, assistRadius + AssistPad))
            {
                weight = LinkWeight;
                kind = &count.rkLink;
            }
            else if (e.assists && callForHelp > 0.0f && e.creature->IsFriendlyTo(x))
            {
                float const chaseSq = ChaseDistSq(dx, dy, ux, uy, botDistance, pullRange);
                if (chaseSq >= 0.0f && Within(chaseSq, callForHelp + CallForHelpPad))
                {
                    weight = LinkWeight;
                    kind = &count.rkLink;
                }
            }
            if (!weight && e.attackDistance > 0.0f)
            {
                float const r = e.attackDistance + SightPad;
                float const sdx = ex - sx;
                float const sdy = ey - sy;
                if (Within(distSq, r))
                {
                    weight = SightWeight;
                    kind = &count.rkTarget;
                }
                else if (offset > 0.0f && Within(sdx * sdx + sdy * sdy, r))
                {
                    weight = SightWeight;
                    kind = &count.rkSpot;
                }
                else if (Within(SegmentDistSq(ex, ey, bx, by, sx, sy), r))
                {
                    weight = SightWeight;
                    kind = &count.rkPath;
                }
            }
            if (!weight)
                continue;
            risk = Add(risk, weight);
            if (kinds)
                kind->fetch_add(1, std::memory_order_relaxed);
            if (last && last->counted < MaxCounted)
                last->guids[last->counted++] = e.creature->GetObjectGuid().GetRawValue();
        }
        bool const edge = Edge(botDistance, sight);
        if (edge && kinds)
            count.edge.fetch_add(1, std::memory_order_relaxed);
        return EdgeRisk(risk, edge);
    };

    std::vector<uint32> risks(candidates.size(), 0);
    for (size_t i = 0; i < candidates.size(); ++i)
        risks[i] = riskOf(i, nullptr, true);

    auto addMicros = [&]()
    {
        count.micros.fetch_add(uint64(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count()),
            std::memory_order_relaxed);
    };

    // The distance pick is one of the candidates (the solo replace decision only takes recorded ones).
    size_t p = candidates.size();
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].unit == pick)
            p = i;
    if (p == candidates.size())
    {
        addMicros();
        return pick;
    }
    count.picks.fetch_add(1, std::memory_order_relaxed);
    classPicks[cohort][cls].fetch_add(1, std::memory_order_relaxed);

    Creature* pickCreature = dynamic_cast<Creature*>(pick);
    bool const pickElite = pickCreature && pickCreature->GetCreatureInfo() && pickCreature->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL;
    size_t b = p;
    bool guarded = false;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i == p || !Better(risks[i], candidates[i].distance, risks[b], candidates[b].distance))
            continue;
        Creature* c = dynamic_cast<Creature*>(candidates[i].unit);
        bool const elite = c && c->GetCreatureInfo() && c->GetCreatureInfo()->Rank > CREATURE_ELITE_NORMAL;
        if (!GuardOk(int32(candidates[i].unit->GetLevel()), elite, int32(pick->GetLevel()), pickElite))
        {
            guarded = true;
            continue;
        }
        b = i;
    }
    if (guarded)
        count.guarded.fetch_add(1, std::memory_order_relaxed);
    count.pickRisk.fetch_add(risks[p], std::memory_order_relaxed);
    count.ruleRisk.fetch_add(risks[b], std::memory_order_relaxed);
    if (!risks[p])
        count.pickSafe.fetch_add(1, std::memory_order_relaxed);
    if (!risks[b])
        count.ruleSafe.fetch_add(1, std::memory_order_relaxed);
    if (b != p)
    {
        count.extraYd.fetch_add(uint64(std::max(0.0f, candidates[b].botDistance - candidates[p].botDistance) + 0.5f), std::memory_order_relaxed);
        (applies ? count.changed : count.wouldChange).fetch_add(1, std::memory_order_relaxed);
        (applies ? classChanged : classWould)[cohort][cls].fetch_add(1, std::memory_order_relaxed);
    }

    // Readiness (fail closed): attackers never reach this point (stage 1), so a wait only delays a new pull.
    uint32 const now = uint32(time(nullptr));
    bool const usesMana = bot->GetPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) > 0;
    uint32 const manaPct = usesMana ? uint32(uint64(bot->GetPower(POWER_MANA)) * 100 / bot->GetMaxPower(POWER_MANA)) : 100;
    bool const ready = Ready(uint32(bot->GetHealthPercent()), usesMana, manaPct, sPlayerbotAIConfig.grindSafePullReadyHp,
        sPlayerbotAIConfig.grindSafePullReadyMana);
    uint32 const waited = safeWaitSince && now >= safeWaitSince ? now - safeWaitSince : 0;
    bool const wait = Waits(risks[b], ready, waited);
    if (wait)
    {
        if (!safeWaitSince)
            safeWaitSince = now;
        (applies ? count.waits : count.wouldWait).fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        if (safeWaitSince && risks[b] >= WaitRisk && !ready)
            count.waitTimeout.fetch_add(1, std::memory_order_relaxed);
        safeWaitSince = 0;
    }

    // The pick for the death line: the rule's target with its counted set (recomputed once, without the kind counters).
    LastPick& last = ai->safePullLast;
    last = LastPick();
    last.target = candidates[b].unit->GetObjectGuid().GetRawValue();
    last.at = now;
    last.pickRisk = risks[p];
    last.applied = applies;
    last.risk = riskOf(b, &last, false);

    addMicros();
    if (!applies)
        return pick;
    return wait ? nullptr : candidates[b].unit;
}
