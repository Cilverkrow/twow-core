#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

namespace ai
{
    class NearestUnitsValue : public ObjectGuidListCalculatedValue
	{
	public:
        NearestUnitsValue(PlayerbotAI* ai, std::string name = "nearest units", float range = sPlayerbotAIConfig.sightDistance, bool ignoreLos = false) :
            ObjectGuidListCalculatedValue(ai, name, 2), range(range), ignoreLos(ignoreLos) {}
        // mod-playerbots also passes the check interval (milliseconds there,
        // this base counts the same number), fifth positional argument.
        NearestUnitsValue(PlayerbotAI* ai, std::string name, float range, bool ignoreLos, int checkInterval) :
            ObjectGuidListCalculatedValue(ai, name, checkInterval), range(range), ignoreLos(ignoreLos) {}

	public:
        virtual std::list<ObjectGuid> Calculate() override
        {
            std::list<Unit*> targets;
            FindUnits(targets);

            // twow-repo#541 (audit A03, AiPlayerbot.Perf.NearestUnitsAcceptFirst, default 0): when LOS is
            // required and the subclass declares a pure filter (AcceptUnitBeforeLos), the cheap filter runs
            // before the VMAP + dynamic-tree raycast. Both are free of AI-visible side effects, so the list
            // and its order are the same; only units the filter rejects skip the raycast.
            bool const acceptFirst = sPlayerbotAIConfig.nearestUnitsAcceptFirst && !ignoreLos && AcceptUnitBeforeLos();
            // twow-repo#541 (owner 11.10.2026 "1b", AiPlayerbot.Perf.NearestSkipHiddenBots, default 0): a parked bot
            // with Park.HideFromBots is already invisible to other bots in the core; these grid searches do not go
            // through visibility and still found it. With the switch it is skipped before the LOS raycast. Parked bots
            // are never in a group and real players are never hidden, so only parked bots drop out.
            bool const skipHidden = sPlayerbotAIConfig.nearestSkipHiddenBots;

            std::list<ObjectGuid> results;
            for(std::list<Unit *>::iterator i = targets.begin(); i!= targets.end(); ++i)
            {
                Unit* unit = *i;
                if (skipHidden && unit->GetTypeId() == TYPEID_PLAYER && static_cast<Player*>(unit)->IsHiddenFromBots())
                    continue;
                if(ai->IsSafe(unit))
                {
                    if (acceptFirst)
                    {
                        if (AcceptUnit(unit) && sServerFacade.IsWithinLOSInMap(bot, unit))
                            results.push_back(unit->GetObjectGuid());
                    }
                    else if ((ignoreLos || sServerFacade.IsWithinLOSInMap(bot, unit)) && AcceptUnit(unit))
                        results.push_back(unit->GetObjectGuid());
                }
            }
            return results;
        }

    protected:
        virtual void FindUnits(std::list<Unit*> &targets) = 0;
        virtual bool AcceptUnit(Unit* unit) = 0;
        // twow-repo#541 (audit A03) purity contract: return true only if AcceptUnit is pure, i.e. it
        // reads the unit and the bot and has no AI-visible side effects (no AI values, no context, no
        // urand, no members, no statics, no chat) and is cheaper than a raycast; core callees may still
        // emit error logs (invalid faction template). Then, behind AiPlayerbot.Perf.NearestUnitsAcceptFirst
        // and only with ignoreLos == false, Calculate runs AcceptUnit before the LOS raycast. Reviewed
        // opt-ins (no subclasses of them allowed) are pinned in
        // t/nearest_units_accept_first_source_contract_tests.cmake; a new one must be reviewed there.
        virtual bool AcceptUnitBeforeLos() const { return false; }

    protected:
        float range;
        bool ignoreLos;
	};

    class NearestStealthedUnitsValue : public NearestUnitsValue
    {
    public:
        NearestStealthedUnitsValue(PlayerbotAI* ai, float range = 30.0f) :
            NearestUnitsValue(ai, "nearest stealthed units", range) {}

    protected:
        void FindUnits(std::list<Unit*>& targets) override
        {
            MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, range);
            MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
            Cell::VisitAllObjects(bot, searcher, range);
        }
        // twow-repo#541 (audit A03): AcceptUnit only reads IsAlive, hostility and aura types (pure).
        bool AcceptUnitBeforeLos() const override { return true; }
        bool AcceptUnit(Unit* unit) override
        {
            if (!unit || !unit->IsAlive() || !sServerFacade.IsHostileTo(unit, bot))
                return false;

            return unit->HasAuraType(SPELL_AURA_MOD_STEALTH) || unit->HasAuraType(SPELL_AURA_MOD_INVISIBILITY);

            /*uint32 dispelMask = 0;
            dispelMask |= GetDispellMask(DispelType(DISPEL_STEALTH));
            dispelMask |= GetDispellMask(DispelType(DISPEL_INVISIBILITY));

            return unit->HasMechanicMaskOrDispelMaskAura(dispelMask, 0, bot);*/
        }
    };

    class NearestStealthedSingleUnitValue : public UnitCalculatedValue
    {
    public:
        NearestStealthedSingleUnitValue(PlayerbotAI* ai) :
            UnitCalculatedValue(ai, "nearest stealthed unit") {}

        virtual Unit* Calculate() override
        {
            std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "nearest stealthed units");
            if (targets.empty())
                return nullptr;

            std::vector<Unit*> units;
            for (std::list<ObjectGuid>::iterator i = targets.begin(); i != targets.end(); ++i)
            {
                Unit* unit = ai->GetUnit(*i);
                if (!unit)
                    continue;

                units.push_back(unit);
            }

            if (units.empty())
                return nullptr;

            return units[urand(0, units.size() - 1)];
        }
    };
}
