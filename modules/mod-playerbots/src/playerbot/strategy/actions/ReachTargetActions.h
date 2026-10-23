#pragma once

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/NamedObjectContext.h"
#include "playerbot/strategy/values/MoveStyleValue.h"
#include "GenericSpellActions.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/ReachLosPolicy.h"   // twow-repo#594 (Rotation.ReachLosClose)

namespace ai
{
    class ReachTargetAction : public MovementAction, public Qualified
    {
    public:
        ReachTargetAction(PlayerbotAI* ai, std::string name, float range = 0.0f) : MovementAction(ai, name), Qualified(), range(range), spellName("") {}

        virtual void Qualify(const std::string& qualifier) override
        {
            Qualified::Qualify(qualifier);

            // Get the distance from the qualified spell given
            if (!qualifier.empty())
            {
                spellName = Qualified::getMultiQualifierStr(qualifier, 0, "::");

                float maxSpellRange;
                float minSpellRangeOut = 0.0f;
                // twow-repo#594 (ReachLosClose floor): the spell's minimum range as well (hunter dead zone).
                if (ai->GetSpellRange(spellName, &maxSpellRange, &minSpellRangeOut))
                {
                    range = maxSpellRange;
                    minSpellRange = minSpellRangeOut;
                }
            }
        }

        virtual bool Execute(Event& event) override
		{
            Unit* target = GetTarget();
            if (target)
            {
                UpdateMovementState();

                // Ignore movement if too far
                const float distanceToTarget = bot->GetDistance(target, false, DIST_CALC_COMBAT_REACH);
                float chaseDist = range;
                const bool inLos = bot->IsWithinLOSInMap(target, true);
                const bool isFriend = sServerFacade.IsFriendlyTo(bot, target);

                // twow-repo#594 (ReachLosClose, CLI-ROT): the episode's in_los_after (also from isUseful, see NoteLos).
                NoteLos(target, inLos);

                // Hotfix 8.2: a hostile target the bot does not get closer to is dropped
                // and ignored for five minutes (endless "reach melee - OK" loops).
                if (!isFriend && ai->CheckReachProgress(target, distanceToTarget))
                {
                    if (AI_VALUE(Unit*, "current target") == target)
                        SET_AI_VALUE(Unit*, "current target", nullptr);
                    return false;
                }

                if (range > 0.0f)
                {
                    // Move to 75% of max range so we land comfortably inside the
                    // range envelope rather than at its edge. This prevents constant
                    // fidgeting when the target drifts slightly out of max range, and
                    // stops bots from walking all the way out to the range limit before
                    // they cast. The 0.75 factor keeps a buffer for target movement
                    // while still being well within casting distance.
                    const float preferredDist = range * 0.75f;
                    // twow-repo#594 (AiPlayerbot.Rotation.ReachLosClose, mode 0/1/2): a hostile target out of sight is chased
                    // closer too, never inside the floor (ReachLosPolicy.h); off or outside the cohort: as before (75 % of the
                    // range, so a bot already closer never moved).
                    uint32 const losMode = sPlayerbotAIConfig.rotationReachLosClose;
                    bool const closeHostile = losMode && ai::reach_los::Applies(losMode, bot->GetGUIDLow());
                    float const floorDist = ai::reach_los::Floor(closeHostile ? FloorSpellMinRange() : 0.0f, sPlayerbotAIConfig.tooCloseDistance);
                    ai::reach_los::Chase const chase = ai::reach_los::ChaseDistance(inLos, isFriend, closeHostile, distanceToTarget,
                        preferredDist, floorDist);
                    chaseDist = std::max(chase.distance - sPlayerbotAIConfig.contactDistance, 0.0f);
                    if (chase.kind != ai::reach_los::Kind::Unchanged)
                    {
                        chaseDist = std::max(chaseDist, std::min(preferredDist, floorDist));
                        if (chase.kind == ai::reach_los::Kind::Stood)
                            ai::reach_los::stood.fetch_add(1, std::memory_order_relaxed);   // no move, no episode (8.2 ends it)
                        else
                        {
                            if (chase.kind == ai::reach_los::Kind::Floored)
                                ai::reach_los::floored.fetch_add(1, std::memory_order_relaxed);
                            else if (ai::cohort::InB(bot->GetGUIDLow()))
                                ai::reach_los::closedB.fetch_add(1, std::memory_order_relaxed);
                            else
                                ai::reach_los::closedA.fetch_add(1, std::memory_order_relaxed);
                            // CLI-ROT re-review: closed_* count per evaluation, episodes once per episode (in_los_after's base).
                            if (!losClosedAt || target->GetObjectGuid() != losClosedTarget)
                                ai::reach_los::episodes.fetch_add(1, std::memory_order_relaxed);
                            losClosedAt = uint32(time(nullptr));
                            losClosedTarget = target->GetObjectGuid();
                        }
                    }
                }

                if (MoveStyleValue::WaitForEnemy(ai) && target->m_movementInfo.HasMovementFlag(movementFlagsMask) &&
                        sServerFacade.IsInFront(target, bot, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT) &&
                        sServerFacade.IsDistanceGreaterThan(distanceToTarget, sPlayerbotAIConfig.tooCloseDistance))
                {
                    return true;
                }                 

                if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "Moving to reach " << ChatHelper::formatWorldobject(target);
                    ai->TellPlayerNoFacing(GetMaster(), out);
                }
                
                if (inLos && isFriend && (range <= ai->GetRange("follow")))
                {
                    return MoveNear(target, chaseDist);
                }
                else
                {
                    return ChaseTo(target, chaseDist, bot->GetAngle(target));
                }
            }

            return false;
        }

        // True for reach actions whose target is always meant to be attacked
        // (melee, pull) - false for "reach spell", which is also used to close
        // distance on friendly targets (e.g. Blessing of Protection/Freedom).
        virtual bool RequiresAttackableTarget() const { return false; }

        virtual bool isUseful() override
		{
            // Do not move if stay strategy is set
            if (!ai->HasStrategy("stay", ai->GetState()))
            {
                Unit* target = GetTarget();
                if (target)
                {
                    // Hotfix 8.2: an unreachable target is not chased again for five minutes.
                    if (ai->IsUnreachableTarget(target->GetObjectGuid()))
                        return false;

                    // A target that can never legally be attacked (friendly, wrong
                    // phase, etc.) isn't worth closing distance to - this is what let
                    // bots walk up to and cluster around friendly NPCs once a stale
                    // "current target"/"pull target" pointed at one (same check used
                    // in PossibleAttackTargetsValue::IsPossibleTarget).
                    if (RequiresAttackableTarget() && !bot->IsValidAttackTarget(target))
                    {
                        return false;
                    }

                    // Do not move while casting
                    if (!bot->IsNonMeleeSpellCasted(true, false, true))
                    {
                        // Check if the spell for which the reach action is used for can be casted
                        if (!spellName.empty() && !ai->CanCastSpell(spellName, target, true, nullptr, true, true, true))
                        {
                            return false;
                        }

                        // Force move if not in los
                        if (bot->IsWithinLOSInMap(target, true))
                        {
                            // twow-repo#594 (ReachLosClose, CLI-ROT re-review): back in sight - Execute does not run again when
                            // the bot is already in range, so the episode's in_los_after is noted here as well.
                            NoteLos(target, true);
                            // Check if the bot is already on the range required
                            return bot->GetDistance(target, true, DIST_CALC_COMBAT_REACH) > range;
                        }

                        return true;
                    }
                }
            }

            return false;
        }

        virtual std::string GetTargetName() override { return "current target"; }
        std::string GetSpellName() const { return spellName; }

        virtual Unit* GetTarget() override
        {
            // Get the target from the qualifiers
            if (!qualifier.empty())
            {
                std::string targetQualifier;
                const std::string targetName = Qualified::getMultiQualifierStr(qualifier, 1, "::");
                if (targetName != "current target")
                {
                    targetQualifier = Qualified::getMultiQualifierStr(qualifier, 2, "::");
                }

                return targetQualifier.empty() ? AI_VALUE(Unit*, targetName) : AI_VALUE2(Unit*, targetName, targetQualifier);
            }
            else
            {
                return AI_VALUE(Unit*, GetTargetName());
            }
        }

    protected:
        float range;
        std::string spellName;
        float minSpellRange = 0.0f;        // twow-repo#594 (ReachLosClose floor): the qualified spell's minimum range, 0 = none
        uint32 losClosedAt = 0;            // ReachLosClose: time of the last "closed" evaluation (in_los_after), 0 = none
        ObjectGuid losClosedTarget;        // ReachLosClose: its target
        float hunterMinRange = -1.0f;      // ReachLosClose: Auto Shot's minimum range for an unqualified reach (-1 = not read yet)

        // twow-repo#594 (ReachLosClose, CLI-ROT re-review): one place for the episode's in_los_after, called from Execute and from
        // isUseful's in-sight branch (back in sight in range, Execute does not run again). Nothing without an open episode.
        void NoteLos(Unit* target, bool inLos)
        {
            if (!losClosedAt)
                return;
            if (uint32(time(nullptr)) - losClosedAt > ai::reach_los::InLosAfterSeconds)
            {
                losClosedAt = 0;
                return;
            }
            if (inLos && target->GetObjectGuid() == losClosedTarget)
            {
                ai::reach_los::inLosAfter.fetch_add(1, std::memory_order_relaxed);
                losClosedAt = 0;
            }
        }

        // ReachLosClose floor source: the qualified spell's minimum range; for an unqualified reach of a hunter Auto Shot's (its
        // dead zone, CLI-ROT re-review), read once; otherwise 0 (then TooCloseDistance).
        float FloorSpellMinRange()
        {
            if (minSpellRange > 0.0f)
                return minSpellRange;
            if (bot->getClass() != CLASS_HUNTER)
                return 0.0f;
            if (hunterMinRange < 0.0f)
            {
                float maxRange = 0.0f, minRange = 0.0f;
                hunterMinRange = ai->GetSpellRange("auto shot", &maxRange, &minRange) ? minRange : 0.0f;
            }
            return hunterMinRange;
        }
    };

    class CastReachTargetSpellAction : public CastSpellAction
    {
    public:
        CastReachTargetSpellAction(PlayerbotAI* ai, std::string spell, float distance) : CastSpellAction(ai, spell), distance(distance) {}

		virtual bool isUseful() override
		{
            // Do not move if stay strategy is set
            if (ai->HasStrategy("stay", ai->GetState()))
                return false;

			return sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "current target"), (distance + sPlayerbotAIConfig.contactDistance));
		}

    protected:
        float distance;
    };

    class ReachMeleeAction : public ReachTargetAction
	{
    public:
        ReachMeleeAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach melee") {}
        bool RequiresAttackableTarget() const override { return true; }
    };

    class ReachSpellAction : public ReachTargetAction
	{
    public:
        ReachSpellAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach spell", ai->GetRange("spell")) {}
    };

    class ReachPullAction : public ReachTargetAction
    {
    public:
        ReachPullAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach pull")
        {
            PullStrategy* strategy = PullStrategy::Get(ai);
            if (strategy)
            {
                range = strategy->GetRange();
            }
        }

        void Qualify(const std::string& qualifier) override
        {
            ReachTargetAction::Qualify(qualifier);

            // Reduce the range slightly for a more accurate pull (moving targets can get out of reach)
            const float threshold = 5.0f;
            range = range > threshold ? range - threshold : range;
        }

        std::string GetTargetName() override { return "pull target"; }
        bool RequiresAttackableTarget() const override { return true; }
    };

    class ReachPartyMemberToHealAction : public ReachTargetAction
    {
    public:
        ReachPartyMemberToHealAction(PlayerbotAI* ai) : ReachTargetAction(ai, "reach party member to heal", ai->GetRange("heal")) {}
        virtual std::string GetTargetName() override { return "party member to heal"; }
    };

    class ReachPartyMemberForTotemAction : public ReachTargetAction
    {
    public:
        ReachPartyMemberForTotemAction(PlayerbotAI* ai)
            : ReachTargetAction(ai, "reach party member for totem", 20.0f) {}

        void Qualify(const std::string& qualifier) override
        {
            ReachTargetAction::Qualify(qualifier);
            range = 20.0f;
        }

        Unit* GetTarget() override
        {
            std::string totemSpell = spellName;
            if (totemSpell.empty() && !qualifier.empty())
                totemSpell = Qualified::getMultiQualifierStr(qualifier, 0, "::");

            Group* group = bot->GetGroup();
            if (!group || totemSpell.empty())
                return AI_VALUE(Unit*, "master target");

            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->getSource();
                if (!member || !sServerFacade.IsAlive(member))
                    continue;

                if (member == bot)
                    continue;

                if (!member->IsInWorld() || member->GetMapId() != bot->GetMapId())
                    continue;

                if (!bot->IsWithinDistInMap(member, 100.0f, false))
                    continue;

                if (!ai->HasAura(totemSpell, member, false, true))
                    return member;
            }

            return nullptr;
        }
    };
}
