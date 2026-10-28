#pragma once

// twow-repo#629 (DUN-03): which bot-account characters a `.dc test` run may claim
// and rebuild. Pure rules, no core types - the loaders live in DcTestPool.cpp.
//
// claim() rebuilds the character it takes (level, gear, talents), so a persistent
// roster bot must never be one of them. The roster is read straight from its
// tables, independent of PersistentActiveRoster.Enabled: a test stack runs with the
// roster switched off (its bots stay offline), and then IsPersistentRosterMember
// answers false for every roster bot. TestRun.AccountPrefix (default "") narrows
// the pool further to dedicated test accounts, e.g. RNDBOTSYN on stack B.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

namespace DcTestPool
{
    // The prefix goes into a LIKE pattern: letters and digits only, at most 32.
    inline bool PrefixIsSafe(std::string const& prefix)
    {
        if (prefix.size() > 32)
            return false;
        for (char c : prefix)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                return false;
        return true;
    }

    // Account names compare case-insensitively (as AiPlayerbot.RandomBotAccountPrefix).
    // An empty prefix matches every account.
    inline bool PrefixMatches(std::string const& username, std::string const& prefix)
    {
        if (username.size() < prefix.size())
            return false;
        for (std::size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(username[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        return true;
    }

    enum class Skip
    {
        None,           // claimable
        NotBotAccount,  // not a random-bot account at all
        NotPoolAccount, // a bot account, but outside TestRun.AccountPrefix
        RosterMember,   // a persistent roster bot (never rebuilt by a test run)
    };

    // In this order: the roster check wins over everything, so a roster bot that
    // also sits on a pool account is still refused (and counted as such).
    inline Skip Decide(bool botAccount, bool prefixSet, bool poolAccount, bool rosterMember)
    {
        if (rosterMember)
            return Skip::RosterMember;
        if (!botAccount)
            return Skip::NotBotAccount;
        if (prefixSet && !poolAccount)
            return Skip::NotPoolAccount;
        return Skip::None;
    }

    // The roster tables' presence decides how a failed read is taken: no tables
    // means no roster on this realm (nothing to exclude); one table without the
    // other, or tables whose read failed, refuse the run (fail closed).
    enum class RosterTables
    {
        None,
        Partial,
        Both,
    };

    inline RosterTables ClassifyRosterTables(unsigned tablesFound)
    {
        return tablesFound == 0 ? RosterTables::None
             : tablesFound >= 2 ? RosterTables::Both
             : RosterTables::Partial;
    }

    // A failed query and an empty result look the same (no result set), so the
    // members are counted first (COUNT(*) always yields one row) and the guid read
    // must then deliver at least that many rows. Anything else refuses the run.
    enum class RosterRead
    {
        Empty,   // the count read 0: nothing to exclude
        Ok,      // every counted member was read
        Refuse,  // count failed, guid read failed or came back short
    };

    inline RosterRead ClassifyRosterRead(bool countOk, std::uint64_t counted, bool guidsOk, std::uint64_t read)
    {
        if (!countOk)
            return RosterRead::Refuse;
        if (counted == 0)
            return RosterRead::Empty;
        if (!guidsOk || read < counted)
            return RosterRead::Refuse;
        return RosterRead::Ok;
    }

    // CLI-GRP G1 probe 28.10.2026: why a harness login (the test driver, a pool slot) was refused. With
    // AiPlayerbot.PersistentActiveRoster.Enabled=1 the random-bot holder admits roster members only, and the harness
    // logs both in through it - so a `.dc test` stack runs with the roster off (see the header). One text, said at once.
    inline std::string LoginRefusal(std::string const& who)
    {
        return who + " login rejected (AddPlayerBot refused it; with AiPlayerbot.PersistentActiveRoster.Enabled=1 "
                     "only roster members may log in through the random-bot holder - run .dc test with the roster off)";
    }
}
