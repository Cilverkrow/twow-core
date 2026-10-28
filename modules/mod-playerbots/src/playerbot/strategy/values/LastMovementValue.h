#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelNode.h"
#include "playerbot/TransportEdgePolicy.h"
#include "playerbot/TransportBoardPolicy.h"

// Declared in MovementActions.h, which includes this header the other way
// around - a scoped enum with fixed underlying type forward-declares cleanly,
// and the Set() parameter below only passes it through.
enum class MovementPriority : uint8;

namespace ai
{
    class LastMovement
    {
    public:
        LastMovement()
        {
            clear();
        }

        LastMovement(LastMovement& other)
        {
            taxiNodes = other.taxiNodes;
            taxiMaster = other.taxiMaster;
            lastFollow = other.lastFollow;
            lastAreaTrigger = other.lastAreaTrigger;
            lastTransportEntry = other.lastTransportEntry;
            transportEdge = other.transportEdge;
            transportDeck = other.transportDeck;
            transportBoard = other.transportBoard;
            lastPath = other.lastPath;
            lastMoveShort = other.lastMoveShort;
            nextTeleport = other.nextTeleport;
            farMoveTarget = other.farMoveTarget;
            farMoveAt = other.farMoveAt;
            farMoveArrive = other.farMoveArrive;
            farMoveDeclined = other.farMoveDeclined;
            farMoveArriveSet = other.farMoveArriveSet;
            farMoveDeclinedSet = other.farMoveDeclinedSet;
            farMoveArrivedAt = other.farMoveArrivedAt;
            transportWaitMs = other.transportWaitMs;
            fleeCount = other.fleeCount;
            lastFleeAttempt = other.lastFleeAttempt;
            fleeBreakAttempt = other.fleeBreakAttempt;
            fleeBreakCount = other.fleeBreakCount;
            fleeChaseTarget = other.fleeChaseTarget;
            fleeChaseMs = other.fleeChaseMs;
            pathReuseStamped = other.pathReuseStamped;
            pathReuseDest = other.pathReuseDest;
            pathReuseBack = other.pathReuseBack;
            pathReuseStartMap = other.pathReuseStartMap;
            pathReuseBuiltMs = other.pathReuseBuiltMs;
            dispatchSet = other.dispatchSet;
            dispatchTarget = other.dispatchTarget;
            dispatchFrom = other.dispatchFrom;
            moveEvent = Event();
        }

        void clear()
        {
            lastPath.clear();
            lastFollow = NULL;
            lastAreaTrigger = 0;
            lastTransportEntry = 0;
            lastFlee = 0;
            fleeCount = 0;
            lastFleeAttempt = 0;
            lastMoveShort = WorldPosition();
            nextTeleport = 0;
            pathReuseStamped = false;  // twow-repo#541 (L1): every reset of the movement drops the reuse stamp
            moveEvent = Event();
        }

        void Set(Unit* lastFollow)
        {
            setPath(TravelPath());
            this->lastFollow = lastFollow;
        }

        void setPath(TravelPath path) { lastPath = path; }

        // Ported dungeon-clear movement records where it sent the bot and for
        // how long, and zeroes the wait to unblock the next order. This tree
        // never read such a wait (IsWaitingForLastMove answers false, see
        // MovementActions.h), so the field is bookkeeping the porting code
        // maintains for its own log lines - kept faithful, not load-bearing.
        void Set(uint32 mapId, float x, float y, float z, float o, float delay, MovementPriority /*priority*/)
        {
            lastMoveShort = WorldPosition(mapId, x, y, z, o);
            lastdelayTime = delay;
        }
        float lastdelayTime = 0.0f;
    public:
        std::vector<uint32> taxiNodes;
        ObjectGuid taxiMaster;
        Unit* lastFollow;
        uint32 lastAreaTrigger;
        uint32 lastTransportEntry;
        // twow-repo#342 (S1, AiPlayerbot.Path.TransportEdgeTrace): the bot's transport edge episode (attempt / board) and
        // the key cache of the movement callers; written only with the trace on, by the bot's own movement (bot thread).
        // clear() leaves it alone: a running episode is closed by the next hook (route_changed / left), never dropped
        // without a line, and the cache is checked against the path on every use.
        transport_edge::State transportEdge;
        // twow-repo#342 (S3b-0, AiPlayerbot.Path.TransportDeckProbe): the dock wait the deck probe ran in (one probe per
        // wait); written only with the probe on, by UseTransport (bot thread). clear() leaves it alone: the wait record
        // lapses by itself (S1's gap / same-point rule).
        transport_board::DeckWait transportDeck;
        // twow-repo#342 (S3b-1, AiPlayerbot.Path.TransportBoardFix): the ride record (boarded by the fix: vessel, boarding point,
        // arrival dock, exit), the board try throttle and the shared stop test answer; written only with the fix on (the stop
        // test answer also with the deck probe on), by the movement code (bot thread). clear() leaves it alone: a re-planned
        // or cleared path aboard is rebuilt from the record (KeepRideTail), and the exit or the lost-ride recovery clears it.
        transport_board::BotState transportBoard;
        time_t lastFlee;
        // Number of flee actions dispatched in quick succession (within returnDelay of each
        // other). Used to detect a "subsequent" flee loop so spellcasting can take priority.
        uint32 fleeCount;
        // Wall-clock of the last dispatched flee, used to decide whether the next flee is
        // "subsequent" (close in time) or a fresh flee (window lapsed -> count resets).
        time_t lastFleeAttempt;
        // twow-repo#602 (AiPlayerbot.Rotation.MeleeFleeBreak, MFB-W 23.10.): the flee dispatches the break counts - written
        // in both Flee() branches (the random-bot chase branch never sets lastFleeAttempt), only for bots the switch applies
        // to, so FleeMultiplier's own fields stay as they were. clear() leaves them alone: the window lapses by itself.
        time_t fleeBreakAttempt = 0;
        uint32 fleeBreakCount = 0;
        // twow-repo#602 (c) (AiPlayerbot.Rotation.FleeAway): the enemy and the time (getMSTime) of the last flee chase the switch issued -
        // the real re-issue guard (the old one compares GetChaseOffset, which reads 0). Written only for bots the switch applies to.
        ObjectGuid fleeChaseTarget;
        uint32 fleeChaseMs = 0;
        TravelPath lastPath;
        WorldPosition lastMoveShort;
        time_t nextTeleport;
        // Hotfix 8.33a (twow-repo#544): RosterFarMove - the target of a pending far move and when the
        // bot may arrive there (walking time, see MovementAction::MoveTo2).
        WorldPosition farMoveTarget;
        time_t farMoveAt = 0;
        // Hotfix 8.36: where the far move ends (before the target) and a target for which no safe
        // arrival point was found (walked instead, until the target changes).
        WorldPosition farMoveArrive;
        WorldPosition farMoveDeclined;
        bool farMoveArriveSet = false;
        bool farMoveDeclinedSet = false;
        time_t farMoveArrivedAt = 0;  // hotfix 8.36: last far-move arrival (arrive_deaths)
        // twow-repo#342 ABBA-1 §6 (PosStuckPolicy.h DockWaitPaused): getMSTime of the last wait for a ship or zeppelin at its
        // dock (HandleSpecialMovement, the transport not used yet and the bot not aboard), 0 = none. Written and read by the bot's
        // own movement and travel target (bot thread). clear() leaves it alone: the pause lapses by itself.
        uint32 transportWaitMs = 0;
        // twow-repo#541 (L1, AiPlayerbot.Perf.PartialPathReuse): stamp of the path MovementAction::ResolveMovePath
        // last built - the requested destination, the end point of that path, the bot's map and the build time
        // (WorldTimer ms). Written and read only with the switch on, only by the bot's own movement (own value,
        // own thread); clear() drops it, and a lastPath that no longer ends at pathReuseBack is not reused.
        bool pathReuseStamped = false;
        WorldPosition pathReuseDest;
        WorldPosition pathReuseBack;
        uint32 pathReuseStartMap = 0;
        uint32 pathReuseBuiltMs = 0;
        // twow-repo#592 (PATH-02 diag, AiPlayerbot.Perf.RouteTimeTrace): target and start of the bot's previous dispatch
        // (MoveTo2), for the reason count in [RouteTime]. Written and read only with the trace on, by the bot's own movement;
        // clear() leaves it alone (a cleared path to the same target is still the same target).
        bool dispatchSet = false;
        WorldPosition dispatchTarget;
        WorldPosition dispatchFrom;
        Event moveEvent;
    };

    class LastMovementValue : public ManualSetValue<LastMovement&>
    {
    public:
        LastMovementValue(PlayerbotAI* ai) : ManualSetValue<LastMovement&>(ai, data) {}
    private:
        LastMovement data = LastMovement();
    };

    class StayTimeValue : public ManualSetValue<time_t>
    {
    public:
        StayTimeValue(PlayerbotAI* ai) : ManualSetValue<time_t>(ai, 0) {}
    };

    class LastLongMoveValue : public CalculatedValue<WorldPosition>
    {
    public:
        LastLongMoveValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "last long move", 30) {}

        WorldPosition Calculate() override;
    };


    class HomeBindValue : public CalculatedValue<WorldPosition>
    {
    public:
        HomeBindValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "home bind", 30) {}

        WorldPosition Calculate() override;

        virtual std::string Format() override;
    };
}
