/*
 * twow-repo#629 (DUN-03): the claim pool rules of `.dc test` (DcTestPoolPolicy.h).
 */

#include "gtest/gtest.h"

#include "TestRun/DcTestPoolPolicy.h"

using DcTestPool::Decide;
using DcTestPool::Skip;

TEST(DcTestPoolPolicy, RosterBotIsNeverClaimed)
{
    // The roster check wins, also on a pool account and without a prefix.
    EXPECT_EQ(Decide(true, false, false, true), Skip::RosterMember);
    EXPECT_EQ(Decide(true, true, true, true), Skip::RosterMember);
    EXPECT_EQ(Decide(false, true, true, true), Skip::RosterMember);
}

TEST(DcTestPoolPolicy, BotAccountsOnly)
{
    EXPECT_EQ(Decide(false, false, false, false), Skip::NotBotAccount);
    EXPECT_EQ(Decide(true, false, false, false), Skip::None);
}

TEST(DcTestPoolPolicy, PrefixNarrowsThePool)
{
    EXPECT_EQ(Decide(true, true, false, false), Skip::NotPoolAccount);
    EXPECT_EQ(Decide(true, true, true, false), Skip::None);
    // Without a prefix the pool flag is ignored.
    EXPECT_EQ(Decide(true, false, true, false), Skip::None);
}

TEST(DcTestPoolPolicy, PrefixMatchesCaseInsensitive)
{
    EXPECT_TRUE(DcTestPool::PrefixMatches("RNDBOTSYN001", "RNDBOTSYN"));
    EXPECT_TRUE(DcTestPool::PrefixMatches("rndbotsyn001", "RNDBOTSYN"));
    EXPECT_FALSE(DcTestPool::PrefixMatches("RNDBOT507", "RNDBOTSYN"));
    EXPECT_FALSE(DcTestPool::PrefixMatches("RND", "RNDBOTSYN"));
    EXPECT_TRUE(DcTestPool::PrefixMatches("anything", ""));
}

TEST(DcTestPoolPolicy, PrefixIsSafeForLike)
{
    EXPECT_TRUE(DcTestPool::PrefixIsSafe("RNDBOTSYN"));
    EXPECT_TRUE(DcTestPool::PrefixIsSafe(""));
    EXPECT_FALSE(DcTestPool::PrefixIsSafe("RND%"));
    EXPECT_FALSE(DcTestPool::PrefixIsSafe("RND_"));
    EXPECT_FALSE(DcTestPool::PrefixIsSafe("x' OR '1'='1"));
    EXPECT_FALSE(DcTestPool::PrefixIsSafe(std::string(33, 'A')));
}

TEST(DcTestPoolPolicy, RosterTablesFailClosed)
{
    EXPECT_EQ(DcTestPool::ClassifyRosterTables(0), DcTestPool::RosterTables::None);
    EXPECT_EQ(DcTestPool::ClassifyRosterTables(1), DcTestPool::RosterTables::Partial);
    EXPECT_EQ(DcTestPool::ClassifyRosterTables(2), DcTestPool::RosterTables::Both);
}
