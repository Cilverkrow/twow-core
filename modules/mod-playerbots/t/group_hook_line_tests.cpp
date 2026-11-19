// T30 v1.1 V2 (OB-00 19.11.2026): the pure [GroupHook] line (GroupHookLine.h). The game side (GroupHookLog.cpp: the
// GroupScript overrides, sLog.outBasic, no deferred log, no player lookup, the snapshot call) is pinned by
// t/contracts.d/t30v2_group_hook_source_contract_tests.cmake.

#include "GroupHookLine.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void RequireEq(std::string const& got, std::string const& want, char const* message)
{
    if (got != want)
    {
        std::cerr << "FAILED: " << message << "\n  got:  " << got << "\n  want: " << want << '\n';
        std::exit(1);
    }
}

using namespace ai::group_hook;
}

int main()
{
    // Remove causes mirror the core's RemoveMethod.
    RequireEq(RemoveCause(0), "leave", "GROUP_LEAVE = 0 -> leave");
    RequireEq(RemoveCause(1), "kick", "GROUP_KICK = 1 -> kick");
    RequireEq(RemoveCause(7), "method7", "unknown method -> method<n>");
    Require(kRemoveLeave == 0 && kRemoveKick == 1, "mirrored RemoveMethod values");

    // Exact field order: the evaluator parses key=value tokens, the T33/T30 extract greps "[GroupHook] ".
    RequireEq(FormatLine("add", 1, 4534, 396, 3, 166, 0, "add"),
        "[GroupHook] ev=add seq=1 bot=4534 group=396 members=3 leader=166 other=0 cause=add", "add line");
    RequireEq(FormatLine("remove", 2, 4534, 396, 2, 166, 0, RemoveCause(0).c_str()),
        "[GroupHook] ev=remove seq=2 bot=4534 group=396 members=2 leader=166 other=0 cause=leave", "remove line");
    RequireEq(FormatLine("remove", 3, 153, 102, 2, 153, 0, "disband"),
        "[GroupHook] ev=remove seq=3 bot=153 group=102 members=2 leader=153 other=0 cause=disband", "disband line");
    RequireEq(FormatLine("leader", 4, 27, 53, 3, 27, 153, "change"),
        "[GroupHook] ev=leader seq=4 bot=27 group=53 members=3 leader=27 other=153 cause=change", "leader line");
    RequireEq(FormatLine("snapshot", 5, 9, 349, 2, 7, 0, "startup"),
        "[GroupHook] ev=snapshot seq=5 bot=9 group=349 members=2 leader=7 other=0 cause=startup", "snapshot line");

    // Largest values fit the buffer without truncation.
    std::string const big = FormatLine("snapshot", UINT64_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, "method255");
    RequireEq(big, "[GroupHook] ev=snapshot seq=18446744073709551615 bot=4294967295 group=4294967295 members=4294967295 "
                   "leader=4294967295 other=4294967295 cause=method255", "max values, no truncation");

    // Null strings never crash the formatter.
    RequireEq(FormatLine(nullptr, 6, 1, 2, 3, 4, 0, nullptr),
        "[GroupHook] ev=- seq=6 bot=1 group=2 members=3 leader=4 other=0 cause=-", "null ev/cause");

    std::cout << "group_hook_line: all checks passed\n";
    return 0;
}
