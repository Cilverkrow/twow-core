#pragma once

// T30 v1.1 V2 (OB-00 19.11.2026; cause: A3-anhang/t02/t30-paar1-auswertung/PSRC-URSACHE-T30-PAAR1-20261116.md; design round
// with CLI-GRP 19.11.): the pure part of the [GroupHook] line - one line per group membership change, written by the core's
// group hooks (GroupHookLog.cpp). No game header here, so t/group_hook_line_tests.cpp compiles it standalone.
//
// Line (one per event, AiPlayerbot.BotGroups.Diagnostics = 1; sLog.outBasic, never the deferred log):
//   [GroupHook] ev=<add|remove|leader|snapshot> seq=<n> bot=<guid> group=<id> members=<n> leader=<guid> other=<guid> cause=<c>
//   ev=add       Group::AddMember (also the founding leader: Group::Create calls AddMember); members after the add; cause=add
//   ev=remove    Group::RemoveMember with more than 2 members before it: members after the removal; cause=leave|kick|method<n>
//                Group::Disband (also the RemoveMember of a 2-member group, which disbands it): one line per member still in the
//                group, members = the count before the disband (2 for that case); cause=disband. The hook has no initiator, so
//                the leaving and the remaining member of a 2-member group both get "ev=remove cause=disband members=2".
//   ev=leader    Group::ChangeLeader: bot = leader = the new leader, other = the old leader; cause=change
//   ev=snapshot  once, on the first world tick of the module: one line per member of every non-battleground group (groups the
//                server loaded from the database fire no hook); cause=startup
//   other=0 for every event but leader. seq is one process-wide counter (the hooks run on world and map threads; the second
//   timestamp alone does not order lines of the same second). A logout is no group change in the core (the member stays in the
//   group, offline) and writes no line. Battleground groups write nothing.

#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::group_hook
{
// Mirrors the core's RemoveMethod (src/game/Group/Group.h: GROUP_LEAVE = 0, GROUP_KICK = 1); pinned by the source contract.
constexpr std::uint8_t kRemoveLeave = 0;
constexpr std::uint8_t kRemoveKick = 1;

inline std::string RemoveCause(std::uint8_t method)
{
    if (method == kRemoveLeave)
        return "leave";
    if (method == kRemoveKick)
        return "kick";
    return "method" + std::to_string(unsigned(method));
}

inline std::string FormatLine(char const* ev, std::uint64_t seq, std::uint32_t bot, std::uint32_t group, std::uint32_t members,
    std::uint32_t leader, std::uint32_t other, char const* cause)
{
    char line[256];
    std::snprintf(line, sizeof(line), "[GroupHook] ev=%s seq=%llu bot=%u group=%u members=%u leader=%u other=%u cause=%s",
        ev ? ev : "-", static_cast<unsigned long long>(seq), unsigned(bot), unsigned(group), unsigned(members), unsigned(leader),
        unsigned(other), cause ? cause : "-");
    return line;
}
}
