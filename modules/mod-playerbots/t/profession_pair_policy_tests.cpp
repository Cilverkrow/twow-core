#include "ProfessionPair.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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

std::vector<ai::profession::RosterMember> MakeRoster(std::size_t count)
{
    std::vector<ai::profession::RosterMember> members;
    std::uint8_t const classes[] = { 3, 1, 7, 8, 4, 2, 11, 5, 9 };
    for (std::size_t index = 0; index < count; ++index)
        members.push_back({ static_cast<std::uint32_t>(10000 + index), classes[index % (sizeof(classes) / sizeof(classes[0]))] });
    return members;
}

std::uint32_t Count(ai::profession::ExactRosterPlan const& plan, ai::profession::Pair pair)
{
    std::uint32_t count = 0;
    for (ai::profession::PlanAssignment const& assignment : plan.assignments)
        if (assignment.pair == pair)
            ++count;
    return count;
}

void TestExactApproved68QuotaAndDeterminism()
{
    using namespace ai::profession;
    std::vector<RosterMember> const members = MakeRoster(kExactRosterBaseSize);
    ExactRosterPlan first, second;
    CHECK(MaterializeExactRosterPlan(members, {}, first) == ExactPlanResult::Success);
    CHECK(MaterializeExactRosterPlan(members, {}, second) == ExactPlanResult::Success);
    CHECK(first.version == kExactRosterPlanVersion);
    CHECK(first.assignments.size() == kExactRosterBaseSize);
    CHECK(first.assignments == second.assignments);
    CHECK(Count(first, HerbalismAlchemy) == 4);
    CHECK(Count(first, SkinningLeatherworking) == 13);
    CHECK(Count(first, MiningBlacksmithing) == 16);
    CHECK(Count(first, MiningEngineering) == 11);
    CHECK(Count(first, MiningJewelcrafting) == 11);
    CHECK(Count(first, TailoringEnchanting) == 13);
    for (PlanAssignment const& assignment : first.assignments)
        CHECK(IsValid(assignment.pair));

    // The first roster member is a hunter. With all quotas available its
    // largest class preference is Skinning+Leatherworking (21), proving that
    // class weight decides before deterministic GUID/pair tie-breaking.
    CHECK(first.assignments.front().pair == SkinningLeatherworking);
}

void TestStablePrefixScaleUpAndIdempotence()
{
    using namespace ai::profession;
    ExactRosterPlan initial, expanded, replay, scaled500;
    std::vector<RosterMember> const roster68 = MakeRoster(68);
    std::vector<RosterMember> const roster136 = MakeRoster(136);
    std::vector<RosterMember> const roster500 = MakeRoster(500);
    CHECK(MaterializeExactRosterPlan(roster68, {}, initial) == ExactPlanResult::Success);
    CHECK(MaterializeExactRosterPlan(roster136, {}, replay) == ExactPlanResult::ExistingPlanRequired);
    CHECK(MaterializeExactRosterPlan(roster136, initial.assignments, expanded) == ExactPlanResult::Success);
    CHECK(MaterializeExactRosterPlan(roster136, expanded.assignments, replay) == ExactPlanResult::Success);
    CHECK(expanded.assignments == replay.assignments);
    CHECK(std::equal(initial.assignments.begin(), initial.assignments.end(), expanded.assignments.begin()));
    CHECK(Count(expanded, HerbalismAlchemy) == 8);
    CHECK(Count(expanded, SkinningLeatherworking) == 26);
    CHECK(Count(expanded, MiningBlacksmithing) == 32);
    CHECK(Count(expanded, MiningEngineering) == 22);
    CHECK(Count(expanded, MiningJewelcrafting) == 22);
    CHECK(Count(expanded, TailoringEnchanting) == 26);
    CHECK(MaterializeExactRosterPlan(roster500, expanded.assignments, scaled500) == ExactPlanResult::Success);
    CHECK(std::equal(expanded.assignments.begin(), expanded.assignments.end(), scaled500.assignments.begin()));
    CHECK(scaled500.assignments.size() == 500);
    CHECK(Count(scaled500, HerbalismAlchemy) == 29);
    CHECK(Count(scaled500, SkinningLeatherworking) == 96);
    CHECK(Count(scaled500, MiningBlacksmithing) == 118);
    CHECK(Count(scaled500, MiningEngineering) == 81);
    CHECK(Count(scaled500, MiningJewelcrafting) == 81);
    CHECK(Count(scaled500, TailoringEnchanting) == 95);
    for (PlanAssignment const& assignment : scaled500.assignments)
        CHECK(IsValid(assignment.pair));
}

void TestInvalidAndConflictingExistingPlansFailClosed()
{
    using namespace ai::profession;
    std::vector<RosterMember> members = MakeRoster(68);
    ExactRosterPlan plan;
    CHECK(MaterializeExactRosterPlan(members, {}, plan) == ExactPlanResult::Success);
    std::vector<PlanAssignment> conflict = plan.assignments;
    conflict.front().pair = conflict.front().pair == HerbalismAlchemy ? SkinningLeatherworking : HerbalismAlchemy;
    ExactRosterPlan rejected;
    CHECK(MaterializeExactRosterPlan(members, conflict, rejected) == ExactPlanResult::ExistingPlanConflict);
    members[1].guid = members[0].guid;
    CHECK(MaterializeExactRosterPlan(members, {}, rejected) == ExactPlanResult::InvalidRoster);

    std::vector<RosterMember> const expandedRoster = MakeRoster(136);
    CHECK(MaterializeExactRosterPlan(expandedRoster, {}, rejected) == ExactPlanResult::ExistingPlanRequired);
}
}

int main()
{
    TestOnlyApprovedPairsAndDeterminism();
    TestMiningSplitAndHunterWeight();
    TestContractClassFamilyMatrix();
    TestCompatibleSingleProfession();
    TestGrandfatheringPolicy();
    TestExactApproved68QuotaAndDeterminism();
    TestStablePrefixScaleUpAndIdempotence();
    TestInvalidAndConflictingExistingPlansFailClosed();
    if (failures) return EXIT_FAILURE;
    std::cout << "PROFESSION_PAIR_POLICY_TESTS=PASS\n";
    return EXIT_SUCCESS;
}
