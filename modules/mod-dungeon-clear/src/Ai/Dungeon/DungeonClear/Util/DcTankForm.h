/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTANKFORM_H
#define _PLAYERBOT_DCTANKFORM_H

class PlayerbotAI;

// Pre-pull shapeshift gate for a DRUID tank. The mirror image of DcFormGate,
// which drops a form so an item-use cast can go out; this one puts the tank's
// combat form UP before it opens a pull.
//
// Why it is needed: stock only ever shifts a feral tank on the COMBAT engine.
// The "bear form" trigger lives in BearDruidStrategy, whose type is
// STRATEGY_TYPE_TANK|STRATEGY_TYPE_MELEE — combat-engine only — so nothing in
// the non-combat engine has an opinion about the druid's form. Every
// DungeonClear pull, by construction, is opened OUT of combat: the tank walks
// to the aggro edge / casts its opener while still on the non-combat engine,
// and only crosses to the combat engine once aggro lands. So the druid tags in
// caster form, eats the pack's opener at cloth armour and caster health, and
// shifts a beat later when BearDruidStrategy finally gets a tick — the "pulls
// in human form, takes a few hits, then shifts to bear" the runs show.
//
// Stock's own DruidPullStrategy already names "dire bear form" as its pull
// PRE-action for exactly this reason, but DungeonClear does not drive stock's
// pull strategy — it owns the whole maneuver (DungeonClearPullAction) and the
// walk-in engage (EngageDirect) — so that pre-action never runs here.
//
// Two things it must not break, and why it doesn't:
//
//  * DRINKING. A bear cannot drink — stock's DrinkAction::isPossible rejects
//    every feral form outright — so shifting early could in principle starve a
//    Smart Rest top-off. Both call sites sit DOWNSTREAM of the rest gate: the
//    pull trigger is ready-gated, and the boss engage's own comment records
//    that the trigger "already proved ... the between-pulls readiness (incl.
//    the Smart Rest top-off)". The engage call is additionally range-gated to
//    DC_PULL_START_RANGE, so a tank still crossing open ground is left in
//    caster form. (Post-combat the tank is in bear form anyway — stock put it
//    there — so the bear-can't-drink case is pre-existing, and Smart Rest's
//    maxRestMs failsafe already bounds it.)
//
//  * DcFormGate. The event/objective item-use steps DROP the form to get their
//    cast out; this gate would put it back. They can't fight over a tick: the
//    engine runs one action per tick, and the event rungs (EventDue) outrank
//    every engage rung, so the shift simply doesn't happen while an event
//    drives. Worst case is one wasted shift, never a stall — DcFormGate's
//    drop is synchronous and its step is idempotent.
namespace DcTankForm
{
    // True when this bot fights as a bear: a druid running the "bear" combat
    // strategy that has actually trained a bear form. Deliberately keyed on the
    // strategy rather than PlayerbotAI::IsTank(bySpec=true) — the spec test for
    // a druid asks whether it is ALREADY in bear form, which is circular here.
    bool IsBearTank(PlayerbotAI* botAI);

    // IMPURE: if the bot is a bear tank and is not already in Bear/Dire Bear
    // form, shift it. Returns true when a shift was ISSUED this tick — not that
    // the form is up.
    //
    // The shift is instant, but "instant" is not "done": CastSpell only puts it
    // into CURRENT_GENERIC_SPELL in state PREPARING, and it executes on the
    // bot's next spell update. Movement issued in the SAME tick cancels it —
    // MovementAction::MoveTo / MoveTo2 interrupt any non-melee cast including a
    // prepared instant (IsNonMeleeSpellCasted(true, false, true) ->
    // InterruptSpell), and DcMovement's SplinePath does CastStop. So a caller
    // that shifts and then walks in the same tick gets SPELL_FAILED_INTERRUPTED
    // (0x23), not a bear: 54 times in 12 probe runs, and the druid tagged in
    // caster form for as long as the approach lasted (CLI-GRP 02.11.2026,
    // g2/CAUSE-DRUIDE-SHIFT-ABBRUCH). The old note here said the shift was "off
    // the movement path ... neither stops a glide nor is interrupted by one" —
    // the first half is true, the second is not.
    //
    // Nothing in the pull is allowed to WAIT on the form: one that cannot go up
    // (silenced, out of mana, mid-GCD, polymorphed) must never be able to hold a
    // pull open. Callers therefore go through ShiftHoldsApproach below, which
    // with the switch at 0 is exactly the old fire-and-forget call.
    bool EnsureBearForm(PlayerbotAI* botAI);

    // IMPURE: the call sites' entry point — calls EnsureBearForm and returns
    // true when the caller must issue NO movement this tick and end its action
    // with true (it consumed the tick), so the shift lands before the approach.
    //
    // DungeonClear.BearShiftBeforeMove = 0 (default): EnsureBearForm exactly as
    // before, its result ignored, always false — no state read or written, no
    // log beyond EnsureBearForm's own "shifting to" line.
    //
    // = 1: true only on a tick that issued a shift AND passed the stall guard
    // (DungeonClearMath::ShouldHoldForBearShift: at most one hold per bot per
    // BearShiftHoldWindowMs, stamped in this bot's own "dungeon clear bear
    // shift hold" value). The next tick the form is up, EnsureBearForm returns
    // false at once and the approach runs as before. A form still down after
    // its held tick is re-issued inside the window and NOT held again — the
    // worst case is the old behaviour, never a stalled pull. The cost is one AI
    // tick per shift, the same order the stock pull's ActionNode prerequisite
    // gives (the prerequisite is its own action tick).
    bool ShiftHoldsApproach(PlayerbotAI* botAI);
}

#endif
