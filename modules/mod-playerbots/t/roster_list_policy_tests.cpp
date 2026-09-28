#include "RosterListPolicy.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// twow-repo#419 variant B: `.bot roster` filters, visibility, paging, rate
// limit and the BL1 wire format the BotMenu addon parses. The golden file
// fixtures/roster_list_v1.txt is the addon contract: a change to it is a new
// protocol version, not an edit.

namespace
{
using namespace ai::roster_list;

void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

Entry Bot(std::string name, unsigned int classId, unsigned int raceId, unsigned int level, Role role,
    Faction faction, std::string zone, std::string guild = "")
{
    Entry e;
    e.name = std::move(name);
    e.classId = classId;
    e.raceId = raceId;
    e.level = level;
    e.role = role;
    e.online = true;
    e.faction = faction;
    e.zone = std::move(zone);
    e.guild = std::move(guild);
    return e;
}

// Sorted by name, as the server cache is.
std::vector<Entry> Roster()
{
    Entry aldric = Bot("Aldric", 1, 1, 60, Role::TANK, Faction::ALLIANCE, "Blackrock Mountain", "Iron Vanguard");
    aldric.group = GroupState::RAID;
    Entry brenna = Bot("Brenna", 5, 3, 58, Role::HEAL, Faction::ALLIANCE, "Ironforge");
    Entry corwin = Bot("Corwin", 8, 7, 12, Role::DPS, Faction::ALLIANCE, "Dun Morogh");
    Entry grom = Bot("Grukk", 1, 2, 60, Role::TANK, Faction::HORDE, "Orgrimmar");
    Entry gm = Bot("Gmchar", 2, 1, 60, Role::HEAL, Faction::ALLIANCE, "Stormwind City");
    gm.security = 3;
    Entry sleeper = Bot("Hild", 11, 4, 40, Role::DPS, Faction::ALLIANCE, "Darnassus");
    sleeper.online = false;
    return { aldric, brenna, corwin, gm, grom, sleeper };
}

Viewer Player(Faction faction = Faction::ALLIANCE)
{
    Viewer v;
    v.faction = faction;
    return v;
}

std::vector<std::string> ReadLines(std::string const& path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            lines.push_back(line);
    return lines;
}

std::vector<std::string> Split(std::string const& line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true)
    {
        size_t const end = line.find(';', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            return fields;
        start = end + 1;
    }
}

Filter Parsed(std::string const& args)
{
    Filter f;
    std::string error;
    Require(ParseFilter(args, f, error), "filter parses");
    return f;
}

bool Rejects(std::string const& args, std::string const& expected)
{
    Filter f;
    std::string error;
    return !ParseFilter(args, f, error) && error == expected;
}
}

int main()
{
    // Parsing.
    Filter f = Parsed("");
    Require(f.page == 1 && f.faction == FactionFilter::OWN && !f.includeOffline, "defaults: page 1, own faction, online only");
    f = Parsed("3");
    Require(f.page == 3, "a bare number is the page");
    f = Parsed("page=2 faction=ALL class=priest role=healer level=10-20 zone=Dun_Morogh status=all");
    Require(f.page == 2 && f.faction == FactionFilter::ALL && f.classId == 5 && f.role == Role::HEAL &&
        f.minLevel == 10 && f.maxLevel == 20 && f.zone == "dun morogh" && f.includeOffline, "every filter parses");
    f = Parsed("level=60 class=Druid");
    Require(f.minLevel == 60 && f.maxLevel == 60 && f.classId == 11, "single level and druid id 11");
    Require(Rejects("page=0", "page"), "page 0 is refused");
    Require(Rejects("level=20-10", "level"), "inverted level band is refused");
    Require(Rejects("class=deathknight", "class"), "no death knights on 1.12");
    Require(Rejects("role=bard", "role"), "unknown role is refused");
    Require(Rejects("faction=scourge", "faction"), "unknown faction is refused");
    Require(Rejects("account=admin", "unknown_key"), "unknown keys are refused, not ignored");
    Require(Rejects("page=99999999", "page"), "oversized numbers are refused");

    // Visibility: players never see GM characters or offline members they did not ask for.
    std::vector<Entry> const roster = Roster();
    size_t matched = 0;
    BuildPage(roster, Parsed(""), Player(), 50, &matched);
    Require(matched == 3, "own faction, online, no GM: Aldric, Brenna, Corwin");
    BuildPage(roster, Parsed("status=all"), Player(), 50, &matched);
    Require(matched == 4, "status=all adds the offline member");
    Viewer gm = Player();
    gm.gm = true;
    BuildPage(roster, Parsed("faction=all status=all"), gm, 50, &matched);
    Require(matched == 6, "a GM sees everything");
    BuildPage(roster, Parsed("faction=all"), Player(), 50, &matched);
    Require(matched == 4, "faction=all still hides the GM character from players");
    Require(!Matches(roster[3], Parsed("faction=all status=all"), Player()), "GM character hidden from rank 0");

    // Faction: another faction only where /who would show it.
    Require(!FactionFilterAllowed(Parsed("faction=horde"), Player()), "no cross-faction list without AllowTwoSide.WhoList");
    Require(FactionFilterAllowed(Parsed("faction=alliance"), Player()), "own faction by name is fine");
    Viewer twoSide = Player();
    twoSide.allowTwoSide = true;
    Require(FactionFilterAllowed(Parsed("faction=horde"), twoSide), "AllowTwoSide.WhoList opens the other faction");
    BuildPage(roster, Parsed(""), Player(Faction::HORDE), 50, &matched);
    Require(matched == 1, "a horde player sees horde bots by default");

    // Filters.
    BuildPage(roster, Parsed("role=tank faction=all"), gm, 50, &matched);
    Require(matched == 2, "role filter");
    BuildPage(roster, Parsed("class=mage"), Player(), 50, &matched);
    Require(matched == 1, "class filter");
    BuildPage(roster, Parsed("level=50-60"), Player(), 50, &matched);
    Require(matched == 2, "level band");
    BuildPage(roster, Parsed("zone=iron"), Player(), 50, &matched);
    Require(matched == 1, "zone substring, case-insensitive");

    // Paging and the addon contract.
    Require(ClampPageSize(0) == 1 && ClampPageSize(200) == kMaxPageSize && ClampPageSize(20) == 20, "page size is clamped to 1..50");
    Require(PageCount(0, 50) == 1 && PageCount(50, 50) == 1 && PageCount(51, 50) == 2 && PageCount(180, 50) == 4, "page count");

    std::vector<std::string> const page1 = BuildPage(roster, Parsed(""), Player(), 2);
    std::vector<std::string> const golden = ReadLines(ROSTER_LIST_FIXTURE);
    Require(!golden.empty(), "golden file is readable");
    Require(page1 == golden, "page 1 matches fixtures/roster_list_v1.txt byte for byte");

    std::vector<std::string> const page2 = BuildPage(roster, Parsed("page=2"), Player(), 2);
    Require(page2.size() == 3 && page2[0] == "BL1;H;2;2;3;1" && page2[2] == "BL1;Z;1", "page 2 header and end");
    Require(page2[1] == "BL1;E;Corwin;Mage;Gnome;12;dps;online;free;Dun Morogh;", "page 2 entry");
    std::vector<std::string> const beyond = BuildPage(roster, Parsed("page=3"), Player(), 2);
    Require(beyond.size() == 1 && beyond[0] == "BL1;X;page;0", "a page past the end is refused");

    std::vector<std::string> const empty = BuildPage(roster, Parsed("class=rogue"), Player(), 50);
    Require(empty.size() == 2 && empty[0] == "BL1;H;1;1;0;0" && empty[1] == "BL1;Z;0", "an empty result is still a well-formed page");

    // Every entry line splits into exactly 11 fields; hostile text cannot add one
    // or open a chat escape.
    Entry hostile = Bot("Evil", 4, 5, 30, Role::DPS, Faction::HORDE, "Zone;X", "Guild|cffff0000;E;Fake");
    std::string const line = EntryLine(hostile);
    Require(Split(line).size() == 11, "entry has 11 fields");
    Require(line.find('|') == std::string::npos, "no chat escape in an entry");
    Require(line == "BL1;E;Evil;Rogue;Undead;30;dps;online;free;Zone X;Guild cffff0000 E Fake", "sanitised entry");
    for (std::string const& l : page1)
        if (l.compare(0, 6, "BL1;E;") == 0)
            Require(Split(l).size() == 11, "golden entries have 11 fields");

    Require(ErrorLine("rate_limited", 1500) == "BL1;X;rate_limited;1500", "error line");
    Require(std::string(RaceToken(10)) == "HighElf" && std::string(RaceToken(9)) == "Goblin", "Turtle races");
    Require(std::string(ClassToken(6)) == "Unknown", "class 6 does not exist here");

    // Rate limit, per session.
    unsigned int retry = 0;
    Require(!RateLimited(10000, 0, 2000, retry) && retry == 0, "first request passes");
    Require(RateLimited(10500, 10000, 2000, retry) && retry == 1500, "second request within 2 s is refused with the wait");
    Require(!RateLimited(12000, 10000, 2000, retry), "after the interval it passes again");
    Require(!RateLimited(9000, 10000, 2000, retry), "a clock that went backwards does not lock the player out");
    Require(!RateLimited(10001, 10000, 0, retry), "interval 0 disables the limit");

    std::cout << "roster_list_policy_tests: OK\n";
    return 0;
}
