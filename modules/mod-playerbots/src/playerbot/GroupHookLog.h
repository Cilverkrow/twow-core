#pragma once

// T30 v1.1 V2: the [GroupHook] membership line (format: GroupHookLine.h).
namespace ai::group_hook
{
// Registers the GroupScript that writes the [GroupHook] lines (AddSC_playerbot_hooks).
void AddGroupHookObserver();

// World thread, every tick: writes the ev=snapshot lines once (first call), then returns at once.
void SnapshotOnce();
}
