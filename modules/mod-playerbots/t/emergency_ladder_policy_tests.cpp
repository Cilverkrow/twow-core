// twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): decision table, escape window, solo CC rules, spell list parsing, CC-broken
// rule and the exact [EmergencyLadder] lines (EmergencyLadderPolicy.h). Hand-rolled Require(), std::exit(1) on failure, no gtest.
#ifdef _WIN32
#include <windows.h>
#endif
#include "EmergencyLadderPolicy.h"
#include "CohortPolicy.h"

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

ai::emergency_ladder::Situation Nearly(std::uint32_t targetHp)
{
    ai::emergency_ladder::Situation s;
    s.hasTarget = true;
    s.targetHp = targetHp;
    s.attackers = 1;
    s.fightTargetHp = 20;
    return s;
}
}

int main()
{
    using namespace ai::emergency_ladder;

    // 1) Decision table: a ready escape first, then fight, then flee.
    {
        Require(Decide(Nearly(15)) == Choice::Fight, "single normal target at 15 %: fight");
        Require(Decide(Nearly(20)) == Choice::Fight, "at the threshold: fight");
        Require(Decide(Nearly(21)) == Choice::Flee, "above the threshold, no escape: flee");
        Situation s = Nearly(21); s.escapeReady = true;
        Require(Decide(s) == Choice::Escape, "above the threshold with an escape: escape");
        s = Nearly(10); s.escapeReady = true;
        Require(Decide(s) == Choice::Escape, "a ready escape comes first: its critical-health node fires anyway (review 18.10.)");
        s = Nearly(10); s.attackers = 2;
        Require(Decide(s) == Choice::Flee, "two attackers: no fight");
        s.escapeReady = true;
        Require(Decide(s) == Choice::Escape, "two attackers with an escape: escape");
        s = Nearly(10); s.targetElite = true;
        Require(Decide(s) == Choice::Flee, "elite target: no fight");
        s = Nearly(10); s.targetPlayer = true;
        Require(Decide(s) == Choice::Flee, "player target: no fight");
        s = Nearly(10); s.levelGap = 3;
        Require(Decide(s) == Choice::Flee, "target 3 levels up: no fight");
        s.levelGap = 2;
        Require(Decide(s) == Choice::Fight, "target 2 levels up: fight");
        s = Nearly(10); s.hasTarget = false;
        Require(Decide(s) == Choice::Flee, "no target: flee");
        s = Nearly(10); s.attackers = 0;
        Require(Decide(s) == Choice::Fight, "attacker count 0 (cache lag) still counts as single");
        s = Nearly(25); s.fightTargetHp = 30;
        Require(Decide(s) == Choice::Fight, "configurable threshold (F2)");
    }

    // 2) A fight stays a fight only while its conditions hold.
    {
        Situation s = Nearly(10);
        Require(FightStillHolds(s), "still nearly dead");
        s.attackers = 2;
        Require(!FightStillHolds(s), "a second mob joined");
        s = Nearly(40);
        Require(!FightStillHolds(s), "the target healed");
        s = Nearly(10); s.escapeReady = true;
        Require(FightStillHolds(s), "a fight already running is not ended by an escape getting ready");
        Require(!FightStillHolds(Situation()), "no target: the fight is over");
    }

    // 3) Escape window: 2 s, also across a millisecond-timer wrap.
    {
        Require(EscapeWindowOpen(1000, 1000) && EscapeWindowOpen(2999, 1000), "inside the window");
        Require(!EscapeWindowOpen(3000, 1000), "window over after 2 s");
        Require(EscapeWindowOpen(500, 0xFFFFFF00u), "wrap: 756 ms after the start");
        Require(!EscapeWindowOpen(5000, 0xFFFFFF00u), "wrap: 5256 ms after the start");
    }

    // 3b) Episode gap: more than 10 s without panic seen ends the episode (combat end, config reload).
    {
        Require(!EpisodeStale(11000, 1000) && EpisodeStale(11001, 1000), "10 s gap");
        Require(!EpisodeStale(6500, 1000), "a 5 s channel plus the GCD stays one episode (recheck 18.10.)");
        Require(!EpisodeStale(100, 0xFFFFFF00u), "wrap: 356 ms");
    }

    // 4) Spell list (F3).
    {
        auto l = ParseSpellList("polymorph, Banish ,fear");
        Require(l.size() == 3 && l[0] == "polymorph" && l[1] == "banish" && l[2] == "fear", "parsed and lower case");
        Require(ParseSpellList("").empty() && ParseSpellList(" , ,").empty(), "empty items dropped");
        Require(SpellAllowed("fear", l) && !SpellAllowed("sap", l), "allowed spells");
        Require(!SpellAllowed("polymorph", ParseSpellList("")), "empty list allows nothing");
    }

    // 5) Solo CC candidate.
    {
        CcCandidate c; c.attacksBotOrPet = true;
        Require(SoloCcEligible(c), "normal add attacking the bot");
        CcCandidate e = c; e.elite = true; Require(!SoloCcEligible(e), "elite");
        CcCandidate p = c; p.player = true; Require(!SoloCcEligible(p), "player");
        CcCandidate n = c; n.attacksBotOrPet = false; Require(!SoloCcEligible(n), "not attacking the bot or its pet");
        CcCandidate d = c; d.hasDot = true; Require(!SoloCcEligible(d), "a DoT would break the CC");
        CcCandidate t = c; t.currentTargetHp = 30; Require(!SoloCcEligible(t), "own target at 30 %: finish it first");
        t.currentTargetHp = 31; Require(SoloCcEligible(t), "own target above 30 %");
    }

    // 6) CC broken = gone within 8 s.
    {
        Require(CcBroken(1000, 8999) && !CcBroken(1000, 9000), "8 s rule");
        Require(CcBroken(0xFFFFF000u, 100), "wrap");
    }

    // 7) Lines.
    {
        Situation s = Nearly(12); s.levelGap = -1; s.attackers = 1;
        RequireLine(FormatEpisodeLine(4711, "Morwenna", 1, 17, 'B', true, "panic", Choice::Fight, nullptr, 18, -1, s),
            "[EmergencyLadder] bot=4711 name=Morwenna cls=1 level=17 cohort=B applied=1 trigger=panic choice=fight escape=- hp=18 mana=-1 "
            "target_hp=12 target_gap=-1 target_elite=0 target_player=0 attackers=1", "episode line");
        Situation n; n.attackers = 2;
        RequireLine(FormatEpisodeLine(8, "Kettlina", 8, 31, 'A', false, "panic", Choice::Escape, "ice block", 9, 4, n),
            "[EmergencyLadder] bot=8 name=Kettlina cls=8 level=31 cohort=A applied=0 trigger=panic choice=escape escape=ice block hp=9 mana=4 "
            "target_hp=0 target_gap=0 target_elite=0 target_player=0 attackers=2", "episode line without target");
        RequireLine(FormatSoloCcLine(8, "Kettlina", 8, 'B', "polymorph", 3127, 2),
            "[EmergencyLadder] solo-cc bot=8 name=Kettlina cls=8 cohort=B spell=polymorph target=3127 attackers=2", "solo cc line");
        Counters c; c.episodes = 5; c.flee = 2; c.escape = 2; c.fight = 1; c.escapeFallback = 1; c.soloCc = 3; c.ccBroken = 1;
        RequireLine(FormatTotalLine(60, 'B', 4, c, 7),
            "[EmergencyLadderTotal] window_min=60 cohort=B cls=4 episodes=5 flee=2 escape=2 fight=1 escape_fallback=1 fight_abort=0 "
            "solo_cc=3 cc_broken=1 dropped=7", "total line");
        Require(Counters().Empty() && !c.Empty(), "empty counters");
        std::string const longName(200, 'x');
        Require(FormatEpisodeLine(1, longName, 1, 1, 'A', true, "panic", Choice::Flee, nullptr, 1, 1, s).find(std::string(41, 'x')) ==
            std::string::npos, "name cut to 40");
    }

    // 8) Cohort wiring as the triggers use it: mode 2 applies only to odd guids.
    {
        Require(!ai::cohort::Applies(0, 1) && ai::cohort::Applies(1, 2) && ai::cohort::Applies(2, 3) && !ai::cohort::Applies(2, 4), "cohorts");
    }

    std::cout << "emergency_ladder_policy_tests passed\n";
    return 0;
}
