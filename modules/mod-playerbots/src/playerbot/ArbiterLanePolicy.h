#pragma once

#include "CohortPolicy.h"
#include "SupervisorPolicy.h"

#include <cstdint>

namespace ai::supervisor
{
// A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): the pure lane rules of the enforcing arbiter.
// Design: ws-00/projekt-durchleuchtung/A3-S3-T02-ENTWURF.md v0.3. No game types, no locks, no config reads.
// A lane is the owner of the live travel target; a writer of a lower lane may not replace it while the owner holds it.
enum class Lane : std::uint8_t
{
    None = 0,        // idle / no owner
    Questbook = 1,
    Group = 2,
    Command = 3,
};

inline char const* LaneName(Lane lane)
{
    switch (lane)
    {
        case Lane::None: return "none";
        case Lane::Questbook: return "questbook";
        case Lane::Group: return "group";
        case Lane::Command: return "command";
    }
    return "none";
}

// neutral: the writer is always admitted and never takes the lane. lane: the lane an admitted write takes over.
struct WriterLane
{
    bool neutral;
    Lane lane;
};

inline WriterLane LaneOfWriter(WriterId writer)
{
    switch (writer)
    {
        case WriterId::Select:
        case WriterId::Requester:
        case WriterId::Brain:
        case WriterId::GroupFollow:
        case WriterId::Errand:
            return {false, Lane::Questbook};
        case WriterId::Group:
            return {false, Lane::Group};
        case WriterId::Command:   // neutral until T02-c
        case WriterId::Execution:
        case WriterId::Lifecycle:
        case WriterId::Manager:
            return {true, Lane::None};
        case WriterId::Test:
        case WriterId::Unknown:
            return {false, Lane::None};
    }
    return {false, Lane::None};   // any value >= kWriterIds
}

// The owner still holds: set, the same write generation as when it took the target, the target still open, and a group owner
// only while the supervisor is in group mode.
inline bool OwnerHolds(Lane owner, std::uint32_t ownerGen, std::uint32_t curGen, std::uint8_t status, bool nullDest,
    bool groupMode)
{
    return owner != Lane::None && ownerGen == curGen && IsOpen(status, nullDest) && (owner != Lane::Group || groupMode);
}

enum class AdmitVerdict : std::uint8_t
{
    Off = 0,       // enforcement is off
    Neutral = 1,   // a null destination or a neutral writer: admitted, no owner change
    Admit = 2,     // the writer's lane is at least the held lane
    DenyLane = 3,  // the writer's lane is below the held lane
};

inline AdmitVerdict DecideAdmit(bool enforce, WriterId writer, bool nullDest, Lane held)
{
    if (!enforce)
        return AdmitVerdict::Off;
    WriterLane const wl = LaneOfWriter(writer);
    if (nullDest || wl.neutral)
        return AdmitVerdict::Neutral;
    return wl.lane >= held ? AdmitVerdict::Admit : AdmitVerdict::DenyLane;
}

// The owner after the write: an admitted write takes its own lane, every other verdict ends the ownership.
inline Lane OwnerAfter(AdmitVerdict verdict, WriterId writer)
{
    return verdict == AdmitVerdict::Admit ? LaneOfWriter(writer).lane : Lane::None;
}

// A writer of lane `want` may close or replace the target while `held` is the owner.
inline bool LaneFreeFor(Lane want, Lane held)
{
    return want >= held;
}

// The probe bots (AiPlayerbot.Supervisor.ArbiterProbe = 1): cohort B, every 25th pair of guids.
inline bool IsProbeBot(std::uint32_t guidLow)
{
    return ai::cohort::InB(guidLow) && ((guidLow / 2) % 25) == 0;
}
}
