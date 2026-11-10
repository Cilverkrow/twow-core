#include "playerbot/playerbot.h"
#include "playerbot/SupervisorEnforce.h"
#include "playerbot/Supervisor.h"
#include "playerbot/SupervisorShadow.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/CohortPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): the enforcing gate on the live travel target and the probe
// (AiPlayerbot.Supervisor.ArbiterProbe, default 0). The pure rules are in ArbiterLanePolicy.h (unit test
// t/supervisor_policy_tests.cpp); counts and lines go through the shadow (SupervisorShadow.h CountEnforce / EmitEnforceLine).
namespace ai::supervisor
{
namespace
{
constexpr std::uint32_t kProbeIntervalMs = 600000;   // at most one probe per bot every 10 minutes

// The null target, the same rule as the shadow's (SupervisorShadow.cpp IsNullDest).
bool IsNullDest(TravelDestination const* dest)
{
    return !dest || dynamic_cast<NullTravelDestination const*>(dest) != nullptr;
}

// The line text of a destination as the shadow writes it (SupervisorShadow.cpp FillName): "<short name>:<entry>:<quest id|0>",
// the null target "idle:0:0". Only while the destination is alive (inside the write that holds it).
void DestName(char* buf, std::size_t n, TravelDestination const* dest)
{
    if (IsNullDest(dest))
    {
        std::snprintf(buf, n, "%s", "idle:0:0");
        return;
    }
    std::uint32_t questId = 0;
    if (auto const* quest = dynamic_cast<QuestTravelDestination const*>(dest))
        questId = quest->GetQuestId();
    std::string name = dest->GetShortName();
    for (char& c : name)
        if (c == ' ' || c == ':' || c == '=')
            c = '_';
    std::snprintf(buf, n, "%s:%d:%u", name.empty() ? "-" : name.c_str(), int(dest->GetEntry()), questId);
}

// The bot's supervisor when the gate may act: an AI with a bot, the bot's own map thread (checked before the supervisor is read),
// a supervisor, enforcement on. nullptr = allow.
Supervisor* EnforcingSupervisor(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot() || !OnOwnThread(ai))
        return nullptr;
    Supervisor* const supervisor = ai->GetSupervisor();
    return supervisor && supervisor->EnforceOn() ? supervisor : nullptr;
}

// The live travel target of the bot (the value TargetValue.h marks live).
TravelTarget* LiveTarget(PlayerbotAI* ai)
{
    AiObjectContext* const context = ai->GetAiObjectContext();
    return context ? context->GetValue<TravelTarget*>("travel target")->Get() : nullptr;
}

// One denied write: counted (deny, or deny_probe during the probe) and one ev=deny line from the shared budget. dest and status
// are the live target's, the one the owner keeps.
void Denied(PlayerbotAI* ai, Supervisor const& supervisor, TravelTarget* target, WriterId writer, Lane held, char const* site)
{
    Player* const bot = ai->GetBot();
    std::uint32_t const guid = bot->GetGUIDLow();
    bool const probe = supervisor.InProbe();
    CountEnforce(guid, probe ? EnforceCount::DenyProbe : EnforceCount::Deny);
    char dest[kDestNameSize];
    DestName(dest, sizeof(dest), target->GetDestination());
    char buf[512];
    int const w = std::snprintf(buf, sizeof(buf), "[Supervisor] bot=%u name=%.40s cohort=%c arbiter=2 ev=deny writer=%s owner=%s "
        "site=%s%s status=%s dest=%.96s", guid, bot->GetName(), cohort::Letter(guid), WriterName(writer), LaneName(held),
        probe ? "probe:" : "", site ? site : "-", StatusName(std::uint8_t(target->GetStatus())), dest);
    Sanitize(buf, w, sizeof(buf));
    EmitEnforceLine(guid, buf);
}
}

bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site)
{
    Supervisor* const supervisor = EnforcingSupervisor(ai);
    if (!supervisor || !target || !target->IsLive())
        return true;
    WriterId const writer = CurrentWriterId();
    Lane const held = supervisor->HeldOwner(target);
    AdmitVerdict const verdict = DecideAdmit(true, writer, nullDest, held);
    if (verdict == AdmitVerdict::DenyLane)
    {
        supervisor->StageOwner(Lane::None);
        Denied(ai, *supervisor, target, writer, held, site);
        return false;
    }
    // A neutral write takes no lane (OwnerAfter = None); its generation bump ends any ownership.
    supervisor->StageOwner(OwnerAfter(verdict, writer));
    return true;
}

void CommitTargetWrite(PlayerbotAI* ai, TravelTarget* target)
{
    Supervisor* const supervisor = EnforcingSupervisor(ai);
    if (!supervisor || !target || !target->IsLive())
        return;
    supervisor->CommitOwner(target->WriteGen());
}

bool WouldAdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest)
{
    Supervisor* const supervisor = EnforcingSupervisor(ai);
    if (!supervisor || !target || !target->IsLive())
        return true;
    // HeldOwner may clear an owner that no longer holds: the same decision for every later writer, so no owner change.
    return DecideAdmit(true, CurrentWriterId(), nullDest, supervisor->HeldOwner(target)) != AdmitVerdict::DenyLane;
}

bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site)
{
    Supervisor* const supervisor = EnforcingSupervisor(ai);
    if (!supervisor)
        return true;
    TravelTarget* const target = LiveTarget(ai);
    if (!target)
        return true;
    if (LaneFreeFor(lane, supervisor->HeldOwner(target)))
        return true;
    char const* const where = site ? site : "";
    std::uint32_t const guid = ai->GetBot()->GetGUIDLow();
    if (std::strcmp(where, "request") == 0)
        CountEnforce(guid, EnforceCount::HeldRequest);
    else if (std::strcmp(where, "probe") != 0)
        CountEnforce(guid, EnforceCount::HeldErrand);
    return false;
}

// The probe (AiPlayerbot.Supervisor.ArbiterProbe = 1, default 0; design 10.1): on probe bots only (ArbiterLanePolicy.h
// IsProbeBot), at most every 10 minutes, the first run spread by guid, and only on an open live target. Arbiter 2: the group lane
// owns the target for this one pass, then a Select write, a Select copy and a questbook LaneFree must all be refused, with the
// write generation and the status unchanged. Arbiter 1: no write at all; the paired control counts what 2 would deny
// (probe_shadow).
void Supervisor::ProbeTick(std::uint32_t nowMs, std::uint32_t arbiter)
{
    if (sPlayerbotAIConfig.supervisorArbiterProbe != 1 || inProbe_)
        return;
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    std::uint32_t const guid = bot->GetGUIDLow();
    if (!IsProbeBot(guid))
        return;
    if (nextProbeMs_ == 0)
    {
        nextProbeMs_ = nowMs + (guid % 600) * 1000;
        if (nextProbeMs_ == 0)
            nextProbeMs_ = 1;
        return;
    }
    if (std::int32_t(nowMs - nextProbeMs_) < 0)
        return;
    TravelTarget* const target = LiveTarget(ai_);
    if (!target || !target->IsLive())
        return;
    TravelDestination* const dest = target->GetDestination();
    std::uint8_t const status0 = std::uint8_t(target->GetStatus());
    if (!IsOpen(status0, IsNullDest(dest)))
        return;   // waits for an open target; the interval counts from the run
    nextProbeMs_ = nowMs + kProbeIntervalMs;
    if (nextProbeMs_ == 0)
        nextProbeMs_ = 1;

    if (arbiter != 2)
    {
        if (DecideAdmit(true, WriterId::Select, false, Lane::Group) == AdmitVerdict::DenyLane)
            CountEnforce(guid, EnforceCount::ProbeShadow);
        return;
    }
    // The gate acts only on the bot's own map thread (the tick runs there); anywhere else every write would pass.
    if (!EnforceOn() || !OnOwnThread(ai_))
        return;

    inProbe_ = true;
    Lane const owner0 = owner_;   // a real owner of the live target is restored after the probe
    std::uint32_t const ownerGen0 = ownerGen_;
    std::uint32_t const gen0 = target->WriteGen();
    owner_ = Lane::Group;
    ownerGen_ = gen0;
    WorldPosition* const pos = target->GetPosition();

    bool r1 = false;
    {
        WriterScope const scope(WriterId::Select);
        r1 = target->SetTarget(dest, pos);
    }
    // The copy holds the live target's own destination; a temporary one is skipped (it belongs to the live target only), and so
    // is the copy after an admitted first write (the gate is broken: no second write on the bot). -1 = not tried.
    int r2 = -1;
    if (!r1 && !dynamic_cast<TemporaryTravelDestination*>(dest))
    {
        WriterScope const scope(WriterId::Select);
        TravelTarget copy(ai_);
        copy.SetTarget(dest, pos);   // local target, not live: no gate
        r2 = target->CopyTarget(&copy) ? 1 : 0;
        target->TakeArbiterDenied();   // the probe's own denial is no caller's
    }
    bool const r3 = LaneFree(ai_, Lane::Questbook, "probe");
    bool const genOk = target->WriteGen() == gen0;
    bool const statusOk = std::uint8_t(target->GetStatus()) == status0;
    bool const pass = !r1 && r2 != 1 && !r3 && genOk && statusOk;
    CountEnforce(guid, pass ? EnforceCount::ProbePass : EnforceCount::ProbeFail);
    char buf[512];
    int const w = std::snprintf(buf, sizeof(buf), "[Supervisor] bot=%u name=%.40s cohort=%c arbiter=2 ev=probe result=%s r1=%d r2=%d "
        "r3=%d gen_ok=%d status_ok=%d", guid, bot->GetName(), cohort::Letter(guid), pass ? "pass" : "fail", r1 ? 1 : 0, r2,
        r3 ? 1 : 0, genOk ? 1 : 0, statusOk ? 1 : 0);
    Sanitize(buf, w, sizeof(buf));
    EmitEnforceLine(guid, buf);

    owner_ = owner0;
    ownerGen_ = ownerGen0;
    inProbe_ = false;
}
}
