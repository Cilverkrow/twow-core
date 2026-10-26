#include "ReachDropPolicy.h"

#include <cstdlib>
#include <iostream>

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
    using namespace ai::reach_drop;

    // twow-repo#630 follow-up (owner repro 25.10.2026): what the drop clears besides "current target".
    Held none;
    Require(!none.Any(), "nothing held: nothing to clear, no line");
    Held attack;
    attack.attackTarget = true;
    Require(attack.Any(), "the attack command's target alone kept the rogue in combat");
    Held pull;
    pull.pullTarget = true;
    Require(pull.Any(), "a pull on the target");
    Held swing;
    swing.victim = true;
    Require(swing.Any(), "a running swing");

    // The rogue's stealth while it follows a real player.
    FollowFacts f;
    f.stealthed = true;
    f.nonCombat = true;
    f.follows = true;
    f.masterMoving = true;
    Require(UnstealthFollowing(f), "stealthed, following outside combat, the player moves: unstealth");

    FollowFacts standing = f;
    standing.masterMoving = false;
    Require(!UnstealthFollowing(standing), "standing together: stealth stays (it does not re-stealth there)");
    FollowFacts behind = standing;
    behind.catchingUp = true;
    Require(UnstealthFollowing(behind), "the player stopped, the rogue still catches up: unstealth");
    Require(CatchUpYards > 0.0f, "catching up means beyond the follow distance by a margin");
    FollowFacts visible = f;
    visible.stealthed = false;
    Require(!UnstealthFollowing(visible), "not stealthed: nothing to do");
    FollowFacts dungeon = f;
    dungeon.inDungeon = true;
    Require(!UnstealthFollowing(dungeon), "in a dungeon: the stock rule (stealth for the opener)");
    FollowFacts bg = f;
    bg.inBattleground = true;
    Require(!UnstealthFollowing(bg), "in a battleground: the stock rule");
    FollowFacts attacked = f;
    attacked.hasAttackers = true;
    Require(!UnstealthFollowing(attacked), "an attacker near: stealth stays");
    FollowFacts enemies = f;
    enemies.enemyPlayers = true;
    Require(!UnstealthFollowing(enemies), "enemy players near: stealth stays");
    FollowFacts combat = f;
    combat.nonCombat = false;
    Require(!UnstealthFollowing(combat), "the combat engine runs: the combat triggers decide");
    FollowFacts stay = f;
    stay.follows = false;
    Require(!UnstealthFollowing(stay), "told to stay (no follow): stealth stays");

    std::cout << "reach_drop_policy_tests passed\n";
    return 0;
}
