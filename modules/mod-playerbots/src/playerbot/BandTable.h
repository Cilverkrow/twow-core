#pragma once

#include "playerbot/BandTablePolicy.h"

// A3 step 3 planner P-1 (AiPlayerbot.Planner.BandTable, default ""): the loader of the planner's band table. Rules and formats:
// BandTablePolicy.h. Built once at boot on the world thread (TravelMgr::LoadQuestTravelTable, after the hub ladder) before any
// bot update, published last (release store, acquire load), never rebuilt or freed - no lock. An empty path: no file I/O, no
// line, Get() == nullptr (the planner runs without a zone term: fb=band_table_off).
namespace ai::band_table
{
void Load();

// The published table, or nullptr (off, not loaded, or failed). Any thread.
Table const* Get();
}
