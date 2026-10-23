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
}
