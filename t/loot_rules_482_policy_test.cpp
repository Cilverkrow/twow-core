// twow-repo#482 owner rule table v5: raid profiles, dungeon bands, mix allocation, trim rule.
#include "../src/game/FunserverLootRules.h"

#include <iostream>
#include <vector>

using namespace FunserverLootRules;

namespace
{
    int failures = 0;

    void Check(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << label << "\n";
            ++failures;
        }
    }

    // Deterministic rolls: hit (0.0) or miss (0.9) in the given order, then misses.
    struct Rolls
    {
        std::vector<float> values;
        size_t next = 0;
        float operator()() { return next < values.size() ? values[next++] : 0.9f; }
    };
    float constexpr HIT = 0.0f;
    float constexpr MISS = 0.9f;
}

int main()
{
    // Profiles and bands as decided (v5).
    RaidProfile const* t1 = FindRaidProfile("T1");
    Check(t1 && t1->setMode == SetMode::Mix && t1->setMax == 18 && t1->ownMin == 6 && t1->ownMax == 12, "T1 18 set, own 6-12");
    RaidProfile const* t2 = FindRaidProfile("T2");
    Check(t2 && t2->setMax == 12 && t2->ownMin == 4 && t2->ownMax == 10, "T2 12 set, own 4-10");
    RaidProfile const* es = FindRaidProfile("T2ES");
    Check(es && es->setMode == SetMode::None && es->ownMin == 4 && es->ownMax == 10, "ES = T2 own, no set");
    RaidProfile const* lk = FindRaidProfile("LK10");
    Check(lk && lk->setMode == SetMode::None && lk->ownMin == 3 && lk->ownMax == 6, "Lower Kara 3-6, no set");
    Check(FindRaidProfile("T25")->tokenMax == 9 && FindRaidProfile("T3")->tokenMax == 6 &&
          FindRaidProfile("T35")->tokenMax == 3 && FindRaidProfile("TOK20")->tokenMax == 8, "token maxima 9/6/3/8");
    Check(FindRaidProfile("T25")->ownMin == 3 && FindRaidProfile("T25")->ownMax == 9 &&
          FindRaidProfile("T3")->ownMax == 8 && FindRaidProfile("T35")->ownMax == 6, "own 3-9 / 3-8 / 2-6");
    Check(FindRaidProfile("TOK20")->ownMin == 2 && FindRaidProfile("TOK20")->ownMax == 6, "Timbermaw/ZG/AQ20 own 2-6");
    Check(FindRaidProfile("T9") == nullptr, "unknown profile");

    DungeonBand const* top = FindDungeonBand("60+");
    Check(top && top->min == 4 && top->max == 8 && top->boeMax == 2 && top->minQuality == 3, "60+ 4-8, blue, 2 BoE");
    Check(FindDungeonBand("30-40")->min == 2 && FindDungeonBand("30-40")->boeMax == 1, "30-40 min 2, 1 BoE");
    Check(FindDungeonBand("40-50")->min == 2 && FindDungeonBand("40-50")->boeMax == 2, "40-50 min 2, 2 BoE");
    Check(FindDungeonBand("10-20")->max == 2 && FindDungeonBand("50-60")->max == 6, "band maxima");

    // Map assignments.
    auto raids = ParseMapNames(DEFAULT_RAID_MAPS);
    Check(raids[409] == "T1" && raids[469] == "T2" && raids[249] == "T2" && raids[807] == "T2ES", "raid maps T1/T2/ES");
    Check(raids[531] == "T25" && raids[533] == "T3" && raids[814] == "T35" && raids[532] == "LK10", "raid maps tokens/LK");
    Check(raids[819] == "TOK20" && raids[309] == "TOK20" && raids[509] == "TOK20", "TOK20 maps");
    for (auto const& r : raids)
        Check(FindRaidProfile(r.second) != nullptr, "every raid map names a profile");
    auto dungeons = ParseMapNames(DEFAULT_DUNGEON_MAPS);
    Check(dungeons.size() == 28, "28 registered dungeons");
    Check(dungeons[70] == "40-50" && dungeons[229] == "60+" && dungeons[36] == "20-30" && dungeons[349] == "50-60", "owner band cases");
    for (auto const& d : dungeons)
        Check(FindDungeonBand(d.second) != nullptr, "every dungeon map names a band");
    auto odd = ParseMapNames("x:T1,12,:T2, 7 :T3,8:");
    Check(odd.size() == 1 && odd[7] == "T3", "malformed map entries skipped");

    // Token lists of the token raids.
    auto tokens = ParseTokenLists(DEFAULT_RAID_TOKENS);
    Check(tokens[531].size() == 6 && tokens[533].size() == 24 && tokens[814].size() == 9, "AQ40 6, Naxx 24, K40 9 tokens");
    Check(tokens[309].size() == 9 && tokens[509].size() == 6 && tokens[819].size() == 4, "ZG 9, AQ20 6, Timbermaw 4 tokens");
    Check(tokens[533].front() == 22352 && tokens[533].back() == 55583, "Naxx range and rings");
    for (auto const& t : tokens)
    {
        auto const profile = raids.find(t.first);
        Check(profile != raids.end() && FindRaidProfile(profile->second)->setMode == SetMode::Tokens, "token lists only for token profiles");
    }
    auto badTokens = ParseTokenLists("x:1;5:9-3,7,a,2-;6:1-5000;8:4");
    Check(badTokens.size() == 2 && badTokens[5].size() == 1 && badTokens[5][0] == 7 && badTokens[8][0] == 4, "malformed token parts skipped");

    // Min sure, then 33 % per further slot.
    Check(RollCount(6, 12, EXTRA_CHANCE, Rolls{}) == 6, "T1 own: all extra rolls miss -> 6");
    Check(RollCount(6, 12, EXTRA_CHANCE, Rolls{ { HIT, HIT, HIT, HIT, HIT, HIT } }) == 12, "T1 own: all hit -> 12");
    Check(RollCount(1, 9, EXTRA_CHANCE, Rolls{ { HIT, MISS, HIT } }) == 3, "AQ40 tokens 1 + 2 hits");
    Check(RollCount(5, 3, EXTRA_CHANCE, Rolls{}) == 3, "min above max -> max");

    // Dungeons: owner examples.
    DungeonPlan p = PlanDungeon(*top, 1, Rolls{ { HIT, HIT, HIT, HIT } });
    Check(p.own == 1 && p.boe == 2, "60+ boss with 1 item: 1 own + 2 BoE, as far as possible");
    p = PlanDungeon(*FindDungeonBand("50-60"), 3, Rolls{ { HIT, HIT, HIT } });
    Check(p.own == 3 && p.boe == 2, "50-60, 3 own: min 3, then rolls go to BoE, at most 2");
    p = PlanDungeon(*FindDungeonBand("50-60"), 10, Rolls{ { HIT, MISS, HIT } });
    Check(p.own == 5 && p.boe == 0, "50-60, rich table: extra rolls stay in the own table");
    p = PlanDungeon(*FindDungeonBand("10-20"), 0, Rolls{ { HIT } });
    Check(p.own == 0 && p.boe == 1, "10-20 without own items: 1 BoE, max 1");
    p = PlanDungeon(*top, 12, Rolls{});
    Check(p.own == 4 && p.boe == 0, "60+ rich table, no hits: exactly the minimum 4");

    // Upgrade check with the set bonus.
    Check(IsSetUpgrade(66, 0, 0), "empty slot is an upgrade");
    Check(IsSetUpgrade(76, 71, 0) && !IsSetUpgrade(76, 76, 0), "item level beats equipped");
    Check(!IsSetUpgrade(66, 70, 1) && IsSetUpgrade(66, 70, 2), "+5 from 2 pieces worn");

    // Mix allocation: 40 members, 18 at most.
    std::vector<ClassNeed> raid = {
        { 1, 8, 8, 0 },     // warrior
        { 2, 4, 4, 0 },     // paladin
        { 4, 4, 0, 0 },     // rogue: nobody would upgrade -> nothing
        { 5, 8, 8, 0 },     // priest
        { 8, 8, 8, 0 },     // mage
        { 9, 4, 4, 1 },     // warlock: one fixed drop already
        { 11, 4, 4, 0 },    // druid
    };
    std::vector<uint32_t> extra = AllocateMix(raid, 18);
    uint32_t total = 0;
    for (uint32_t e : extra)
        total += e;
    Check(total == 17, "18 minus the fixed drop -> 17 extra");
    Check(extra[2] == 0, "class without upgrader gets nothing");
    Check(extra[0] >= 3 && extra[3] >= 3 && extra[4] >= 3, "large classes get the larger share");
    Check(extra[5] <= 3, "warlock capped by upgraders minus fixed drop");
    for (size_t i = 0; i < raid.size(); ++i)
        Check(raid[i].fixedDrops + extra[i] <= raid[i].upgraders, "never more pieces than upgraders");

    std::vector<ClassNeed> small = { { 1, 10, 2, 0 }, { 8, 2, 2, 0 } };
    extra = AllocateMix(small, 12);
    Check(extra[0] == 2 && extra[1] == 2, "few upgraders cap the total below SetMax");
    Check(AllocateMix({ { 1, 5, 5, 3 } }, 2).front() == 0, "fixed drops above SetMax -> nothing extra");

    // Trim rule: normal first, then set down to need, then extra own; the rest never.
    LootCounts t1kill;
    t1kill.mandatory = 1; t1kill.fixedSet = 1; t1kill.ownMin = 6; t1kill.set = 17; t1kill.ownExtra = 6; t1kill.normal = 5;
    Check(t1kill.Total() == 36, "T1 worst case 36");
    LootCounts c = Trim(t1kill, 32, 9);
    Check(c.normal == 1 && c.set == 17 && c.ownExtra == 6 && c.Total() == 32, "32 slots: only normal loot cut");
    c = Trim(t1kill, 24, 9);
    Check(c.normal == 0 && c.set == 10 && c.ownExtra == 6 && c.Total() == 24, "24 slots: normal gone, set 17 -> 10, extra own kept");
    c = Trim(t1kill, 20, 9);
    Check(c.normal == 0 && c.set == 9 && c.ownExtra == 3 && c.Total() == 20, "20 slots: set down to need 9, then extra own 6 -> 3");
    c = Trim(t1kill, 8, 9);
    Check(c.mandatory == 1 && c.fixedSet == 1 && c.ownMin == 6, "protected parts never cut");
    Check(c.normal == 0 && c.set == 9 && c.ownExtra == 0 && c.Total() == 17, "set never below need; limit unreachable -> 17");
    LootCounts small2;
    small2.ownMin = 4; small2.normal = 3;
    Check(Trim(small2, 16, 0).Total() == 7, "under the limit nothing is cut");

    if (failures)
    {
        std::cerr << failures << " loot rule check(s) failed\n";
        return 1;
    }
    std::cout << "LOOT_RULES_482_POLICY=PASS\n";
    return 0;
}
