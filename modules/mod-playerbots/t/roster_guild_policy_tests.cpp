#include "RosterGuildPolicy.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

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
    using namespace ai::roster_guild;

    // Only roster bots on their own, and only with BotsPerGuild > 0 (default 0 = stock path).
    Require(!UsesRosterPath(0, true, false), "BotsPerGuild 0 keeps the stock path");
    Require(UsesRosterPath(45, true, false), "a roster bot on its own takes the new path");
    Require(!UsesRosterPath(45, false, false), "other bots keep the stock path");
    Require(!UsesRosterPath(45, true, true), "a roster bot with a real master keeps the stock path");

    // Target per faction: one guild per started BotsPerGuild of the roster.
    Require(TargetGuilds(90, 45) == 2, "90 / 45 = 2");
    Require(TargetGuilds(91, 45) == 3, "91 / 45 = 3 (a started guild counts)");
    Require(TargetGuilds(180, 45) == 4, "wave 2: 180 / 45 = 4");
    Require(TargetGuilds(90, 30) == 3, "90 / 30 = 3");
    Require(TargetGuilds(90, 0) == 0, "BotsPerGuild 0: no target");
    Require(TargetGuilds(0, 45) == 0, "no roster bots: no guilds");

    // No more charters for bots once guilds plus open bot charters cover the target.
    Require(!MayBuyCharter(2, 0, 38), "38 open alliance charters at target 2: no purchase");
    Require(MayBuyCharter(2, 0, 1), "one charter open, two guilds missing: purchase");
    Require(!MayBuyCharter(2, 2, 0), "target reached: no purchase");
    Require(!MayBuyCharter(2, 1, 1), "one guild and one charter cover target 2");
    Require(!MayBuyCharter(0, 0, 0), "no target: no purchase");

    Require(MayFound(2, 1), "one guild of two: founding allowed");
    Require(!MayFound(2, 2), "target reached: no founding");
    Require(!MayFound(0, 0), "no target: no founding");

    // Critic B5.3: 4 complete charters (pid 59/62/65/72 at 9 signatures), target 2, the owners reach
    // guild masters at the same time on different map threads: 2 foundings, not 4.
    {
        FoundingLedger ledger;
        int slots = 0;
        for (int candidate = 0; candidate < 4; ++candidate)
            if (TryReserveFounding(2, ledger))
                ++slots;
        Require(slots == 2 && ledger.reserved == 2, "4 overlapping candidates at target 2 get 2 slots");
        FinishFounding(ledger, true);
        FinishFounding(ledger, true);
        Require(ledger.guilds == 2 && ledger.reserved == 0, "2 foundings");
        Require(!TryReserveFounding(2, ledger), "no third founding after the target");
    }
    {
        // The same 4 candidates one after the other.
        FoundingLedger ledger;
        int founded = 0;
        for (int candidate = 0; candidate < 4; ++candidate)
            if (TryReserveFounding(2, ledger))
            {
                FinishFounding(ledger, true);
                ++founded;
            }
        Require(founded == 2 && ledger.guilds == 2, "4 sequential candidates at target 2: 2 foundings");
    }
    {
        // A failed turn-in gives its slot back.
        FoundingLedger ledger;
        Require(TryReserveFounding(2, ledger) && TryReserveFounding(2, ledger), "two slots");
        Require(!TryReserveFounding(2, ledger), "both slots in flight");
        FinishFounding(ledger, false);
        Require(ledger.guilds == 0 && ledger.reserved == 1, "failure: no guild, slot free");
        Require(TryReserveFounding(2, ledger), "the free slot can be taken again");
        FinishFounding(ledger, false);
        FinishFounding(ledger, false);
        FinishFounding(ledger, false);
        Require(ledger.reserved == 0 && ledger.guilds == 0, "never below zero");
    }

    // Signing: truth table of the spec.
    Require(DecideSign(true, false, false, -1, 5) == SignDecision::DeclineFaction, "other faction: decline, even a real player");
    Require(DecideSign(false, false, false, -1, 5) == SignDecision::DeclineFaction, "other faction: decline");
    Require(DecideSign(true, true, true, 8, 1) == SignDecision::Accept, "real player of the faction: always accept");
    Require(DecideSign(false, true, true, -1, 8) == SignDecision::DeclineTargetReached, "target reached: decline bot charters");
    Require(DecideSign(false, true, false, -1, 0) == SignDecision::Accept, "no own signature: accept");
    Require(DecideSign(false, true, false, 3, 5) == SignDecision::Accept, "5 > 3: the signature moves to the fuller charter");
    Require(DecideSign(false, true, false, 5, 3) == SignDecision::DeclineNotBetter, "3 < 5: stays");
    Require(DecideSign(false, true, false, 4, 4) == SignDecision::DeclineNotBetter, "4 = 4: stays (no back and forth)");
    Require(std::string(SignDecisionName(SignDecision::DeclineNotBetter)) == "not_better", "trace name");

    // Names from the config.
    std::vector<std::string> const names = SplitNames(" \"Wardens of Elwynn, Northshire Vigil ,, Kezan Sparkwrights\" , ");
    Require(names.size() == 3, "three names, empty entries dropped");
    Require(names[0] == "Wardens of Elwynn" && names[1] == "Northshire Vigil" && names[2] == "Kezan Sparkwrights",
        "blanks and quotes around names dropped, inner blanks kept");
    Require(SplitNames("").empty() && SplitNames(" , ,\"\"").empty(), "no list: no names");
    Require(SplitNames("Brill Lanternwatch").size() == 1, "a single name");

    Require(!IsValidCharterName("Kezan's Sparkwrights"), "apostrophe rejected");
    Require(!IsValidCharterName(std::string(25, 'a')), "25 characters rejected");
    Require(IsValidCharterName(std::string(24, 'a')), "24 characters allowed");
    Require(IsValidCharterName("Kezan Sparkwrights"), "letters and blanks allowed");
    Require(!IsValidCharterName(""), "empty rejected");
    Require(!IsValidCharterName("Guild 2"), "digits rejected");

    Require(IsApproved(names, "Northshire Vigil") && !IsApproved(names, "Northshire"), "approved means listed");

    Require(PickName(names, { "Wardens of Elwynn" }) == "Northshire Vigil", "used names are skipped");
    Require(PickName(names, { "Wardens of Elwynn", "Northshire Vigil", "Kezan Sparkwrights" }).empty(), "none left: empty");
    Require(PickName(std::vector<std::string>(), {}).empty(), "no list: no name");
    Require(PickName(names, {}, [](std::string const& name) { return name != "Wardens of Elwynn"; }) == "Northshire Vigil",
        "a name the core refuses is skipped");
    Require(PickName(std::vector<std::string>{ "Bad'Name", "Brill Lanternwatch" }, {}) == "Brill Lanternwatch",
        "invalid approved names are skipped");
    {
        // Two foundings in flight never get the same name: the first one is reserved (used).
        std::set<std::string> used;
        std::string const first = PickName(names, used);
        used.insert(first);
        std::string const second = PickName(names, used);
        Require(!first.empty() && !second.empty() && first != second, "reserved names are not handed out twice");
    }

    Require(IsDue(1000, 0, 3600), "never traced: due");
    Require(!IsDue(1000, 900, 3600) && IsDue(4500, 900, 3600), "at most once per interval");

    // Recount interval: the configured value within 10-3600 s.
    Require(SnapshotInterval(60) == 60 && SnapshotInterval(10) == 10 && SnapshotInterval(3600) == 3600, "configured interval kept");
    Require(SnapshotInterval(0) == 10, "0 does not recount on every call");
    Require(SnapshotInterval(4294967295u) == 3600, "a negative config value (wrapped) does not freeze the counts");

    // Guild note (owner 04.10.): only with the switch, for roster bots in a guild.
    Require(!UsesGuildNote(false, true, true), "GuildNote 0 (default) leaves notes alone");
    Require(!UsesGuildNote(true, false, true), "only roster bots");
    Require(!UsesGuildNote(true, true, false), "only bots in a guild");
    Require(UsesGuildNote(true, true, true), "roster bot in a guild with the switch");

    Require(NoteRefreshInterval(300) == 300 && NoteRefreshInterval(60) == 60 && NoteRefreshInterval(3600) == 3600, "configured refresh kept");
    Require(NoteRefreshInterval(0) == 60, "0 does not check on every pass");
    Require(NoteRefreshInterval(4294967295u) == 3600, "a wrapped negative refresh does not stop the notes");

    // Format: the owner's examples.
    Require(FormatGuildNote(5, 1, false, 23) == "Priest Holy iLvl 23", "Priest Holy iLvl 23");
    Require(FormatGuildNote(1, 2, true, 31) == "Warrior Tank iLvl 31", "Warrior Tank iLvl 31");
    Require(FormatGuildNote(1, 0, true, 31) == "Warrior Tank iLvl 31", "the tank role wins over the tree");
    Require(FormatGuildNote(11, 1, true, 40) == "Druid Tank iLvl 40", "feral tank");
    Require(FormatGuildNote(7, 2, false, 45) == "Shaman Restoration iLvl 45", "healer tree by name");
    Require(FormatGuildNote(4, 0, false, 12) == "Rogue Assassination iLvl 12", "readable tree names");
    // Unknown spec or class: the part is left out, no double blank.
    Require(FormatGuildNote(8, -1, false, 5) == "Mage iLvl 5", "no talents yet: no tree");
    Require(FormatGuildNote(8, 3, false, 5) == "Mage iLvl 5", "tab out of range: no tree");
    Require(FormatGuildNote(6, 0, false, 10) == "iLvl 10", "unknown class: item level only");
    Require(FormatGuildNote(9, 2, false, 0) == "Warlock Destruction iLvl 0", "no items: iLvl 0");
    // 31-character limit: every class and tree fits even with a 3-digit item level ...
    for (std::uint8_t cls = 0; cls < 13; ++cls)
        for (int tab = -1; tab < 4; ++tab)
            for (bool tank : { false, true })
            {
                std::string const note = FormatGuildNote(cls, tab, tank, 999);
                Require(note.size() <= kGuildNoteMaxLength, "note within 31 characters");
                Require(!note.empty() && note.back() != ' ' && note.find("  ") == std::string::npos, "no stray blanks");
                Require(tank || !*NoteTreeName(cls, tab) || note.find(NoteTreeName(cls, tab)) != std::string::npos, "tree kept when it fits");
            }
    Require(FormatGuildNote(3, 0, false, 999) == "Hunter Beast Mastery iLvl 999", "longest tree fits (29)");
    // ... and a too long note drops the tree first instead of cutting the item level.
    Require(FormatGuildNote(3, 0, false, 4294967295u) == "Hunter iLvl 4294967295", "tree dropped when too long");

    // Average item level, rounded half up; empty slots are not passed in.
    Require(AverageItemLevel(0, 0) == 0, "no items: 0");
    Require(AverageItemLevel(45, 2) == 23 && AverageItemLevel(44, 2) == 22, "x.5 rounds up");
    Require(AverageItemLevel(67, 3) == 22 && AverageItemLevel(68, 3) == 23, "rounded to the nearest level");
    Require(AverageItemLevel(17 * 60, 17) == 60, "17 worn items at 60");

    std::cout << "roster_guild_policy_tests passed\n";
    return 0;
}
