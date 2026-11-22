#include "RescueValleyPolicy.h"
#include "QuestSearchPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// twow-repo#575 R2 (AiPlayerbot.QuestRescue.StartValleyFix): the start-valley candidate rules of RescueValleyPolicy.h - own race only (a),
// dedupe + last target without progress (b), the rescue distance (c) - for human, dwarf, gnome and goblin (OB-10 22.11.2026) - and the
// bot thread's progress memory: each progress type (level, XP, gathering skill, quest log) lifts the exclusion, no progress keeps it.
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

using ai::rescue_valley::Drop;
using ai::rescue_valley::Point;

// Start points (playercreateinfo, vanilla): human Northshire; dwarf and gnome share Coldridge Valley (the 565 target).
constexpr std::uint32_t Human = 1, Dwarf = 3, NightElf = 4, Gnome = 7, Goblin = 9;
Point const Northshire{ 0, -8949.95f, -132.493f };
Point const ColdridgeDwarf{ 0, -6240.32f, 331.033f };
Point const ColdridgeGnome{ 0, -6240.32f, 331.033f };
Point const Teldrassil{ 1, 10311.3f, 832.463f };

float Dist(Point const& a, Point const& b)
{
    return a.map == b.map ? std::sqrt(ai::rescue_valley::Dist2dSq(a, b)) : 1.0e9f;
}

// The candidate pass of QuestRescueTargets with the key: races of the faction in order, own race only with the fix.
struct Pass
{
    std::vector<Point> targets;
    ai::rescue_valley::Tally tally;
};

Pass Run(bool fix, std::uint32_t botRace, Point const& bot, bool hasLast, Point const& last)
{
    struct Start { std::uint32_t race; Point point; };
    std::vector<Start> const alliance{ { Human, Northshire }, { Dwarf, ColdridgeDwarf }, { NightElf, Teldrassil }, { Gnome, ColdridgeGnome } };
    Pass pass;
    std::vector<Point> kept;
    for (Start const& s : alliance)
    {
        if (!ai::rescue_valley::TakesValleyOf(fix, s.race, botRace))
            continue;
        Drop const d = ai::rescue_valley::Check(s.point, Dist(bot, s.point), ai::quest_search::RescueMinDistance, kept, hasLast, last);
        pass.tally.Add(d);
        if (d != Drop::Kept)
            continue;
        kept.push_back(s.point);
        pass.targets.push_back(s.point);
    }
    return pass;
}
}

int main()
{
    using namespace ai::rescue_valley;
    Point const none{ 0, 0.0f, 0.0f };

    // 1. (a) own race only.
    {
        Require(TakesValleyOf(false, Dwarf, Human) && TakesValleyOf(false, Gnome, Human), "key 0: the other races' valleys as before");
        Require(!TakesValleyOf(true, Dwarf, Human) && !TakesValleyOf(true, Gnome, Human) && !TakesValleyOf(true, NightElf, Human),
            "fix: a human never gets a dwarf / gnome / night-elf valley");
        Require(TakesValleyOf(true, Human, Human) && TakesValleyOf(true, Dwarf, Dwarf), "fix: the own race's valley stays");
    }

    // 2. Human 565 (DAUER-B): L8 standing in Coldridge. Legacy: Coldridge twice (dwarf + gnome) plus Teldrassil; fix: Northshire only.
    {
        Point const bot565 = ColdridgeDwarf;
        Pass const legacy = Run(false, Human, bot565, false, none);
        Require(legacy.tally.nearby == 2, "the legacy view (with the distance only for the count): both Coldridge points are near the bot");
        Pass const fixed = Run(true, Human, bot565, false, none);
        Require(fixed.targets.size() == 1 && SamePoint(fixed.targets[0], Northshire), "fix: human 565 is sent to Northshire, never Coldridge");
        Require(fixed.tally.kept == 1 && fixed.tally.nearby == 0 && fixed.tally.duplicate == 0 && fixed.tally.last == 0, "fix: counts");
    }

    // 3. (b) last target without progress: a human rescued to Northshire before, no progress since -> no target (honest stop).
    {
        Pass const again = Run(true, Human, ColdridgeDwarf, true, Northshire);
        Require(again.targets.empty() && again.tally.last == 1, "fix: the last target is excluded while there was no progress");
        Pass const progressed = Run(true, Human, ColdridgeDwarf, false, Northshire);
        Require(progressed.targets.size() == 1, "fix: after progress (memory cleared) the own valley is a target again");
        Require(Check(Northshire, 5000.0f, ai::quest_search::RescueMinDistance, {}, true, Point{ 0, -8949.95f + 30.0f, -132.493f }) == Drop::Last,
            "a last target within DedupeYards counts as the same point");
        Require(Check(Northshire, 5000.0f, ai::quest_search::RescueMinDistance, {}, true, Point{ 1, -8949.95f, -132.493f }) == Drop::Kept,
            "the same coordinates on another map are another point");
    }

    // 4. (c) the rescue distance: a human standing in Northshire gets no target (it is there already).
    {
        Pass const home = Run(true, Human, Northshire, false, none);
        Require(home.targets.empty() && home.tally.nearby == 1, "fix: a human in Northshire -> near, no target");
        Require(Check(Northshire, ai::quest_search::RescueMinDistance, ai::quest_search::RescueMinDistance, {}, false, none) == Drop::Near,
            "exactly at the minimum distance is near (as the other branches: > min is a target)");
    }

    // 5. Dwarf and gnome: their own valley is Coldridge; both kept from afar, near when standing there.
    {
        Pass const dwarf = Run(true, Dwarf, Northshire, false, none);
        Require(dwarf.targets.size() == 1 && SamePoint(dwarf.targets[0], ColdridgeDwarf), "fix: a dwarf away from home -> Coldridge");
        Pass const gnome = Run(true, Gnome, Northshire, false, none);
        Require(gnome.targets.size() == 1 && SamePoint(gnome.targets[0], ColdridgeGnome), "fix: a gnome away from home -> Coldridge");
        Pass const gnomeHome = Run(true, Gnome, ColdridgeGnome, false, none);
        Require(gnomeHome.targets.empty() && gnomeHome.tally.nearby == 1, "fix: a gnome in Coldridge -> no target");
    }

    // 6. Dedupe: the dwarf and gnome start points are one rescue target (legacy list, no fix, bot far away).
    {
        std::vector<Point> kept{ ColdridgeDwarf };
        Require(Check(ColdridgeGnome, 5000.0f, ai::quest_search::RescueMinDistance, kept, false, none) == Drop::Duplicate,
            "gnome Coldridge = dwarf Coldridge -> duplicate");
        Require(SamePoint(ColdridgeDwarf, ColdridgeGnome) && !SamePoint(ColdridgeDwarf, Northshire), "SamePoint");
    }

    // 7. Goblin: the goblin branch comes before the start-valley branch (L < 11); (c) gives its two Durotar pushes the rescue distance.
    {
        namespace gi = ai::quest_search::goblin_island;
        Require(ai::quest_search::goblin_island::UsesGoblinStartRescue(Goblin, 2), "goblin L2 takes the goblin branch, not the start valleys");
        Point const razorHill{ 1, gi::RazorHillX, gi::RazorHillY };
        Point const port{ 1, gi::PortMachineX, gi::PortMachineY };
        Require(Dist(razorHill, port) > ai::quest_search::RescueMinDistance - 600.0f && !SamePoint(razorHill, port),
            "Razor Hill and Sparkwater are two points");
        Point const bot2293 = razorHill;   // standing at Razor Hill after a rescue
        Require(Dist(bot2293, razorHill) <= ai::quest_search::RescueMinDistance, "goblin at Razor Hill: Razor Hill is near (dropped)");
        Require(Dist(bot2293, port) <= ai::quest_search::RescueMinDistance,
            "goblin at Razor Hill: Sparkwater (~560 yd) is near too -> no Durotar push (no ping-pong)");
    }

    // 8. Names.
    {
        Require(DropName(Drop::Kept)[0] == 'k' && DropName(Drop::Near)[0] == 'n' && DropName(Drop::Duplicate)[0] == 'd' &&
            DropName(Drop::Last)[0] == 'l', "drop names");
        Require(std::string(MovedName(Moved::None)) == "none" && std::string(MovedName(Moved::QuestLog)) == "quest_log", "moved names");
    }

    // 9. (b) progress since the rescue (OB-10 22.11.2026): each progress type lifts the exclusion, no progress keeps it.
    {
        Progress const base{ 8, 1200, 0, 0x1234u };

        // 9a. No progress: the baseline check and every later check with the same values keep the exclusion.
        {
            Memory m;
            Require(m.Observe(base) == Moved::None, "the baseline check itself is no progress");
            Require(m.Observe(base) == Moved::None && m.Observe(base) == Moved::None, "no progress: the exclusion stays");
        }
        // 9b-9e. One progress type at a time.
        auto lifted = [&](Progress now, Moved expected, char const* message)
        {
            Memory m;
            m.Observe(base);
            Require(m.Observe(now) == expected, message);
        };
        Progress level = base; level.level = 9; level.xp = 0;
        lifted(level, Moved::Level, "a level-up lifts the exclusion");
        Progress xp = base; xp.xp = 1260;
        lifted(xp, Moved::Xp, "XP lifts the exclusion");
        Progress gather = base; gather.gather = 75ull * (182ull * 2654435761ULL + 1ULL);
        lifted(gather, Moved::Gather, "a gathering skill-up (under a purpose) lifts the exclusion");
        Progress quest = base; quest.questState = 0x9876u;
        lifted(quest, Moved::QuestLog, "a quest taken / finished / handed in / dropped lifts the exclusion");

        // 9f. A new rescue restarts the baseline: progress before it does not count after it.
        {
            Memory m;
            m.Observe(base);
            m.Restart();
            Require(m.Observe(xp) == Moved::None, "after Restart the next check is the new baseline");
            Require(m.Observe(xp) == Moved::None, "and no progress after it keeps the exclusion");
            Require(m.Observe(quest) == Moved::Xp, "progress against the new baseline (XP back to the old value moved too)");
        }
        // 9g. The order of the reported part: level before XP before gathering before the quest log.
        {
            Progress all{ 9, 0, 1, 1 };
            Require(ProgressSince(base, all) == Moved::Level, "several parts moved: level is reported");
        }
    }

    std::cout << "rescue_valley_policy: all checks passed\n";
    return 0;
}
