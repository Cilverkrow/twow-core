#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::melee_skip
{
// twow-repo#602 ROT-01 failure census (OB-00 27.10.2026; AiPlayerbot.Rotation.MeleeSkipAttacking, code default 1, 0 = rollback): "melee"
// (MeleeAction, relevance -199) is the default action of the combat strategies and runs whenever every higher action is IMPOSSIBLE - in
// each GCD after an instant (druid: Wrath IMPOSSIBLE) or each rage gap (warrior). It calls AttackAction::Attack, and Unit::Attack returns
// false when the bot already attacks that victim and either swings already or would not swing (ranged): FAILED, after Attack() repeated
// its side effects (selection, target values, loot add, pet AttackStart, facing, attack emote). M1r run 4: druid "melee" FAILED 27,658
// vs 1 OK, the AI re-ticking every ~50 ms in the gap. With the switch the action is USELESS in that case: the same fall-through in the
// engine, without the side effects and without a false FAILED in the action log. Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// Unit::Attack(target, wantMelee) would return false: the bot already attacks this target, and it swings already or would not swing.
// A ranged bot that would now switch to melee (hunter "close" mode, Rotation.HunterMeleeStart) still attacks. A bot not facing its
// target still runs the old Attack(), which turns it (the melee swing needs the target in front), and so does a bot whose living pet is
// not on that target (Attack() sends the pet). plainAttack = not mounted (Attack() unmounts) and neither "wait for attack" nor
// "stealthed" (there Attack() returns OK without attacking, which ends the engine pass).
inline bool Skip(bool applies, bool targetIsVictim, bool swinging, bool wantMelee, bool inFront, bool petOnTarget, bool plainAttack)
{
    return applies && targetIsVictim && inFront && petOnTarget && plainAttack && (swinging || !wantMelee);
}

// [MeleeSkip] counter. Map threads add, the world thread takes it once per minute.
inline std::atomic<std::uint64_t> skipped{ 0 };   // melee passes made USELESS because the bot already attacks its target
}
