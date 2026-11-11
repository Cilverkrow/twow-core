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
// Agreed with CLI-Projekt 09.11.; one caller: SupervisorGroup.cpp (T02-GROUP, SendAck). Same drop rule as Post.
bool PostDirectiveAck(std::uint32_t guidLow, std::uint32_t seq, std::uint8_t state, std::uint8_t reason);

// Any thread (T30-1b, agreed with CLI-Projekt 10.11., G2): the bot left the GROUP mode itself. A DirectiveAck with seq 0,
// state kAckLeft in b0-7 of the state word and the group serial (low 16 bits) the bot held in b16-31; reason 26 goal_done,
// 27 goal_lost, 22 arbiter_off. One caller: SupervisorGroup.cpp (T02-GROUP, SendLeft; it reports 26 and 27 only, Q5 (a)).
// Same drop rule as Post.
bool PostDirectiveLeft(std::uint32_t guidLow, std::uint16_t serial, std::uint8_t reason);

// World thread only, once per manager pass (GroupManager.cpp): appends up to max events to out, oldest first.
std::size_t Drain(std::vector<Event>& out, std::size_t max);

// Any thread (T30-0, the status post): a quest id that does not fit the objective key (PackKey false); the key went out empty.
void CountKeyOverflow();

// Monotonic totals since start (keyOverflow included); the minute line prints their increments.
Counters Read();

// The minute line, own tag (the manager decides when; this only writes it).
void LogMinute(MinuteLine const& minute);
}
