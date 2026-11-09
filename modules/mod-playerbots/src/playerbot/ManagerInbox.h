#pragma once

#include "playerbot/ManagerInboxPolicy.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A3-GERUEST-L60 7.2.11 (AiPlayerbot.BotGroups.Manager.StatusSeconds, default 0): the one module-wide inbox of the group manager.
// One ring (ManagerInboxPolicy.h) under one mutex, held only while events are copied in or out.
namespace ai::manager_inbox
{
// Any thread (a bot's map thread). false = ring full: the event is dropped and counted; the caller never waits for the manager.
bool Post(Event const& event);

// Any thread: the bot's answer to a manager directive (state kAckReceived / kAckApplied / kAckRefused, reason by directive).
// Agreed with CLI-Projekt 09.11.; no caller in this pair (their mailbox pair or T30 wires it). Same drop rule as Post.
bool PostDirectiveAck(std::uint32_t guidLow, std::uint32_t seq, std::uint8_t state, std::uint8_t reason);

// World thread only, once per manager pass (GroupManager.cpp): appends up to max events to out, oldest first.
std::size_t Drain(std::vector<Event>& out, std::size_t max);

// Monotonic totals since start; the minute line prints their increments.
Counters Read();

// The minute line, own tag (the manager decides when; this only writes it).
void LogMinute(MinuteLine const& minute);
}
