// twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace): codes, keys, budgets, tokens and the exact [Rot*] lines
// of RotCastPolicy.h. Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "RotCastPolicy.h"

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

void RequireLine(std::string const& actual, std::string const& expected, char const* message)
{
    if (actual != expected)
    {
        std::cerr << "FAILED: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        std::exit(1);
    }
}

// twow-core SpellDefines.h: SPELL_FAILED_NOT_KNOWN = 56 (0x38), SPELL_FAILED_OUT_OF_RANGE = 89, SPELL_CAST_OK = 0xFF.
constexpr std::uint8_t NotKnown = 56;
constexpr std::uint8_t OutOfRange = 89;
}

int main()
{
    using namespace ai::rot_cast;

    // 1) CanCastSpell outcome codes.
    {
        Require(EvalCode(true, 0xFF) == ResultOk, "possible without a result = ok");
        Require(EvalCode(true, OutOfRange) == (TolerFlag | OutOfRange), "possible with a result = tolerated");
        Require(EvalCode(false, NotKnown) == NotKnown, "not possible = the result");
        Require(EvalCode(false, 0xFF) == EvalNoResult, "not possible without a result");
        Require(EvalCode(false, 0) == 0, "result 0 (SPELL_FAILED_AFFECTING_COMBAT) stays 0");
    }

    // 2) Verdicts and reason texts.
    {
        Require(VerdictOf(ResultOk) == Verdict::Ok && VerdictOf(EvalMountOk) == Verdict::Ok && VerdictOf(StartOk) == Verdict::Ok, "ok codes");
        Require(VerdictOf(TolerFlag | OutOfRange) == Verdict::Tolerated, "tolerated");
        Require(VerdictOf(NotKnown) == Verdict::Fail && VerdictOf(EvalNoTarget) == Verdict::Fail, "fail codes");
        Require(VerdictOf(StartFail) == Verdict::Fail && VerdictOf(StartNoTarget) == Verdict::Fail, "start failures (0x3nn is not tolerated)");
        Require(VerdictOf(ServerShort) == Verdict::Fail && VerdictOf(ServerOther) == Verdict::Fail, "server classes");
        Require(ReasonText(NotKnown) == "res_56" && ReasonText(TolerFlag | OutOfRange) == "res_89", "result reasons");
        Require(ReasonText(ResultOk) == "ok" && ReasonText(EvalNoSpellId) == "no_spell_id" && ReasonText(StartFail) == "cast_false", "named reasons");
        Require(ReasonText(0x3FF) == "code_1023", "unknown codes stay visible");
        Require(std::string(KindText(KindEval)) == "eval" && std::string(KindText(KindStart)) == "start" &&
            std::string(KindText(KindServer)) == "server" && std::string(KindText(3)) == "other", "kinds");
    }

    // 3) Unknown spells: no spell id or SPELL_FAILED_NOT_KNOWN; tolerated NOT_KNOWN is not unknown.
    {
        Require(IsUnknown(EvalNoSpellId, NotKnown) && IsUnknown(NotKnown, NotKnown), "unknown codes");
        Require(!IsUnknown(EvalNoTarget, NotKnown) && !IsUnknown(ResultOk, NotKnown) && !IsUnknown(TolerFlag | NotKnown, NotKnown), "known codes");
        Require(std::string(UnknownText(EvalNoSpellId)) == "no_spell_id" && std::string(UnknownText(NotKnown)) == "not_known", "unknown texts");
    }

    // 4) SMSG_CAST_RESULT classic layout.
    {
        Require(ServerCode(4, 0, 0) == ServerShort, "no status byte");
        Require(ServerCode(5, 0, 0) == ResultOk, "status 0 = ok");
        Require(ServerCode(5, 2, 0) == ServerShort, "status 2 without a result byte");
        Require(ServerCode(6, 2, NotKnown) == NotKnown && ServerCode(10, 2, 0) == 0, "status 2 = the result (extra fields after it)");
        Require(ServerCode(6, 1, 7) == ServerOther, "other status");
    }

    // 5) Keys round-trip and are never 0.
    {
        Key const k = UnpackKey(PackKey(11, true, KindServer, 61001, 0x3FF));
        Require(k.cls == 11 && k.cohortB && k.kind == KindServer && k.id == 61001 && k.code == 0x3FF, "server key round trip");
        Key const e = UnpackKey(PackKey(0, false, KindEval, 0, 0));
        Require(e.cls == 0 && !e.cohortB && e.kind == KindEval && e.id == 0 && e.code == 0, "zero fields round trip");
        Require(PackKey(0, false, KindEval, 0, 0) != 0, "a key is never 0 (empty slot marker)");
        Require(PackKey(3, false, KindEval, 5, NotKnown) != PackKey(3, true, KindEval, 5, NotKnown), "cohort is part of the key");
        Require(UnpackKey(PackKey(1, false, KindStart, 0x1FFFFFF, 1)).id == 0xFFFFFF, "id is cut to 24 bits");
    }

    // 6) Budget: cap lines per 60 s window; a clock that goes back starts a new window.
    {
        std::uint32_t start = 0, used = 0;
        Require(BudgetTake(start, used, 1000, 2) && BudgetTake(start, used, 1001, 2), "two lines");
        Require(!BudgetTake(start, used, 1059, 2), "cap reached inside the window");
        Require(BudgetTake(start, used, 1060, 2) && used == 1 && start == 1060, "new window after 60 s");
        Require(BudgetTake(start, used, 900, 2) && start == 900 && used == 1, "clock went back");
        std::uint32_t s0 = 0, u0 = 0;
        Require(!BudgetTake(s0, u0, 5, 0), "cap 0 = no lines");
    }

    // 7) Tokens.
    {
        Require(Token("", 10) == "-", "empty = '-'");
        Require(Token("auto shot", 20) == "auto_shot", "spaces become '_'");
        Require(Token("a=b", 20) == "a_b", "'=' becomes '_'");
        Require(Token("abcdefghij", 8) == "abcde...", "cut with '...'");
        Require(Token("abcdefghij", 2) == "ab", "tiny cap");
        Require(Token("dps, dps assist", 40) == "dps,_dps_assist", "strategy list");
    }

    // 8) Summary arithmetic.
    {
        Summary s;
        AddToSummary(s, KindEval, ResultOk, 5);
        AddToSummary(s, KindEval, TolerFlag | OutOfRange, 2);
        AddToSummary(s, KindEval, NotKnown, 3);
        AddToSummary(s, KindStart, StartOk, 4);
        AddToSummary(s, KindStart, StartFail, 1);
        AddToSummary(s, KindServer, ResultOk, 6);
        AddToSummary(s, KindServer, NotKnown, 7);
        AddToSummary(s, KindServer, ServerShort, 1);
        Require(s.v[SumEvals] == 10 && s.v[SumPossible] == 7 && s.v[SumTolerated] == 2, "eval sums");
        Require(s.v[SumStarts] == 5 && s.v[SumStartOk] == 4, "start sums");
        Require(s.v[SumServerOk] == 6 && s.v[SumServerFail] == 7 && s.v[SumServerOther] == 1, "server sums");
    }

    // 9) Exact lines.
    {
        Summary s;
        AddToSummary(s, KindEval, ResultOk, 12);
        AddToSummary(s, KindServer, NotKnown, 3);
        RequireLine(FormatSummary("[RotCast]", "window_s", 60, s, 2, 0, 1, 0, 4),
            "[RotCast] window_s=60 evals=12 possible=12 tolerated=0 starts=0 start_ok=0 srv_fail=3 srv_ok=0 srv_other=0 "
            "keys=2 dropped=0 unknown_dropped=1 role_dropped=0 pull_dropped=4", "minute summary");
        RequireLine(FormatSummary("[RotCastTotal]", "window_min", 60, Summary(), 0, 9, 0, 0, 0),
            "[RotCastTotal] window_min=60 evals=0 possible=0 tolerated=0 starts=0 start_ok=0 srv_fail=0 srv_ok=0 srv_other=0 "
            "keys=0 dropped=9 unknown_dropped=0 role_dropped=0 pull_dropped=0", "hour summary");

        Key eval = UnpackKey(PackKey(3, false, KindEval, 7, NotKnown));
        RequireLine(FormatRow("[RotCast]", 1, eval, ActionSubject("arcane shot"), 4211),
            "[RotCast] row=1 cls=3 cohort=A kind=eval action=arcane_shot verdict=fail reason=res_56 n=4211", "eval row");
        Key server = UnpackKey(PackKey(8, true, KindServer, 12042, ResultOk));
        RequireLine(FormatRow("[RotCastTotal]", 40, server, SpellSubject(12042, "Arcane Power"), 7),
            "[RotCastTotal] row=40 cls=8 cohort=B kind=server spell=12042 name=Arcane_Power verdict=ok reason=ok n=7", "server row");
        Key start = UnpackKey(PackKey(1, false, KindStart, 2, StartFail));
        RequireLine(FormatRow("[RotCast]", 2, start, ActionSubject(""), 1),
            "[RotCast] row=2 cls=1 cohort=A kind=start action=- verdict=fail reason=cast_false n=1", "start row, empty action");

        RequireLine(FormatUnknownLine(4711, "Morwenna", 3, 7, 'B', "arcane shot", 0, EvalNoSpellId),
            "[RotCastUnknown] bot=4711 name=Morwenna cls=3 level=7 cohort=B action=arcane_shot spell=0 reason=no_spell_id", "unknown line");
        RequireLine(FormatRoleLine(75, "Thaneira", 1, 12, 'B', -1, "protection", true, false, 0, true, true, false, 3,
                "dps, dps assist, tank assist", "food, quest"),
            "[RotRole] guid=75 name=Thaneira cls=1 level=12 cohort=B tab=-1 path=protection role=TANK forced=0 free=1 member=1 "
            "autoload=0 suppressed=3 co=dps,_dps_assist,_tank_assist nc=food,_quest", "role line");
        RequireLine(FormatPullLine(50, "Kettlina", 11, 15, 'A', 3127, 13, 64, -1, 1, 22.71f),
            "[RotPull] bot=50 name=Kettlina cls=11 level=15 cohort=A entry=3127 tlevel=13 hp=64 mana=-1 attackers=1 dist=22.7",
            "pull line");
        Require(std::string(RoleText(false, true)) == "HEAL" && std::string(RoleText(false, false)) == "DPS", "role texts");
    }

    // 10) Long inputs are cut by Token(), so every field survives (BotLog line limit 4096).
    {
        std::string const longList(5000, 'x');
        std::string const line = FormatRoleLine(1, longList, 1, 1, 'A', 0, longList, false, false, 0, false, false, true, 0,
            longList, longList);
        Require(line.size() < 1300, "role line bounded by the caps (name 40, path 40, co 512, nc 512)");
        Require(line.find(" co=" + std::string(509, 'x') + "... nc=") != std::string::npos, "co= cut to 512 with '...'");
        Require(line.size() >= 3 && line.compare(line.size() - 3, 3, "...") == 0, "nc= is the cut last field");
        std::string const unknown = FormatUnknownLine(1, longList, 1, 1, 'A', longList, 1, NotKnown);
        Require(unknown.size() < 300, "unknown line bounded by the caps (name 40, action 80)");
        std::string const lastField = " reason=not_known";
        Require(unknown.size() > lastField.size() &&
            unknown.compare(unknown.size() - lastField.size(), lastField.size(), lastField) == 0, "unknown line keeps its last field");
        std::string const pull = FormatPullLine(1, longList, 1, 1, 'A', 1, 1, 1, 1, 1, 1.0f);
        Require(pull.size() < 200 && pull.find(" dist=1.0") != std::string::npos, "pull line keeps its last field");
    }

    // 11) Home slots: every field of the key moves the slot, so one code does not pile into one cluster.
    {
        bool seen[1024] = {};
        unsigned distinct = 0;
        for (std::uint8_t cls = 1; cls <= 11; ++cls)
            for (int b = 0; b < 2; ++b)
                for (std::uint32_t action = 1; action <= 40; ++action)
                {
                    std::size_t const slot = HomeSlot(PackKey(cls, b != 0, KindEval, action, ResultOk), 10);
                    Require(slot < 1024, "slot inside the table");
                    if (!seen[slot])
                    {
                        seen[slot] = true;
                        ++distinct;
                    }
                }
        Require(distinct > 500, "880 'ok' keys of one code spread over more than 500 of 1024 slots");
        Require(HomeSlot(PackKey(3, false, KindEval, 7, ResultOk), 10) != HomeSlot(PackKey(3, true, KindEval, 7, ResultOk), 10) ||
            HomeSlot(PackKey(4, false, KindEval, 7, ResultOk), 10) != HomeSlot(PackKey(3, false, KindEval, 7, ResultOk), 10),
            "cohort / class move the slot");
    }

    std::cout << "rot_cast_policy_tests passed\n";
    return 0;
}
