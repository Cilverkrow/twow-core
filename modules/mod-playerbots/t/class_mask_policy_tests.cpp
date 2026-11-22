// twow-repo#742 (AUDIT-L03-6): AllowableClass is a class mask; -1 / all-class masks are not class-restricted, 0 is nobody.
// Mask values are the consumable masks found in tw_world pin 3b (item_template.allowable_class, class 0).
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "ClassMaskPolicy.h"

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

// Vanilla playable classes: warrior 1, paladin 2, hunter 3, rogue 4, priest 5, shaman 7, mage 8, warlock 9, druid 11.
constexpr std::uint32_t Bit(std::uint32_t classId) { return 1u << (classId - 1); }
constexpr std::uint32_t AllPlayable =
    Bit(1) | Bit(2) | Bit(3) | Bit(4) | Bit(5) | Bit(7) | Bit(8) | Bit(9) | Bit(11);
}

int main()
{
    using namespace ai::class_mask;

    Require(AllPlayable == 0x5DFu, "CLASSMASK_ALL_PLAYABLE of vanilla is 0x5DF");

    // -1 (1272 consumables) and the other all-class masks: never "class-restricted", for any class.
    for (std::uint32_t classId : {1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 11u})
    {
        Require(!IsRestrictedTo(0xFFFFFFFFu, Bit(classId), AllPlayable), "mask -1 is all classes, not restricted");
        Require(!IsRestrictedTo(32767u, Bit(classId), AllPlayable), "mask 32767 is all classes, not restricted");
        Require(!IsRestrictedTo(2047u, Bit(classId), AllPlayable), "mask 2047 is all classes, not restricted");
        Require(!IsRestrictedTo(AllPlayable, Bit(classId), AllPlayable), "the exact playable mask is not restricted");
        Require(!IsRestrictedTo(0u, Bit(classId), AllPlayable), "mask 0 is nobody, not for this class");
    }

    // Rogue poisons: mask 8 = rogue only.
    Require(IsRestrictedTo(8u, Bit(4), AllPlayable), "mask 8 is for the rogue");
    Require(!IsRestrictedTo(8u, Bit(1), AllPlayable), "mask 8 is not for the warrior (the old ID compare matched nothing useful)");
    // The old ID compare matched warrior (1) and paladin (2) by accident; the mask reading keeps them right.
    Require(IsRestrictedTo(1u, Bit(1), AllPlayable), "mask 1 is for the warrior");
    Require(IsRestrictedTo(2u, Bit(2), AllPlayable), "mask 2 is for the paladin");
    Require(!IsRestrictedTo(2u, Bit(1), AllPlayable), "mask 2 is not for the warrior");
    // Multi-class restricted masks.
    Require(IsRestrictedTo(1025u, Bit(11), AllPlayable) && IsRestrictedTo(1025u, Bit(1), AllPlayable), "mask 1025: druid and warrior");
    Require(!IsRestrictedTo(1025u, Bit(4), AllPlayable), "mask 1025 is not for the rogue");
    Require(IsRestrictedTo(13u, Bit(3), AllPlayable) && !IsRestrictedTo(13u, Bit(2), AllPlayable), "mask 13: hunter yes, paladin no");

    std::cout << "class_mask_policy tests passed\n";
    return 0;
}
