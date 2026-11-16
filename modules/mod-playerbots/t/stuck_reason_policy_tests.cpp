// A3-HARM-UPSTREAM T40 follow-up (OB-00 (a) 16.11.2026; StuckReasonPolicy.h, AiPlayerbot.ActivityTrace.StuckReason, log only,
// default 0): the scope-exit note (null slot, last gate, nesting), the gate -> isUseful result mapping, the wrap-safe ages, the old
// "no target" rule, the leader states, the nc_mult bits, the line text (cls= right after bot=) and every token.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "StuckReasonPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
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
}

int main()
{
    using namespace ai::stuck_reason;

    // 1) AaNote: a null slot stores nothing; a set slot gets the last gate, the stage, the clock of the evaluation and the relevance.
    {
        AaRecord record;
        {
            AaNote note(AaSlot{ nullptr, 77 }, AaStage::Useful, AaGate::Activity, 5.0f);
            note.gate = AaGate::Ok;
        }
        Require(record.stage == 0 && record.gate == 0 && record.ms == 0 && record.relevance == 0.0f, "null slot: nothing stored");
    }
    {
        AaRecord record;
        {
            AaNote note(AaSlot{ &record, 1234 }, AaStage::Useful, AaGate::Activity, 5.0f);
            note.gate = AaGate::RealMaster;
            note.gate = AaGate::CannotMove;
            Require(record.stage == 0, "nothing stored before the scope ends");
        }
        Require(record.stage == std::uint8_t(AaStage::Useful) && record.gate == std::uint8_t(AaGate::CannotMove), "set slot: the last gate");
        Require(record.ms == 1234 && record.relevance == 5.0f, "set slot: the clock at entry and the relevance");
    }
    {
        AaRecord record;
        {
            AaNote first(AaSlot{ &record, 10 }, AaStage::Useful, AaGate::Activity, 1.0f);
            Require(record.stage == 0, "untouched before the first scope ends");
        }
        Require(record.gate == std::uint8_t(AaGate::Activity), "the first gate when no marker ran");
        {
            AaNote outer(AaSlot{ &record, 20 }, AaStage::Executed, AaGate::Failed, 5.0f);
            {
                AaNote inner(AaSlot{ &record, 30 }, AaStage::Possible, AaGate::NotPossible, 0.0f);
                inner.gate = AaGate::Ok;
            }
            Require(record.stage == std::uint8_t(AaStage::Possible) && record.ms == 30, "the inner note stores first");
            outer.gate = AaGate::Ok;
        }
        Require(record.stage == std::uint8_t(AaStage::Executed) && record.gate == std::uint8_t(AaGate::Ok) && record.ms == 20,
            "nested notes: the outer one is last");
    }

    // 2) The isUseful result each gate implies: only OnSight and Ok are true.
    for (int g = int(AaGate::None); g <= int(AaGate::Failed); ++g)
    {
        AaGate const gate = AaGate(g);
        bool const expected = gate == AaGate::OnSight || gate == AaGate::Ok;
        Require(UsefulResult(gate) == expected, "UsefulResult: only on_sight and ok are true");
    }

    // 3) AgeMs: never, equal, plain, the wrap at 0xFFFFFFF0 -> 0x10, the clamp.
    Require(AgeMs(100, 50, false) == -1, "never recorded: -1");
    Require(AgeMs(100, 100, true) == 0, "equal: 0");
    Require(AgeMs(1500, 500, true) == 1000, "plain difference");
    Require(AgeMs(0x10u, 0xFFFFFFF0u, true) == 0x20, "wrap-safe");
    Require(AgeMs(0x80000010u, 0x0u, true) == INT32_MAX, "clamped to INT32_MAX");

    // 4) OldRuleOf truth table: none > unseen > nc > stale > invalid / valid.
    for (int bits = 0; bits < 32; ++bits)
    {
        bool const sel = bits & 1, res = bits & 2, cached = bits & 4, invalid = bits & 8, fresh = bits & 16;
        OldRule const expected = !sel ? OldRule::None : !res ? OldRule::Unseen : !cached ? OldRule::Nc : !fresh ? OldRule::Stale :
            invalid ? OldRule::Invalid : OldRule::Valid;
        Require(OldRuleOf(sel, res, cached, invalid, fresh) == expected, "OldRuleOf truth table");
    }
    Require(OldRuleOf(true, true, true, true, true) == OldRule::Invalid, "fresh invalid fires");
    Require(OldRuleOf(true, true, true, false, true) == OldRule::Valid, "fresh valid does not fire");

    // 5) LeaderOf truth table: none > self > offline > other_map > same_map.
    for (int bits = 0; bits < 16; ++bits)
    {
        bool const grouped = bits & 1, self = bits & 2, found = bits & 4, sameMap = bits & 8;
        Leader const expected = !grouped ? Leader::None : self ? Leader::Self : !found ? Leader::Offline :
            sameMap ? Leader::SameMap : Leader::OtherMap;
        Require(LeaderOf(grouped, self, found, sameMap) == expected, "LeaderOf truth table");
    }

    // 6) nc_mult: each bit alone, none, all; the strategy name per bit.
    Require(NcMultOf(false, false, false, false, false) == 0, "nc_mult none");
    Require(NcMultOf(true, false, false, false, false) == 1, "passive = 1");
    Require(NcMultOf(false, true, false, false, false) == 2, "wbuff travel = 2");
    Require(NcMultOf(false, false, true, false, false) == 4, "dungeon clear = 4");
    Require(NcMultOf(false, false, false, true, false) == 8, "pull = 8");
    Require(NcMultOf(false, false, false, false, true) == 16, "stealthed = 16");
    Require(NcMultOf(true, true, true, true, true) == 31, "nc_mult all");
    Require(std::strcmp(NcMultName(NcMultPassive), "passive") == 0, "name passive");
    Require(std::strcmp(NcMultName(NcMultWbuffTravel), "wbuff travel") == 0, "name wbuff travel");
    Require(std::strcmp(NcMultName(NcMultDungeonClear), "dungeon clear") == 0, "name dungeon clear");
    Require(std::strcmp(NcMultName(NcMultPull), "pull") == 0, "name pull");
    Require(std::strcmp(NcMultName(NcMultStealthed), "stealthed") == 0, "name stealthed");
    Require(std::strcmp(NcMultName(3), "") == 0, "no name for a mixed value");

    // 7) FormatLine: one full line and one all-unknown line, exact text.
    {
        Line line;
        line.bot = 4711;
        line.cls = 11;
        line.aa.ms = 5;
        line.aa.relevance = 5.0f;
        line.aa.stage = std::uint8_t(AaStage::Useful);
        line.aa.gate = std::uint8_t(AaGate::HealerOrPull);
        line.aaAgeMs = 1200;
        line.passAgeMs = 1500;
        line.passMinimal = 0;
        line.grind = 1;
        line.ncMult = NcMultPassive | NcMultPull | NcMultStealthed;
        line.oldRule = OldRule::Invalid;
        line.cfmMax = 0;
        line.cfmFresh = 1;
        line.leader = Leader::SameMap;
        line.leaderGuid = 812;
        line.groupSize = 3;
        line.leaderDist = 41.6f;
        Require(FormatLine(line) == "[StuckReason] bot=4711 cls=11 aa_stage=useful aa_gate=healer_or_pull aa_age_ms=1200 aa_rel=5.00"
            " pass_age_ms=1500 pass_min=0 grind=1 nc_mult=25 old_rule=invalid cfm_max=0 cfm_fresh=1 leader=same_map leader_guid=812"
            " group_size=3 leader_dist=42", "full line");
    }
    {
        Line line;
        line.bot = 3;
        Require(FormatLine(line) == "[StuckReason] bot=3 cls=0 aa_stage=never aa_gate=none aa_age_ms=-1 aa_rel=0.00 pass_age_ms=-1"
            " pass_min=-1 grind=0 nc_mult=0 old_rule=none cfm_max=-1 cfm_fresh=-1 leader=none leader_guid=0 group_size=0"
            " leader_dist=-1", "all-unknown line");
    }

    // 8) Every token once.
    {
        std::set<std::string> stages;
        for (int s = int(AaStage::Never); s <= int(AaStage::Executed); ++s)
            stages.insert(StageToken(std::uint8_t(s)));
        Require(stages == std::set<std::string>{ "never", "useful", "possible", "executed" }, "stage tokens");
        std::set<std::string> gates;
        for (int g = int(AaGate::None); g <= int(AaGate::Failed); ++g)
            gates.insert(GateToken(std::uint8_t(g)));
        Require(gates == std::set<std::string>{ "none", "activity", "real_master", "cannot_move", "no_target", "unsafe", "trip_no_grind",
            "healer_or_pull", "on_sight", "travelling", "ok", "not_possible", "failed" }, "gate tokens");
        std::set<std::string> rules;
        for (int r = int(OldRule::None); r <= int(OldRule::Valid); ++r)
            rules.insert(OldRuleToken(OldRule(r)));
        Require(rules == std::set<std::string>{ "none", "unseen", "nc", "stale", "invalid", "valid" }, "old_rule tokens");
        std::set<std::string> leaders;
        for (int l = int(Leader::None); l <= int(Leader::SameMap); ++l)
            leaders.insert(LeaderToken(Leader(l)));
        Require(leaders == std::set<std::string>{ "none", "self", "offline", "other_map", "same_map" }, "leader tokens");
        Require(std::strcmp(StageToken(200), "unknown") == 0 && std::strcmp(GateToken(200), "unknown") == 0, "out of range: unknown");
    }

    std::cout << "stuck_reason_policy_tests: OK\n";
    return 0;
}
