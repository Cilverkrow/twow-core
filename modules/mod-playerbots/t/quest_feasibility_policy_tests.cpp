// twow-repo#601 S3a (AiPlayerbot.QuestFeasibility.*, design rev. 2, OB-10 / OB-00 18.10.2026): the id list parser, the
// skip decision (lists, the conditional list while CustomRaceStartZones = 0, the solo group rule with Type 1 / 81 / 62 and
// SuggestedPlayers), scope and class exemption, giver and drop rules and the bounded skip lines (QuestFeasibilityPolicy.h).
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "QuestFeasibilityPolicy.h"

#include <cstddef>
#include <cstdint>
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

bool Same(char const* a, char const* b)
{
    return a && b && std::strcmp(a, b) == 0;
}

// A roster bot on its own, switch on, nothing listed, no rule: the base every case changes one thing of.
ai::quest_feasibility::Facts Base()
{
    ai::quest_feasibility::Facts facts;
    facts.enabled = true;
    facts.rosterOnItsOwn = true;
    return facts;
}
}

int main()
{
    using namespace ai::quest_feasibility;

    // 1) Parser: trim, decimal ids > 0 only, duplicates dropped, at most MaxIds.
    {
        Require(ParseQuestIdList("").empty(), "empty text: no ids");
        Require(ParseQuestIdList(" , ,\t").empty(), "blanks only: no ids");
        std::set<std::uint32_t> const ids = ParseQuestIdList("62, 76,984 ,\t870\t");
        Require(ids == std::set<std::uint32_t>({62, 76, 870, 984}), "trimmed decimal ids");
        Require(ParseQuestIdList("41169,41170,41169").size() == 2, "duplicates dropped");
        Require(ParseQuestIdList("0,-5,12a,a12,1.5,+7, 3 4").empty(), "zero, signs, letters, decimals, inner blanks skipped");
        Require(ParseQuestIdList("4294967295") == std::set<std::uint32_t>({4294967295u}), "largest uint32 kept");
        Require(ParseQuestIdList("4294967296,99999999999").empty(), "above uint32 skipped (no wrap)");
        Require(ParseQuestIdList("00042") == std::set<std::uint32_t>({42}), "leading zeros read as decimal");
        Require(ParseQuestIdList("5,,6") == std::set<std::uint32_t>({5, 6}), "empty token between commas skipped");

        std::string many;
        for (std::uint32_t id = 1; id <= 600; ++id)
            many += std::to_string(id) + ",";
        std::set<std::uint32_t> const capped = ParseQuestIdList(many);
        Require(capped.size() == MaxIds && MaxIds == 512, "cap at 512 ids");
        Require(capped.count(512) == 1 && capped.count(513) == 0, "the first 512 ids are kept");
        std::string dupes;
        for (int i = 0; i < 1000; ++i)
            dupes += "7,";
        Require(ParseQuestIdList(dupes + "8") == std::set<std::uint32_t>({7, 8}), "duplicates do not use up the cap");
    }

    // 2) Switch off, scope: nothing else is looked at. Class quests: the explicit lists apply (review S3a F2, OB-00 Q-O3
    //    lists 80330 / 60163 / 41201), only the generic solo group rule spares them.
    {
        Facts facts = Base();
        facts.listed = true;
        facts.soloSkipGroupQuests = true;
        facts.type = TypeElite;
        Require(SkipReason(facts) == Reason::Blocklist, "base: listed quest skipped");

        Facts off = facts;
        off.enabled = false;
        off.classQuest = true;
        Require(SkipReason(off) == Reason::None, "Enable 0: never a skip");

        Facts led = facts;
        led.rosterOnItsOwn = false;
        led.classQuest = true;
        Require(SkipReason(led) == Reason::None, "not a roster bot on its own (real master / not on the roster): untouched");

        Facts cls = facts;
        cls.classQuest = true;
        Require(SkipReason(cls) == Reason::Blocklist, "class quest on the plain list: skipped (explicit id)");
        cls.listed = false;
        cls.listedNoCustomStarts = true;
        Require(SkipReason(cls) == Reason::BlocklistCrsz0, "class quest on the conditional list: skipped (60163 goblin hunter)");
        cls.customRaceStartZones = true;
        Require(SkipReason(cls) == Reason::None, "class quest on the conditional list, CustomRaceStartZones 1: taken");
        Require(SkipReason(Base()) == Reason::None, "nothing listed, no rule: no skip");
    }

    // 3) Lists: the conditional list only while CustomRaceStartZones = 0.
    {
        Facts facts = Base();
        facts.listedNoCustomStarts = true;
        Require(SkipReason(facts) == Reason::BlocklistCrsz0, "conditional list with CustomRaceStartZones 0");
        facts.customRaceStartZones = true;
        Require(SkipReason(facts) == Reason::None, "conditional list lifts with CustomRaceStartZones 1");
        facts.listed = true;
        Require(SkipReason(facts) == Reason::Blocklist, "the plain list stays with CustomRaceStartZones 1");
        facts.customRaceStartZones = false;
        Require(SkipReason(facts) == Reason::Blocklist, "both lists: the plain list wins (reason=blocklist)");
    }

    // 4) Solo group rule: Type 1 / 81 / 62 or SuggestedPlayers >= 2, bot not in a group, only with the key.
    {
        Require(IsGroupQuest(TypeElite, 0) && IsGroupQuest(TypeDungeon, 0) && IsGroupQuest(TypeRaid, 0), "types 1 / 81 / 62");
        Require(TypeElite == 1 && TypeDungeon == 81 && TypeRaid == 62, "core quest type numbers");
        Require(IsGroupQuest(0, 2) && IsGroupQuest(0, 5), "SuggestedPlayers >= 2");
        Require(!IsGroupQuest(0, 0) && !IsGroupQuest(0, 1), "SuggestedPlayers 0 / 1 is no group quest");
        Require(!IsGroupQuest(41, 0) && !IsGroupQuest(84, 0) && !IsGroupQuest(21, 1), "PvP 41, escort 84, others: no group quest");

        Facts facts = Base();
        facts.type = TypeElite;
        Require(SkipReason(facts) == Reason::None, "SoloSkipGroupQuests 0: elite quest taken");
        facts.soloSkipGroupQuests = true;
        Require(SkipReason(facts) == Reason::GroupSolo, "solo bot: Type 1 skipped");
        facts.type = TypeDungeon;
        Require(SkipReason(facts) == Reason::GroupSolo, "solo bot: Type 81 skipped (OB-00 Q-O1)");
        facts.type = TypeRaid;
        Require(SkipReason(facts) == Reason::GroupSolo, "solo bot: Type 62 skipped (OB-00 Q-O1)");
        facts.type = 0;
        facts.suggestedPlayers = 2;
        Require(SkipReason(facts) == Reason::GroupSolo, "solo bot: SuggestedPlayers 2 skipped");
        facts.suggestedPlayers = 1;
        Require(SkipReason(facts) == Reason::None, "solo bot: SuggestedPlayers 1 taken");
        facts.suggestedPlayers = 3;
        facts.inGroup = true;
        Require(SkipReason(facts) == Reason::None, "a bot in a group takes group quests");
        facts.classQuest = true;
        facts.inGroup = false;
        Require(SkipReason(facts) == Reason::None, "a class group quest stays (class exemption: solo group rule only)");
        facts.listed = true;
        Require(SkipReason(facts) == Reason::Blocklist, "a listed class group quest is skipped (reason=blocklist)");
    }

    // 5) Giver relation: every skip reason (review S3a F3: a solo bot does not walk to a group quest's giver to decline);
    //    drop: open quests only, both list reasons map to infeasible_blocklist, paths E / F are not undone (review S3a F2).
    {
        Require(GiverInactive(Reason::Blocklist) && GiverInactive(Reason::BlocklistCrsz0), "giver of a listed quest inactive");
        Require(GiverInactive(Reason::GroupSolo), "giver of a solo bot's group quest inactive (Type 0, SP >= 2 too)");
        Require(!GiverInactive(Reason::None), "feasible quest: giver unchanged");

        Require(Same(DropReasonName(Reason::Blocklist, false, false, false), "infeasible_blocklist"), "open listed quest dropped");
        Require(Same(DropReasonName(Reason::BlocklistCrsz0, false, false, false), "infeasible_blocklist"),
            "open conditional quest dropped");
        Require(Same(DropReasonName(Reason::GroupSolo, false, false, false), "infeasible_group_solo"),
            "open group quest of a solo bot dropped");
        Require(!DropReasonName(Reason::Blocklist, true, false, false), "complete listed quest kept (turn-in allowed, OB-10 rev. 2)");
        Require(!DropReasonName(Reason::GroupSolo, true, false, false), "complete group quest kept (OB-00 Q-O2)");
        Require(!DropReasonName(Reason::None, false, false, false), "feasible quest kept");
        // E: a listed escort a group member's confirm brought in stays while the bot is in a group.
        Require(!DropReasonName(Reason::Blocklist, false, true, false), "listed quest kept while in a group (escort confirm, E)");
        Require(!DropReasonName(Reason::BlocklistCrsz0, false, true, false), "conditional quest kept while in a group (E)");
        // F: the guild quest order's quest is never dropped (no accept / drop churn per cleanup).
        Require(!DropReasonName(Reason::Blocklist, false, false, true), "listed guild order quest kept (F)");
        Require(!DropReasonName(Reason::GroupSolo, false, false, true), "group-type guild order quest kept (F)");
    }

    // 6) Names and lines.
    {
        Require(Same(ReasonName(Reason::Blocklist), "blocklist") && Same(ReasonName(Reason::BlocklistCrsz0), "blocklist_crsz0") &&
            Same(ReasonName(Reason::GroupSolo), "group_solo") && Same(ReasonName(Reason::None), "none"), "reason names");
        Require(Same(PathName(Path::Accept), "accept") && Same(PathName(Path::Item), "item") && Same(PathName(Path::Giver), "giver"),
            "path names");
        Require(FormatSkipLine(216, 12, 41169, Reason::BlocklistCrsz0, Path::Giver) ==
            "[QuestFeasibility] state=skip bot=216 level=12 quest=41169 reason=blocklist_crsz0 path=giver", "skip line format");
        Require(FormatMinuteLine(3, 2, 7) == "[QuestFeasibility] minute skips=3 drops=2 giver_inactive=7", "minute line format");
    }

    // 7) Bounded skip lines: once per (bot, quest) an hour, at most LinesPerMinute per bot and minute, 16-quest ring.
    {
        SkipLog log;
        Require(!LogDue(log, 0, Path::Accept, 1000), "quest 0 never logs");
        Require(LogDue(log, 100, Path::Accept, 1000), "first skip of a quest logs");
        Require(!LogDue(log, 100, Path::Accept, 1001), "same quest right after: silent");
        Require(!LogDue(log, 100, Path::Accept, 1000 + RepeatSeconds - 1), "same quest within the hour: silent");
        Require(LogDue(log, 100, Path::Accept, 1000 + RepeatSeconds), "same quest after an hour: logs again");

        // Keyed by path (review S3a F5): a giver line does not silence the same quest's accept line, and back.
        SkipLog paths;
        Require(LogDue(paths, 41169, Path::Giver, 2000), "giver line logs");
        Require(LogDue(paths, 41169, Path::Accept, 2001), "accept line of the same quest still logs");
        Require(LogDue(paths, 41169, Path::Item, 2002), "item line of the same quest still logs");
        Require(!LogDue(paths, 41169, Path::Giver, 2003) && !LogDue(paths, 41169, Path::Accept, 2003) &&
            !LogDue(paths, 41169, Path::Item, 2003), "each (quest, path) silent within its hour");

        SkipLog burst;
        std::uint32_t logged = 0;
        for (std::uint32_t quest = 1; quest <= 20; ++quest)
            logged += LogDue(burst, quest, Path::Accept, 5000) ? 1u : 0u;
        Require(logged == LinesPerMinute && LinesPerMinute == 4, "at most 4 lines per bot and minute");
        Require(LogDue(burst, 5, Path::Accept, 5060), "next minute: a quest not yet logged logs");
        Require(!LogDue(burst, 1, Path::Accept, 5060), "a quest logged in the burst stays silent within its hour");

        // Ring: 16 distinct quests over many minutes - each logs once; the 17th evicts the oldest entry.
        SkipLog ring;
        std::uint32_t now = 10000;
        for (std::uint32_t quest = 1; quest <= LogRingSize; ++quest, now += 60)
            Require(LogDue(ring, quest, Path::Accept, now), "ring: distinct quest logs (one per minute)");
        for (std::uint32_t quest = 1; quest <= LogRingSize; ++quest)
            Require(!LogDue(ring, quest, Path::Accept, now), "ring: all 16 remembered");
        Require(LogDue(ring, 999, Path::Accept, now), "ring: 17th quest logs");
        Require(LogDue(ring, 1, Path::Accept, now + 60), "ring: the evicted oldest quest may log again (bounded memory, not silence)");
        Require(!LogDue(ring, 3, Path::Accept, now + 60), "ring: the other entries stay");
        Require(LogRingSize == 16, "ring size 16");

        SkipLog back;
        Require(LogDue(back, 7, Path::Accept, 9000), "clock: first line");
        Require(LogDue(back, 7, Path::Accept, 100), "clock going back: logs (never stuck silent), bounded by the minute cap");
    }

    std::cout << "quest_feasibility_policy_tests: all passed\n";
    return 0;
}
