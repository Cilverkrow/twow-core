// A3 step 3 planner S5 (twow-repo#726, S5-FIX-ENTWURF-v0.1; AiPlayerbot.Planner.TimeTrace, default 0): the [PlanTime] line of
// PlannerTimePolicy.h - the key check, the microsecond difference and the line format (stage fields, cpu_us=na, the buffer bound).
// The header names no game type, so this suite compiles it standalone.

#include "PlannerTimePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

using namespace ai::planner;

void TestValidTimeTrace()
{
    Require(ValidTimeTrace(0), "TimeTrace 0 is valid");
    Require(ValidTimeTrace(1), "TimeTrace 1 is valid");
    Require(!ValidTimeTrace(2), "TimeTrace 2 is not valid (loader sets 0)");
    Require(!ValidTimeTrace(0xFFFFFFFFu), "TimeTrace max is not valid");
}

void TestUsBetween()
{
    Require(UsBetween(0, 0) == 0, "same reading = 0 us");
    Require(UsBetween(1000, 0) == 0, "a backwards reading = 0 us");
    Require(UsBetween(0, 999) == 0, "below one microsecond = 0 us");
    Require(UsBetween(0, 1000) == 1, "1000 ns = 1 us");
    Require(UsBetween(5000, 2005999) == 2000, "2000.999 us truncates to 2000");
    Require(UsBetween(0, std::int64_t(0xFFFFFFFFu) * 1000 + 5000) == 0xFFFFFFFFu, "clamped to UINT32_MAX us");
}

void TestLineShape()
{
    PlanTime t;
    t.guid = 1413;
    t.cohort = 'B';
    t.totalUs = 3812;
    t.cpuUs = 3790;
    t.ms = 4;
    t.overBudget = true;
    t.cands = 33;
    t.cut = 2;
    t.done = 1;
    t.open = 7;
    t.headUs = 41;
    t.take.ran = true;
    t.take.fetchUs = 120;
    t.take.sortUs = 7;
    t.take.loopUs = 55;
    t.take.walk = 12;
    t.take.ret = 3;
    t.give.ran = true;
    t.give.fetchUs = 2900;
    t.give.sortUs = 80;
    t.give.loopUs = 400;
    t.give.walk = 15000;
    t.give.ret = 40;
    t.restUs = 190;
    char buf[kPlanTimeBuf];
    std::size_t const len = FormatPlanTimeLine(buf, sizeof(buf), t);
    std::string const line(buf);
    Require(len == line.size(), "returned length = string length");
    Require(line == "[PlanTime] bot=1413 cohort=B us=3812 cpu_us=3790 ms=4 over_budget=1 cands=33 cut=2 done=1 open=7 head=41 "
        "take=120/7/55/12/3 obj=- give=2900/80/400/15000/40 rest=190", "line shape");

    PlanTime na;
    char nbuf[kPlanTimeBuf];
    FormatPlanTimeLine(nbuf, sizeof(nbuf), na);
    Require(std::string(nbuf) == "[PlanTime] bot=0 cohort=A us=0 cpu_us=na ms=0 over_budget=0 cands=0 cut=0 done=0 open=0 head=0 "
        "take=- obj=- give=- rest=0", "default line: cpu_us=na, every stage -");
}

void TestBufferBound()
{
    PlanTime t;
    t.guid = 0xFFFFFFFFu;
    t.cohort = 'B';
    t.totalUs = 0xFFFFFFFFu;
    t.cpuUs = 0x7FFFFFFFFFFFFFFFll;
    t.ms = 0xFFFFFFFFu;
    t.overBudget = true;
    t.cands = 0xFFFFFFFFu;
    t.cut = 0xFFFFFFFFu;
    t.done = 0xFFFFFFFFu;
    t.open = 0xFFFFFFFFu;
    t.headUs = 0xFFFFFFFFu;
    t.restUs = 0xFFFFFFFFu;
    StageTime* const stages[3] = { &t.take, &t.obj, &t.give };
    for (StageTime* s : stages)
    {
        s->ran = true;
        s->fetchUs = s->sortUs = s->loopUs = s->walk = s->ret = 0xFFFFFFFFu;
    }
    char buf[kPlanTimeBuf];
    std::size_t const len = FormatPlanTimeLine(buf, sizeof(buf), t);
    Require(len + 1 < sizeof(buf), "every field at its maximum fits kPlanTimeBuf");
    Require(std::strstr(buf, " rest=4294967295") != nullptr, "the last field is not cut");

    // S5 F1: the prefilter suffix at its maximum fits too
    t.givePrefilter = true;
    t.givePre = t.giveXmap = t.giveInbox = 0xFFFFFFFFu;
    t.giveVerify = true;
    char pbuf[kPlanTimeBuf];
    std::size_t const plen = FormatPlanTimeLine(pbuf, sizeof(pbuf), t);
    Require(plen + 1 < sizeof(pbuf), "every field and the prefilter suffix at their maximum fit kPlanTimeBuf");
    Require(std::strstr(pbuf, " give_inbox=4294967295 verify=1") != nullptr, "the prefilter suffix is not cut");

    // S5 F1-IX: the index suffix at its maximum fits too
    t.giveIx = true;
    t.giveIxAll = t.giveIxMaps = t.giveIxPortals = t.giveIxBuckets = t.giveIxWide = 0xFFFFFFFFu;
    t.giveIxPortalUs = t.giveIxCollectUs = t.giveIxSortUs = t.giveIxAppendUs = 0xFFFFFFFFu;
    char xbuf[kPlanTimeBuf];
    std::size_t const xlen = FormatPlanTimeLine(xbuf, sizeof(xbuf), t);
    Require(xlen + 1 < sizeof(xbuf), "every field, the prefilter and the index suffix at their maximum fit kPlanTimeBuf");
    Require(std::strstr(xbuf, " give_ixt=4294967295/4294967295/4294967295/4294967295") != nullptr, "the index suffix is not cut");
    t.giveIx = false;

    char small[16];
    std::size_t const cut = FormatPlanTimeLine(small, sizeof(small), t);
    Require(cut == sizeof(small) - 1 && std::strlen(small) == cut, "a short buffer cuts and terminates");
    Require(FormatPlanTimeLine(nullptr, 8, t) == 0 && FormatPlanTimeLine(small, 0, t) == 0, "no buffer = 0");
}
}

namespace
{
// S5 F1 (twow-repo#726, F1-ENTWURF-v0.2): with the giver prefilter the line ends with its counts; without it the line is as before.
void TestPrefilterSuffix()
{
    PlanTime t;
    t.guid = 7;
    t.give.ran = true;
    t.give.fetchUs = 410;
    t.give.walk = 6118;
    t.give.ret = 164;
    t.givePrefilter = true;
    t.givePre = 5800;
    t.giveXmap = 3100;
    t.giveInbox = 2;
    char buf[kPlanTimeBuf];
    std::size_t const len = FormatPlanTimeLine(buf, sizeof(buf), t);
    std::string const line(buf);
    Require(len == line.size(), "prefilter: returned length = string length");
    Require(line == "[PlanTime] bot=7 cohort=A us=0 cpu_us=na ms=0 over_budget=0 cands=0 cut=0 done=0 open=0 head=0 "
        "take=- obj=- give=410/0/0/6118/164 rest=0 prefilter=1 give_pre=5800 give_xmap=3100 give_inbox=2 verify=0", "prefilter suffix");
    t.giveVerify = true;
    FormatPlanTimeLine(buf, sizeof(buf), t);
    Require(std::string(buf).size() > 9 && std::string(buf).substr(std::string(buf).size() - 9) == " verify=1", "verify=1 last");
    t.givePrefilter = false;
    FormatPlanTimeLine(buf, sizeof(buf), t);
    Require(std::strstr(buf, "prefilter=") == nullptr && std::string(buf).substr(std::string(buf).size() - 7) == " rest=0",
        "prefilter off: the line ends at rest= as before");
    char small[40];
    t.givePrefilter = true;
    std::size_t const cut = FormatPlanTimeLine(small, sizeof(small), t);
    Require(cut == sizeof(small) - 1 && std::strlen(small) == cut, "prefilter: a short buffer cuts and terminates");
}
}

namespace
{
// S5 F1-IX (twow-repo#726, IX-ENTWURF-v0.2): an index plan ends with ix=1 and its counts after the F1 part; a fallback with
// give_ixmiss=1 / give_ixstale=1; without the index the line is as before (OB-10 B3: key 0 leaves give= and the line unchanged).
void TestIndexSuffix()
{
    PlanTime t;
    t.guid = 9;
    t.give.ran = true;
    t.give.fetchUs = 310;
    t.give.walk = 420;
    t.give.ret = 160;
    char buf[kPlanTimeBuf];
    FormatPlanTimeLine(buf, sizeof(buf), t);
    std::string const plain(buf);
    t.givePrefilter = true;
    t.givePre = 250;
    t.giveIx = true;
    t.giveIxAll = 6111;
    t.giveIxMaps = 3;
    t.giveIxPortals = 2;
    t.giveIxBuckets = 63;
    t.giveIxWide = 1;
    t.giveIxPortalUs = 40;
    t.giveIxCollectUs = 20;
    t.giveIxSortUs = 5;
    t.giveIxAppendUs = 230;
    std::size_t const len = FormatPlanTimeLine(buf, sizeof(buf), t);
    std::string const line(buf);
    Require(len == line.size(), "index: returned length = string length");
    Require(line == "[PlanTime] bot=9 cohort=A us=0 cpu_us=na ms=0 over_budget=0 cands=0 cut=0 done=0 open=0 head=0 "
        "take=- obj=- give=310/0/0/420/160 rest=0 prefilter=1 give_pre=250 give_xmap=0 give_inbox=0 verify=0 ix=1 give_ixall=6111 "
        "give_ixmaps=3 give_ixportals=2 give_ixbuckets=63 give_ixwide=1 give_ixt=40/20/5/230", "index suffix");
    t.giveIx = false;
    t.giveIxMiss = true;
    FormatPlanTimeLine(buf, sizeof(buf), t);
    Require(std::string(buf).substr(std::string(buf).size() - 14) == " give_ixmiss=1", "miss last");
    t.giveIxStale = true;
    FormatPlanTimeLine(buf, sizeof(buf), t);
    Require(std::string(buf).substr(std::string(buf).size() - 15) == " give_ixstale=1", "stale last");
    PlanTime off;
    off.guid = 9;
    off.give = t.give;
    FormatPlanTimeLine(buf, sizeof(buf), off);
    Require(std::string(buf) == plain && std::strstr(buf, "ix") == nullptr, "index off: the line is as before");
}
}

int main()
{
    TestValidTimeTrace();
    TestUsBetween();
    TestLineShape();
    TestBufferBound();
    TestPrefilterSuffix();
    TestIndexSuffix();
    std::cout << "planner_time_policy_tests: OK\n";
    return 0;
}
