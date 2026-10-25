#pragma once

// telemetry P0 (OB-00 25.10.2026, audit REPORT Q7): the summaries of the path traces ([RouteFailDetail], [MoveTo] partial_noop,
// [TaxiUse], [TransportEdge], [TransportBoard], [PosStuck]) used to be rolled only by the next EVENT of their trace: a quiet
// minute printed nothing (indistinguishable from "no data"), a roll after a gap summed the gap without saying so, and the first
// window was lost.
//
// ReportMinute() is called once a minute from the roster manager's minute report (world thread). It rolls the counters of every
// ENABLED trace, so each minute is a summary line, with zeros when nothing happened. A summary line carries window_s= (the
// seconds the window covers; 60 with the driver, more after a gap), or mins= for the two traces that already had it.
// Disabled traces stay silent: no line, no work.
namespace ai::lazy_roll
{
void ReportMinute();

// The [PosStuck] part (it lives in TravelMgr.cpp next to its counters); the event path of PosStuck rolls through it as well.
void ReportPosStuckSummary();
}
