#pragma once

#include <cstdint>

// twow-repo#630 follow-up (AiPlayerbot.Rotation.ReachDropEngagement, owner repro 25.10.2026 on C): with Rotation.ReachPlayerMaster
// a bot under a real player drops an unreachable target after 20 s - but only its "current target". A rogue sent with "attack"
// while stealthed never swung (AttackAction skips bot->Attack in stealth) yet went to the combat engine, and its "attack target"
// kept naming the target: AttackersValue counts the bot's own attack/pull target as an attacker, "has attackers" stayed true,
// the combat engine never ended, and there is no follow in it - the rogue stood stealthed in front of the target until the
// player said "follow" (whose AI reset clears all targets). Then it followed in stealth: the stock unstealth trigger fires only
// beyond 10 yd of the master target, and follow outranks it.
namespace ai::reach_drop
{
// What still named the dropped target besides "current target" - the drop clears each of them.
struct Held
{
    bool attackTarget = false;   // "attack target" (the attack command, AttackMyTargetAction)
    bool pullTarget = false;     // "pull target" (and the pull strategy's running pull)
    bool oldTarget = false;      // "old target" (SelectNewTargetAction's last target)
    bool selection = false;      // the bot's own selection
    bool victim = false;         // a running melee swing

    bool Any() const { return attackTarget || pullTarget || oldTarget || selection || victim; }
};

// AiPlayerbot.Rotation.FollowUnstealth (its own switch, so an A/B tells the two effects apart): a rogue following a real player
// outside combat, dungeons and battlegrounds drops stealth while the master moves or the rogue is still catching up (it does
// not re-stealth there: the stealth trigger is a combat trigger). Standing together it keeps stealth; inside a dungeon, in a
// battleground or with an attacker near, the stock rule stays (stealth for the opener). Known: Stealth's cooldown starts when
// it breaks, so a pull within ~10 s after the unstealth opens without Cheap Shot/Ambush.
constexpr float CatchUpYards = 5.0f;   // beyond the follow distance by more than this: still catching up

struct FollowFacts
{
    bool stealthed = false;
    bool inBattleground = false;
    bool inDungeon = false;
    bool hasAttackers = false;
    bool enemyPlayers = false;
    bool nonCombat = false;      // the non-combat engine runs
    bool follows = false;        // "follow" in the non-combat strategies
    bool masterMoving = false;   // the player's client movement flags (a bot's own IsStopped stays false while it follows)
    bool catchingUp = false;     // farther than follow distance + CatchUpYards from the master
};

inline bool UnstealthFollowing(FollowFacts const& f)
{
    return f.stealthed && !f.inBattleground && !f.inDungeon && !f.hasAttackers && !f.enemyPlayers && f.nonCombat && f.follows &&
        (f.masterMoving || f.catchingUp);
}
}
