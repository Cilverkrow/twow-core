#pragma once

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "ReviveFromCorpseAction.h"
#include "playerbot/MasterWaitPolicy.h"
#include "playerbot/ReleaseVeto.h"   // twow-repo#629 G1 (DungeonClear.StayDeadInRun)
#include "playerbot/TravelMgr.h"

#include "playerbot/BotSlots.h"
#include "playerbot/CorpseRunPolicy.h"
#include "playerbot/StartZonePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ShortcutPolicy.h"   // twow-repo#595 P4 S0: [Shortcut] counters
#include "playerbot/CohortPolicy.h"     // twow-repo#601/#602 G2: DeathLoop.FarEscape mode 2 = cohort B
#include <cmath>
// The base class. Came in through botpch.h inside the bot library; a module
// including this header from outside has no botpch.
#include "GenericActions.h"
namespace ai
{
    class ReleaseSpiritAction : public ChatCommandAction
    {
    public:
        ReleaseSpiritAction(PlayerbotAI* ai, std::string name = "release") : ChatCommandAction(ai, name) {}

    public:
        virtual bool Execute(Event& event) override
        {
            Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
            if (sServerFacade.IsAlive(bot))
                return false;

            if (bot->GetCorpse() && bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            {
                ai->TellPlayerNoFacing(requester, "I am already a spirit", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

            WorldPacket& p = event.getPacket();
            if (!p.empty() && p.GetOpcode() == CMSG_REPOP_REQUEST)
                ai->TellPlayerNoFacing(requester, "Releasing...", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            else
                ai->TellPlayerNoFacing(requester, "Meet me at the graveyard", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            sLog.outDetail("Bot #%d %s:%d <%s> released", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

            WorldPacket packet(CMSG_REPOP_REQUEST);
            packet << uint8(0);
            bot->GetSession()->HandleRepopRequestOpcode(packet);

            // add waiting for ress aura
            if (bot->InBattleGround() && !ai->HasAura(2584, bot))
            {
                // cast Waiting for Resurrect
                bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED);
            }

            return true;
        }
    };

    class AutoReleaseSpiritAction : public ReleaseSpiritAction 
    {
    public:
        AutoReleaseSpiritAction(PlayerbotAI* ai, std::string name = "auto release") : ReleaseSpiritAction(ai, name) {}

        virtual bool Execute(Event& event) override
        {
            sLog.outDetail("Bot #%d %s:%d <%s> auto released", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

            WorldPacket packet(CMSG_REPOP_REQUEST);
            packet << uint8(0);
            bot->GetSession()->HandleRepopRequestOpcode(packet);

            // add waiting for ress aura
            if (bot->InBattleGround() && !ai->HasAura(2584, bot))
            {
                // cast Waiting for Resurrect
                bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED);
            }

            sPlayerbotAIConfig.logEvent(ai, "AutoReleaseSpiritAction");

            return true;
        }

        virtual bool isUseful() override
        {
            if (!sServerFacade.UnitIsDead(bot))
                return false;

            if (bot->GetCorpse())
                return false;

#ifndef MANGOSBOT_ZERO
            if (bot->InArena())
                return false;
#endif

            if (bot->InBattleGround())
                return !bot->GetCorpse() || !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);

            if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                return false;

            // twow-repo#629 G1 (ReleaseVeto.h): a module keeps this corpse where it lies (DungeonClear.StayDeadInRun:
            // a dead member of an active dungeon run waits for the healer instead of releasing out of the instance).
            // The predicate bounds the wait itself, by the seconds since the death (-1 = unknown).
            time_t const vetoDeathTime = AI_VALUE(time_t, "death time");
            if (ai::release_veto::Vetoed(bot, vetoDeathTime > 0 ? master_wait::SecondsSinceDeath(time(nullptr), vetoDeathTime, 0) : -1))
                return false;

            if (!bot->GetGroup())
                return true;

            if (!ai->GetGroupMaster())
                return true;

            if (ai->GetGroupMaster() == bot)
                return true;

            if (!ai->HasActivePlayerMaster())
                return true;

            // #276: the active master had its bounded chance to resurrect (3 min
            // in the open world, 10 min in dungeons and raids). This also covers
            // instance maps (e.g. Deeprun Tram), where the core never
            // auto-releases and a waiting bot would stay a body indefinitely.
            uint32 const masterWaitLimit = master_wait::LimitFor(bot->GetMap()->IsDungeon(),
                sPlayerbotAIConfig.deadWaitForRealMasterSeconds, sPlayerbotAIConfig.deadWaitForRealMasterInstanceSeconds);
            if (master_wait::IsExpired(master_wait::SecondsSinceDeath(time(nullptr), AI_VALUE(time_t, "death time"), 0),
                    masterWaitLimit))
            {
                sLog.outBasic("[BOT CORPSE] state=master_wait_expired guid=%u action=release wait_seconds=%u",
                    bot->GetGUIDLow(), masterWaitLimit);
                return true;
            }

            if (ai->HasActivePlayerMaster() && ai->GetGroupMaster()->GetMapId() == bot->GetMapId() && (bot->GetMap()->IsRaid() || bot->GetMap()->IsDungeon()))
                return false;

            if(sServerFacade.UnitIsDead(ai->GetGroupMaster()))
                return true;

            if (sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.sightDistance))
                return true;

            return false;
        }
    };

    // "corpse run" chat command: set the "corpse run" flag so FindCorpseAction ignores the
    // wait-for-master gate and the bot runs to its own corpse (e.g. when the master has no
    // way to resurrect it). The flag is cleared automatically when the bot resurrects.
    // A bot that is dead but has not released yet has no corpse object; it releases first
    // (the same repop the "release" command sends) and then runs (#277).
    class CorpseRunAction : public ChatCommandAction
    {
    public:
        CorpseRunAction(PlayerbotAI* ai, std::string name = "corpse run") : ChatCommandAction(ai, name) {}

        virtual bool Execute(Event& event) override
        {
            Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

            corpse_run::State const state = corpse_run::Classify(sServerFacade.IsAlive(bot),
                bot->GetCorpse() != nullptr, bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST));

            if (state == corpse_run::State::Alive)
            {
                ai->TellPlayerNoFacing(requester, "I am not dead", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

            if (state == corpse_run::State::GhostWithoutCorpse)
            {
                sLog.outString("[BOT CORPSE] %s: corpse run command received - ghost without corpse, nothing to run to", bot->GetName());
                ai->TellPlayerNoFacing(requester, "I have no corpse to return to", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

            if (state == corpse_run::State::DeadUnreleased)
            {
                sLog.outString("[BOT CORPSE] %s: corpse run command received - not released yet, releasing spirit first", bot->GetName());
                ai->TellPlayerNoFacing(requester, "Releasing my spirit first", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                WorldPacket packet(CMSG_REPOP_REQUEST);
                packet << uint8(0);
                bot->GetSession()->HandleRepopRequestOpcode(packet);

                if (!bot->GetCorpse())
                {
                    sLog.outString("[BOT CORPSE] %s: corpse run - release did not create a corpse", bot->GetName());
                    ai->TellPlayerNoFacing(requester, "I could not release my spirit", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                    return false;
                }
            }

            SET_AI_VALUE(bool, "corpse run", true);
            sLog.outString("[BOT CORPSE] %s: corpse run command received - overriding wait-for-master, running to corpse", bot->GetName());
            ai->TellPlayerNoFacing(requester, "Running to my corpse", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
    };

    class RepopAction : public SpiritHealerAction
    {
    public:
        RepopAction(PlayerbotAI* ai, std::string name = "repop") : SpiritHealerAction(ai, name) {}

    public:
        virtual bool Execute(Event& event) override
        {
            Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

            sLog.outDetail("Repop bot #%d %s:%d <%s>", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

            // G4: always visible, the acceptance signal of the death loop guard.
            bool const deathLoop = ai->IsInDeathLoop();
            if (deathLoop)
                sLog.outBasic("[DeathLoop] state=evacuate guid=%u level=%u map=%u zone=%u deaths=%u window_seconds=%u radius=%.0f",
                    bot->GetGUIDLow(), bot->GetLevel(), bot->GetMapId(), bot->GetZoneId(), ai->GetDeathLoopSize(),
                    sPlayerbotAIConfig.deathLoopWindowSeconds, sPlayerbotAIConfig.deathLoopRadius);
            ai->ClearDeathLoop();

            SET_AI_VALUE(uint32, "death count", 0);

            if (bot->IsDead())
            {
                bot->ResurrectPlayer(1.0f, false);
                bot->SpawnCorpseBones();
                bot->SaveToDB();
            }

            
            if (!ai->HasRealPlayerMaster())
            {
                if (Group* group = bot->GetGroup())
                {
                    sLog.outDetail("Repop: Removing bot #%d %s:%d <%s> from group", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
                    group->RemoveMember(bot->GetObjectGuid(), 0);
                }
            }

            RESET_AI_VALUE(Unit*, "old target");
            RESET_AI_VALUE(Unit*, "current target");
            RESET_AI_VALUE(Unit*, "pull target");
            RESET_AI_VALUE(bool, "combat::self target");
            RESET_AI_VALUE(WorldPosition, "current position");

            bot->SetSelectionGuid(ObjectGuid());
            ai->TellPlayer(requester, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
            sTravelMgr.SetNullTravelTarget(travelTarget);
            travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            travelTarget->SetExpireIn(1000);

            // Goblin/High Elf bots are spawned in Durotar/Elwynn instead of their real (custom,
            // player-only, bot-excluded) starting zone - RandomPlayerbotFactory::CreateRandomBot
            // overrides their homebind to reflect this. GetPlayerInfo() below would return the
            // real racial spawn point instead, sending the bot right back to the excluded zone
            // on every death, so route these two races through the homebind branch instead.
            // twow-repo#575 (PATH-01 E2, AiPlayerbot.CustomRaceStartZones): on, the real start zone is their spawn up to
            // level 10, the homebind above, so a level 25 goblin is not sent back to the island.
            bool useHomebindOverride = ai::start_zone::RepopUsesHomebind(bot->getRace(), bot->GetLevel(), sPlayerbotAIConfig.customRaceStartZones);
            PlayerInfo const* defaultPlayerInfo = useHomebindOverride ? nullptr : sObjectMgr.GetPlayerInfo(bot->getRace(), bot->getClass());

            // twow-repo#541 A39 (AiPlayerbot.DeathLoop.Escape): the evacuation must not end in a hostile area. A
            // Goblin / High Elf goes to its homebind below - if that lies in a hostile area (a park run had bound
            // High Elves to the Razor Hill inn: evacuated 225 times, back in Durotar every time), it is reset to the
            // race's safe start first. Other races go to their race start; a hostile homebind is reset there too,
            // so the hearthstone cannot bring them back either.
            if (sPlayerbotAIConfig.deathLoopEscape && deathLoop)
            {
                death_loop::EscapeCounter(death_loop::EscapeDetected).fetch_add(1, std::memory_order_relaxed);
                WorldPosition const home(WorldLocation(bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ()));
                bool const hostileHome = home.isEnemyHomeZoneFor(bot->GetTeam());
                bool rebound = false;
                death_loop::StartPoint start;
                if (hostileHome && useHomebindOverride && death_loop::RaceStartOverride(bot->getRace(), start))
                {
                    bot->SetHomebindToLocation(WorldLocation(start.map, start.x, start.y, start.z, 0.0f), start.zone);
                    rebound = true;
                }
                else if (hostileHome && defaultPlayerInfo)
                {
                    bot->SetHomebindToLocation(WorldLocation(defaultPlayerInfo->mapId, defaultPlayerInfo->positionX,
                        defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ, defaultPlayerInfo->orientation), defaultPlayerInfo->areaId);
                    rebound = true;
                }
                if (rebound)
                    death_loop::EscapeCounter(death_loop::EscapeRebound).fetch_add(1, std::memory_order_relaxed);

                WorldPosition const destination = defaultPlayerInfo
                    ? WorldPosition(WorldLocation(defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ))
                    : WorldPosition(WorldLocation(bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ()));
                bool const safe = !destination.isEnemyHomeZoneFor(bot->GetTeam());
                if (safe)
                    death_loop::EscapeCounter(death_loop::EscapeEscaped).fetch_add(1, std::memory_order_relaxed);
                ai->MarkDeathLoopEscape(uint32(time(nullptr)));
                sLog.outBasic("[DeathLoop] state=escape guid=%u race=%u level=%u hostile_home=%u rebound=%u to_map=%u safe=%u",
                    bot->GetGUIDLow(), uint32(bot->getRace()), bot->GetLevel(), hostileHome ? 1u : 0u, rebound ? 1u : 0u,
                    destination.getMapId(), safe ? 1u : 0u);
            }

            // twow-repo#601/#602 G2 (AiPlayerbot.DeathLoop.FarEscape): an evacuation point at the loop - on its map closer
            // than FarEscapeMinYards to the latest death, or in its zone - is replaced by the race's safe start (Goblin /
            // High Elf: Durotar / Elwynn, the others: their race start), and the homebind is set there. The goblins'
            // homebind on Blackstone Island lay 380 yd from the graveyard they died at. Never for a bot led by a real player: its
            // homebind is its player's business (OB-10 review of #702).
            death_loop::Death loopDeath;
            uint32 loopZone = 0;
            if (deathLoop && ai::cohort::Applies(sPlayerbotAIConfig.deathLoopFarEscape, bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&
                ai->GetLatestLoopDeath(loopDeath, loopZone))
            {
                auto zoneOf = [](WorldLocation const& location) -> uint32
                {
                    AreaTableEntry const* area = WorldPosition(location).GetArea();
                    return area ? (area->zone ? area->zone : area->ID) : 0;
                };
                WorldLocation const point = defaultPlayerInfo
                    ? WorldLocation(defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ)
                    : WorldLocation(bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ());
                uint32 const pointZone = zoneOf(point);
                if (death_loop::EscapeAtLoop(loopDeath.mapId, loopDeath.x, loopDeath.y, loopZone, point.mapid, point.coord_x, point.coord_y, pointZone))
                {
                    death_loop::StartPoint start;
                    bool known = death_loop::RaceStartOverride(bot->getRace(), start);
                    if (!known)
                    {
                        if (PlayerInfo const* raceInfo = sObjectMgr.GetPlayerInfo(bot->getRace(), bot->getClass()))
                        {
                            start = death_loop::StartPoint{ raceInfo->mapId, raceInfo->positionX, raceInfo->positionY, raceInfo->positionZ, raceInfo->areaId };
                            known = true;
                        }
                    }
                    WorldLocation const safeStart(start.map, start.x, start.y, start.z, 0.0f);
                    bool const moved = known &&
                        !death_loop::EscapeAtLoop(loopDeath.mapId, loopDeath.x, loopDeath.y, loopZone, safeStart.mapid, safeStart.coord_x, safeStart.coord_y, zoneOf(safeStart));
                    if (moved)
                    {
                        bot->SetHomebindToLocation(safeStart, start.zone);
                        defaultPlayerInfo = nullptr;   // the homebind branch below teleports to the safe start
                    }
                    // The race start itself lies at the loop (a start area graveyard): a homebind away from the loop and
                    // not in a hostile area takes its place, unchanged.
                    bool homebind = false;
                    WorldLocation const home(bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ());
                    if (!moved && defaultPlayerInfo)
                    {
                        homebind = !death_loop::EscapeAtLoop(loopDeath.mapId, loopDeath.x, loopDeath.y, loopZone, home.mapid, home.coord_x, home.coord_y, zoneOf(home)) &&
                            !WorldPosition(home).isEnemyHomeZoneFor(bot->GetTeam());
                        if (homebind)
                            defaultPlayerInfo = nullptr;
                    }
                    float const dx = point.coord_x - loopDeath.x;
                    float const dy = point.coord_y - loopDeath.y;
                    sLog.outBasic("[DeathLoop] state=far_escape guid=%u race=%u level=%u map=%u zone=%u point_zone=%u point_dist=%.0f min_dist=%.0f moved=%u homebind=%u to_map=%u",
                        bot->GetGUIDLow(), uint32(bot->getRace()), bot->GetLevel(), loopDeath.mapId, loopZone, pointZone,
                        point.mapid == loopDeath.mapId ? std::sqrt(dx * dx + dy * dy) : -1.0f, death_loop::FarEscapeMinYards,
                        moved ? 1u : 0u, homebind ? 1u : 0u, moved ? start.map : homebind ? home.mapid : point.mapid);
                }
            }

            // twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.Trace): the repop (critic C08) counted only, both teleports below
            // are unchanged. The source is where the bot just stood up.
            if (sPlayerbotAIConfig.shortcutTrace)
            {
                if (defaultPlayerInfo)
                    sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Repop, bot, defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ);
                else
                    sRandomPlayerbotMgr.CountShortcut(ai::shortcut::Mech::Repop, bot, bot->GetHomebindMapId(), bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ());
            }

            if (defaultPlayerInfo)
            {
                sLog.outDetail("Repop: Teleporting bot #%d %s:%d <%s> to spawn", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
                //teleport bot to spawn
                bot->TeleportTo(defaultPlayerInfo->mapId, defaultPlayerInfo->positionX, defaultPlayerInfo->positionY, defaultPlayerInfo->positionZ, defaultPlayerInfo->orientation);
                if (IsRealPlayer(bot))
                    bot->SendHeartBeat();
            }
            else
            {
                sLog.outDetail("Repop: Teleporting bot #%d %s:%d <%s> to homebind", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
                //teleport bot to homebind
                bot->TeleportToHomebind();
                bot->SendHeartBeat();
            }

            sPlayerbotAIConfig.logEvent(ai, "RepopAction");

            return true;
        }

        virtual bool isUseful() override
        {
            if (bot->InBattleGround())
                return false;

            if (ai->HasActivePlayerMaster())
                return false;

            return true;
        }
    };

    class SelfResurrectAction : public ChatCommandAction
    {
    public:
        SelfResurrectAction(PlayerbotAI* ai, std::string name = "self resurrect") : ChatCommandAction(ai, name) {}

    public:
        bool Execute(Event& event) override
        {
            WorldPacket packet(CMSG_SELF_RES);
            bot->GetSession()->HandleSelfResOpcode(packet);
            return true;
        }

        bool isPossible() override
        {
            return ai->IsStateActive(BotState::BOT_STATE_DEAD) && bot->GetUInt32Value(PLAYER_SELF_RES_SPELL);
        }
    };
}
