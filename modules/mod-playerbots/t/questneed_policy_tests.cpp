// A3 step 2 H5 need per open objective + quest item use by need (QuestNeedPolicy.h, AiPlayerbot.QuestBook.NeedPerObjective,
// .ItemUse, .Log). Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "QuestNeedPolicy.h"

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

bool Has(std::string const& line, char const* part)
{
    return line.find(part) != std::string::npos;
}
}

int main()
{
    using namespace ai::quest_need;

    // ---- the switch and the level gate (OB-00 10.11. option A): L9 off, L10 on, in every mode --------------------------------
    {
        Require(MinLevel == 10, "the gate is level 10");
        for (std::uint32_t guid : {1u, 2u, 77u, 1000u})
        {
            bool const odd = (guid & 1u) != 0;
            Require(!H5Applies(0, guid, 30), "mode 0: never");
            Require(!H5Applies(1, guid, 9), "mode 1, L9: off");
            Require(H5Applies(1, guid, 10), "mode 1, L10: on");
            Require(H5Applies(1, guid, 60), "mode 1, L60: on");
            Require(!H5Applies(2, guid, 9), "mode 2, L9: off");
            Require(H5Applies(2, guid, 10) == odd, "mode 2, L10: cohort B (odd) only");
            Require(!H5Applies(3, guid, 9), "mode 3, L9: off");
            Require(H5Applies(3, guid, 10) == !odd, "mode 3, L10: cohort A (even) only");
            Require(!H5Applies(4, guid, 30) && !H5Applies(99, guid, 30), "unknown mode: off");
            Require(!H5Applies(1, guid, 0) && !H5Applies(1, guid, 1), "level 0 / 1: off");
        }
        Require(ValidMode(0) && ValidMode(3) && !ValidMode(4), "valid modes 0..3");
        Require(SanitizeMode(7) == 0 && SanitizeMode(2) == 2, "sanitize");
    }

    // ---- (a) the objective bits match TravelValues.h QuestObjective1..4 -------------------------------------------------------
    {
        Require(ObjectiveBit(0) == (1u << 1) && ObjectiveBit(1) == (1u << 2) && ObjectiveBit(2) == (1u << 3) && ObjectiveBit(3) == (1u << 4),
            "objective j -> bit j + 1");
        Require(ObjectiveBit(4) == 0 && ObjectiveBit(100) == 0, "no bit beyond 4 objectives");
        Require((ObjectiveBit(0) | ObjectiveBit(1) | ObjectiveBit(2) | ObjectiveBit(3)) == 0x1Eu, "all four = QuestAllObjective");
        Require((ObjectiveBit(0) & 1u) == 0 && (ObjectiveBit(3) & (1u << 5)) == 0, "never the giver or taker bit");
        Require(StalePick(false, true), "serves only a complete objective: stale");
        Require(!StalePick(true, true), "serves an open one too: not stale");
        Require(!StalePick(true, false) && !StalePick(false, false), "open only / none: not stale");
    }

    // ---- (b) the link rule ----------------------------------------------------------------------------------------------------
    {
        ObjectiveLink o;
        o.targetMatches = true; o.open = true; o.killOrCast = true; o.spellMatches = true;
        Require(Link(o) == LinkKind::SpellCredit, "L1: kill-or-cast quest, spell and target match, open");
        o.killOrCast = false;
        Require(Link(o) == LinkKind::None, "L1 needs the kill-or-cast flag");
        o.sourceItem = true;
        Require(Link(o) == LinkKind::SourceItem, "L2: source item on an open objective target");
        o.open = false;
        Require(Link(o) == LinkKind::None, "a complete objective links nothing");
        o.open = true; o.targetMatches = false;
        Require(Link(o) == LinkKind::None, "another target links nothing");
        ObjectiveLink none;
        Require(Link(none) == LinkKind::None, "no facts: no link");
        Require(CreditKey(3430, false) == 3430u, "creature credit key = entry");
        Require(CreditKey(1234, true) == (1234u | 0x80000000u), "game object credit key = entry | high bit");
    }

    // ---- (b) the cap: 2 misses on a target refuse it 600 s; a credit in 15 s is no miss ------------------------------------------
    {
        ItemUseCap cap;
        std::uint32_t t = 1000;
        Require(cap.Check(5, 900, t) == CapVerdict::Allowed, "fresh: allowed");
        cap.NoteUse(5, 900, 77, t);
        Require(cap.NoteCredit(77, t + 14999) == 1, "credit inside 15 s");
        Require(cap.PendingCount() == 0, "a credited use is done");
        cap.NoteUse(5, 900, 77, t + 20000);
        cap.NoteUse(5, 900, 77, t + 21000);
        Require(cap.Check(5, 900, t + 35999) == CapVerdict::Allowed, "misses judged only after 15 s");
        Require(cap.Check(5, 900, t + 36000) == CapVerdict::RefusedTarget, "2 misses: target refused");
        Require(cap.Check(5, 901, t + 36000) == CapVerdict::Allowed, "another target of the same item: allowed");
        Require(cap.Check(5, 900, t + 36000 + 599999) == CapVerdict::RefusedTarget, "refused for 600 s");
        Require(cap.Check(5, 900, t + 36000 + 600000) == CapVerdict::Allowed, "free again after 600 s");
        Require(cap.NoteCredit(77, t + 36000) == 0, "a late credit counts nothing");
    }

    // ---- (b) the cap: 6 misses of one item within 600 s refuse the item 600 s ---------------------------------------------------
    {
        ItemUseCap cap;
        std::uint32_t t = 50000;
        for (std::uint64_t target = 1; target <= 6; ++target)
            cap.NoteUse(9, target, 100 + std::uint32_t(target), t + std::uint32_t(target) * 1000);
        Require(cap.Check(9, 50, t + 6000 + 15000) == CapVerdict::RefusedItem, "6 misses of one item: item refused");
        Require(cap.Check(8, 50, t + 6000 + 15000) == CapVerdict::Allowed, "another item: allowed");
        Require(cap.Check(9, 50, t + 21000 + 600000) == CapVerdict::Allowed, "item free again after 600 s");

        ItemUseCap spread;
        for (std::uint32_t i = 0; i < 6; ++i)
            spread.NoteUse(9, 100 + i, 1, t + i * 200000);   // 6 misses over 1000 s: never 6 inside 600 s
        Require(spread.Check(9, 999, t + 6 * 200000) == CapVerdict::Allowed, "misses outside the 600 s window do not add up");
    }

    // ---- (b) the cap: pending bound and wrap-safe times --------------------------------------------------------------------------
    {
        CapParams p;
        p.maxPending = 3;
        ItemUseCap cap(p);
        for (std::uint32_t i = 0; i < 5; ++i)
            cap.NoteUse(1, 10 + i, 5, 100 + i);
        Require(cap.PendingCount() == 3, "pending bounded");
        ItemUseCap wrap;
        std::uint32_t const near = 0xFFFFFFFFu - 5000u;
        wrap.NoteUse(2, 7, 3, near);
        wrap.NoteUse(2, 7, 3, near + 1000);
        Require(wrap.Check(2, 7, near + 10000) == CapVerdict::Allowed, "across the wrap: not yet judged");
        Require(wrap.Check(2, 7, near + 17000) == CapVerdict::RefusedTarget, "across the wrap: judged and refused");
        Require(wrap.Check(2, 7, near + 17000 + 599999) == CapVerdict::RefusedTarget && wrap.Check(2, 7, near + 17000 + 600000) == CapVerdict::Allowed,
            "across the wrap: refused 600 s from the judging, then free");
    }

    // ---- the hour clock and the lines -----------------------------------------------------------------------------------------
    {
        HourClock c;
        Require(!c.Due(7200 + 59), "the first call starts the hour");
        Require(c.hourStart == 7200, "hour start");
        Require(!c.Due(7200 + 3599), "same hour: not due");
        Require(c.Due(10800), "next hour: due");
        Require(HourClock::StartOf(10801) == 10800, "start of hour");

        HourStats s;
        s.picksQuest = 4; s.picksStale = 1; s.calc = 30; s.calcUs = 1234; s.setN = 300;
        s.uses = 7; s.credited = 5; s.linked = 9; s.refusedCap = 2; s.refusedNolink = 11;
        std::string const need = FormatNeedLine(77, 23, true, 10800, s);
        Require(need.rfind("[QuestNeed] bot=77 cohort=B level=23 applies=1 hour=10800 ", 0) == 0, "need line head");
        Require(Has(need, " picks_quest=4 picks_stale=1 calc=30 calc_us=1234 set_n=300"), "need line counters");
        std::string const use = FormatItemUseLine(78, 23, false, 10800, s);
        Require(use.rfind("[QuestItemUse] bot=78 cohort=A level=23 applies=0 hour=10800 ", 0) == 0, "item use line head");
        Require(Has(use, " uses=7 credited=5 linked=9 refused_cap=2 refused_nolink=11"), "item use line counters");
    }

    std::cout << "questneed_policy_tests: all passed\n";
    return 0;
}
