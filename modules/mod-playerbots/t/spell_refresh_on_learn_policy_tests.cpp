// twow-repo#541 (audit A27): pure part of the spell id refresh behind AiPlayerbot.CastSpell.RefreshOnLearn.
// The stamp must change on every learn/forget path the core has, never be 0, keep its fields apart, and the
// range must follow the spell only while no subclass replaced it.
#include "SpellStampPolicy.h"

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
    using namespace ai::spellstamp;

    const float meleeRange = 5.0f;  // ATTACK_DISTANCE
    const auto base = Make(120, 9, 0, 0);

    // 0 stays reserved for "not stamped yet" (first isPossible and a switch turned on by reload refresh once).
    Require(base != 0, "stamp is never 0");
    Require(Make(0, 0, 0, 0) != 0, "empty state stamp is never 0");

    // Same state -> same stamp -> no refresh.
    Require(Make(120, 9, 0, 0) == base, "unchanged state keeps the stamp");

    // Every learn/forget path changes the stamp.
    Require(Make(121, 9, 0, 0) != base, "new spell or rank (spell map gains a key)");
    Require(Make(119, 9, 0, 0) != base, "NEW spell erased / REMOVED spell dropped at save");
    Require(Make(120, 10, 1, 0) != Make(120, 10, 0, 0), "talent rank swap keeps the map size but spends a point");
    Require(Make(120, 10, 2, 0) != Make(120, 10, 1, 0), "talent reset or level-up point");
    Require(Make(120, 10, 0, 0) != base, "level-up alone");
    // Review A27x-1 (ABA): +1 point at level-up, spent on the next rank of a talent whose rank is still NEW:
    // size and points are back where they were, only the level differs.
    Require(Make(150, 21, 0, 0) != Make(150, 20, 0, 0), "level breaks the size/points ABA");

    // Pet number (pet spell actions only).
    Require(Make(120, 9, 0, 7) != Make(120, 9, 0, 0), "pet appeared");
    Require(Make(120, 9, 0, 7) != Make(120, 9, 0, 8), "other pet");
    Require(Make(120, 9, 0, 7) == Make(120, 9, 0, 7), "same pet back (resummon keeps the pet number)");

    // Fields do not bleed into each other.
    Require(Make(0x7FFFF, 0, 0, 0) != Make(0, 1, 0, 0), "spell count does not reach the level field");
    Require(Make(0, 0xFF, 0, 0) != Make(0, 0, 1, 0), "level does not reach the talent field");
    Require(Make(0, 0, 0xFFF, 0) != Make(0, 0, 0, 1), "talent points do not reach the pet field");
    Require(Make(0, 0, 0, 0xFFFFFF) != Make(0, 0, 0, 0), "full pet number keeps the marker");

    // Pet spell actions wait for a pet; plain spell actions never wait.
    Require(DeferWithoutPet(true, 0), "pet spell action without pet: no refresh (no N->0->N)");
    Require(!DeferWithoutPet(true, 7), "pet spell action with pet: refresh");
    Require(!DeferWithoutPet(false, 0), "plain spell action: never deferred");

    // Range: follows the spell only while untouched; melee/custom subclass ranges stay.
    Require(RangeFollowsSpell(30.0f, 30.0f, meleeRange), "plain ranged action follows the spell (relog value)");
    Require(RangeFollowsSpell(25.0f, 25.0f, meleeRange), "default spell distance follows once the spell is known");
    Require(!RangeFollowsSpell(meleeRange, 30.0f, meleeRange), "CastMeleeSpellAction keeps ATTACK_DISTANCE");
    Require(!RangeFollowsSpell(meleeRange, meleeRange, meleeRange), "the melee marker never follows, even if equal");
    Require(!RangeFollowsSpell(10.0f, 30.0f, meleeRange), "judgement / blind subclass keeps 10");
    Require(!RangeFollowsSpell(8.0f, 20.0f, meleeRange), "shouts keep 8");

    std::cout << "spell_refresh_on_learn policy tests passed\n";
    return 0;
}
