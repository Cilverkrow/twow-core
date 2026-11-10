#pragma once

#include "playerbot/ArbiterLanePolicy.h"

class PlayerbotAI;

namespace ai
{
class TravelTarget;
}

// A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0; design ws-00/projekt-durchleuchtung/A3-S3-T02-ENTWURF.md v0.3,
// build spec A3-S3-T02A-BAUSPEC.md): the enforcing arbiter on the live travel target. One gate decides (ArbiterLanePolicy.h
// DecideAdmit); the supervisor keeps the owning lane with the write generation it holds for. Every function returns its "allow"
// value at once when the AI, the bot or its supervisor is missing, when the caller is not the bot's own map thread
// (SupervisorShadow.h OnOwnThread; another thread never reads the supervisor), or when enforcement is off (arbiter below 2). So
// at the defaults each call is a few early-returning checks: no write, no line, no count.
namespace ai::supervisor
{
// Head of the public TravelTarget destination write and of TravelTarget::CopyTarget (TravelMgr.cpp), live target only, any
// thread. false = denied: the caller writes nothing (one ev=deny line, deny counted; during the probe deny_probe and
// site=probe:<site>). An admitted write stages its lane (None for a neutral one); CommitTargetWrite makes it the owner. Foreign
// threads are always admitted and never touch the supervisor (the generation bump ends any ownership).
bool AdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest, char const* site);

// Right after the admitted write (TravelMgr.cpp, after SetTargetUnchecked in the public write and in CopyTarget): the staged lane
// owns the target for the generation the write created. A gate pass without a write (the #485 refusal) never commits.
void CommitTargetWrite(PlayerbotAI* ai, TravelTarget* target);

// The same decision without a count, a line or an owner change (ReportTravelTarget in setNewTarget). true when enforcement is
// off.
bool WouldAdmitTargetWrite(PlayerbotAI* ai, TravelTarget* target, bool nullDest);

// Request isUseful, the close-then-request errands and the probe: true when a writer of `lane` may close or replace the live
// target now (a fresh status read of the live target, never a cached value). false counts held: site "request" -> held_request,
// "probe" -> nothing, any other (errand) site -> held_errand.
bool LaneFree(PlayerbotAI* ai, Lane lane, char const* site);
}
