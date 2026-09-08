#include "ProfessionPair.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

void TestOnlyApprovedPairsAndDeterminism()
{
    for (std::uint8_t classId : {std::uint8_t(1),std::uint8_t(2),std::uint8_t(3),std::uint8_t(4),std::uint8_t(5),std::uint8_t(7),std::uint8_t(8),std::uint8_t(9),std::uint8_t(11)})
        for (std::uint32_t guid = 1; guid < 2048; ++guid)
        {
            ai::profession::Pair const first = ai::profession::Select(guid, classId);
            CHECK(ai::profession::IsValid(first));
            CHECK(first == ai::profession::Select(guid, classId));
        }
}

void TestMiningSplitAndHunterWeight()
{
    using namespace ai::profession;
    CHECK(Weight(3, MiningBlacksmithing) == 9);
    CHECK(Weight(3, MiningEngineering) == 6);
    CHECK(Weight(3, MiningJewelcrafting) == 6);
    CHECK(Weight(3, MiningBlacksmithing) + Weight(3, MiningEngineering) + Weight(3, MiningJewelcrafting) == 21);
    CHECK(Weight(3, HerbalismAlchemy) == 14);
    CHECK(Weight(3, SkinningLeatherworking) == 21);
}

void TestContractClassFamilyMatrix()
{
    using namespace ai::profession;
    struct Expected { std::uint8_t classId; std::uint32_t herbalism; std::uint32_t skinning; std::uint32_t mining; std::uint32_t tailoring; };
    Expected const expected[] =
    {
        { 1, 2, 2, 3, 1 }, // warrior
        { 2, 2, 1, 3, 2 }, // paladin
        { 3, 2, 3, 3, 1 }, // hunter
        { 4, 1, 3, 2, 1 }, // rogue
        { 5, 2, 1, 1, 3 }, // priest
        { 7, 3, 3, 2, 1 }, // shaman
        { 8, 2, 1, 1, 3 }, // mage
        { 9, 2, 1, 1, 3 }, // warlock
        { 11, 3, 3, 1, 1 } // druid
    };

    for (Expected const& row : expected)
    {
        CHECK(Weight(row.classId, HerbalismAlchemy) == row.herbalism * 7);
        CHECK(Weight(row.classId, SkinningLeatherworking) == row.skinning * 7);
        CHECK(Weight(row.classId, MiningBlacksmithing) == row.mining * 3);
        CHECK(Weight(row.classId, MiningEngineering) == row.mining * 2);
        CHECK(Weight(row.classId, MiningJewelcrafting) == row.mining * 2);
        CHECK(Weight(row.classId, TailoringEnchanting) == row.tailoring * 7);
    }
}

void TestCompatibleSingleProfession()
{
    using namespace ai::profession;
    Pair const mining = Select(18281, 3, kMining);
    CHECK(IsValid(mining));
    CHECK(Contains(mining, kMining));
    CHECK(Select(18281, 3, kMining) == mining);
    CHECK(Select(18281, 3, 0xFFFFFFFFu) == None);
}

void TestGrandfatheringPolicy()
{
    using namespace ai::profession;
    std::uint32_t const complete[] = { kSkinning, kLeatherworking };
    CHECK(SelectExisting(18281, 3, complete, 2) == SkinningLeatherworking);

    std::uint32_t const single[] = { kMining };
    Pair const selected = SelectExisting(18281, 3, single, 1);
    CHECK(IsValid(selected));
    CHECK(Contains(selected, kMining));

    std::uint32_t const conflicting[] = { kAlchemy, kBlacksmithing };
    CHECK(SelectExisting(18281, 1, conflicting, 2) == None);
    std::uint32_t const tooMany[] = { kHerbalism, kAlchemy, kMining };
    CHECK(SelectExisting(18281, 1, tooMany, 3) == None);

    Pair const emptyFirst = SelectExisting(18281, 3, nullptr, 0);
    Pair const emptySecond = SelectExisting(18281, 3, nullptr, 0);
    CHECK(IsValid(emptyFirst));
    CHECK(emptyFirst == emptySecond);
}
}

int main()
{
    TestOnlyApprovedPairsAndDeterminism();
    TestMiningSplitAndHunterWeight();
    TestContractClassFamilyMatrix();
    TestCompatibleSingleProfession();
    TestGrandfatheringPolicy();
    if (failures) return EXIT_FAILURE;
    std::cout << "PROFESSION_PAIR_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
