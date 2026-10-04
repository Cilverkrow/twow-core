#include "RosterGuildRolePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
using namespace ai::roster_guild_role;

void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Classes and races of the 1.12 client.
std::uint8_t const WARRIOR = 1, PALADIN = 2, HUNTER = 3, ROGUE = 4, PRIEST = 5, SHAMAN = 7, MAGE = 8, DRUID = 11;
std::uint8_t const HUMAN = 1, DWARF = 3, NIGHTELF = 4, GNOME = 7;

struct Roster
{
    std::vector<Member> members;
    std::uint32_t next = 1;

    Member& Add(Role role, std::uint8_t cls, std::uint8_t race, std::uint32_t guild = 0, std::uint32_t level = 1)
    {
        Member member;
        member.guid = next;
        member.ordinal = next;
        ++next;
        member.role = role;
        member.cls = cls;
        member.race = race;
        member.guild = guild;
        member.level = level;
        members.push_back(member);
        return members.back();
    }

    void Add(std::uint32_t count, Role role, std::uint8_t cls, std::uint8_t race, std::uint32_t guild = 0)
    {
        for (std::uint32_t i = 0; i < count; ++i)
            Add(role, cls, race, guild);
    }
};

// guild -> role -> count / guild -> (role, class) -> count after a deal.
struct Result
{
    std::map<std::uint32_t, std::map<Role, std::uint32_t>> roles;
    std::map<std::uint32_t, std::map<RoleClass, std::uint32_t>> classes;
    std::map<std::uint32_t, std::map<RaceClass, std::uint32_t>> combos;
    std::uint32_t unplaced = 0;
};

Result Count(Roster const& roster, std::map<std::uint32_t, std::uint32_t> const& assigned)
{
    Result result;
    for (Member const& member : roster.members)
    {
        auto it = assigned.find(member.guid);
        if (it == assigned.end())
        {
            ++result.unplaced;
            continue;
        }
        ++result.roles[it->second][member.role];
        ++result.classes[it->second][RoleClass(member.role, member.cls)];
        ++result.combos[it->second][RaceClass(member.race, member.cls)];
    }
    return result;
}

std::uint32_t HealerClasses(Result const& result, std::uint32_t guild)
{
    std::uint32_t classes = 0;
    auto it = result.classes.find(guild);
    if (it == result.classes.end())
        return 0;
    for (auto const& item : it->second)
        if (item.first.first == Role::Healer && item.second)
            ++classes;
    return classes;
}
}

int main()
{
    // Defaults: no quota, no plan = the behaviour of core#281.
    {
        Quota quota;
        Require(!QuotaActive(quota), "0/0/0 is no role quota");
        Require(!UsesRoleFill(true, quota, false), "no quota and no plan: no role fill");
        Require(UsesRoleFill(true, Quota{ 5, 10, 30 }, false), "a quota on the roster path fills by role");
        Require(UsesRoleFill(true, quota, true), "a plan file on the roster path fills by plan");
        Require(!UsesRoleFill(false, Quota{ 5, 10, 30 }, true), "off the roster path never");
        Require(QuotaFor(Quota{ 7, 10, 28 }, Role::Tank) == 7 && QuotaFor(Quota{ 7, 10, 28 }, Role::Dps) == 28, "quota per role");
        Require(QuotaFor(Quota{ 5, 10, 30 }, Role::Unknown) == 0, "unknown role has no slot");
        Switches switches;
        Require(!switches.healerClassMin && !switches.tankClassSpread && !switches.rareComboSpread, "switches default off");
    }

    // Role from the bot's spec bits; no talents = no role yet.
    Require(RoleFromBits(0x01, true) == Role::Tank, "tank bit");
    Require(RoleFromBits(0x02, true) == Role::Healer, "healer bit");
    Require(RoleFromBits(0x04, true) == Role::Dps, "dps bit");
    Require(RoleFromBits(0x01, false) == Role::Unknown, "no talents: unknown");
    Require(ParseRole("TANK") == Role::Tank && ParseRole("HEALER") == Role::Healer && ParseRole("DPS") == Role::Dps, "plan roles");
    Require(ParseRole("tank") == Role::Unknown, "plan roles are upper case as guilds.tsv");

    // guild_plan.py helpers: 5-level bands, ceil spread.
    Require(Band(1) == 0 && Band(5) == 0 && Band(6) == 1 && Band(60) == 11, "5-level bands");
    Require(SpreadCap(7, 2) == 4 && SpreadCap(8, 2) == 4 && SpreadCap(1, 4) == 1 && SpreadCap(0, 2) == 0, "spread cap");

    // 1. Role counts per guild per config: 2 guilds, 10/20/60 per faction, quota 5/10/30 -> 5/10/30 each.
    {
        Roster roster;
        roster.Add(4, Role::Tank, WARRIOR, HUMAN);
        roster.Add(3, Role::Tank, PALADIN, DWARF);
        roster.Add(3, Role::Tank, DRUID, NIGHTELF);
        roster.Add(8, Role::Healer, PRIEST, HUMAN);
        roster.Add(6, Role::Healer, PALADIN, HUMAN);
        roster.Add(4, Role::Healer, DRUID, NIGHTELF);
        roster.Add(2, Role::Healer, SHAMAN, DWARF);
        roster.Add(20, Role::Dps, MAGE, GNOME);
        roster.Add(20, Role::Dps, ROGUE, HUMAN);
        roster.Add(20, Role::Dps, HUNTER, DWARF);
        std::vector<std::uint32_t> const guilds = { 11, 12 };
        Result const result = Count(roster, Deal(roster.members, guilds, Quota{ 5, 10, 30 }, Switches(), 2));
        for (std::uint32_t guild : guilds)
        {
            Require(result.roles.at(guild).at(Role::Tank) == 5, "5 tanks per guild");
            Require(result.roles.at(guild).at(Role::Healer) == 10, "10 healers per guild");
            Require(result.roles.at(guild).at(Role::Dps) == 30, "30 DPS per guild");
            // guild_plan.py deals class by class, rarest first: every healer class in every guild.
            Require(HealerClasses(result, guild) == 4, "every healer class in every guild (enough of each)");
        }
        Require(result.unplaced == 0, "everybody placed");

        // The same roster at 7/10/28: 14 tank slots for 10 tanks, 56 DPS slots for 60 DPS.
        Result const other = Count(roster, Deal(roster.members, guilds, Quota{ 7, 10, 28 }, Switches(), 2));
        Require(other.roles.at(11).at(Role::Tank) == 5 && other.roles.at(12).at(Role::Tank) == 5, "7/10/28: the 10 tanks split 5/5");
        Require(other.roles.at(11).at(Role::Dps) == 28 && other.roles.at(12).at(Role::Dps) == 28, "7/10/28: 28 DPS each");
        Require(other.unplaced == 4, "7/10/28: 4 DPS without a slot stay without a guild");

        // Deterministic: the same input gives the same deal.
        Require(Deal(roster.members, guilds, Quota{ 5, 10, 30 }, Switches(), 2) == Deal(roster.members, guilds, Quota{ 5, 10, 30 }, Switches(), 2),
            "deterministic deal");
    }

    // 2. A role at 0 with a quota set gets no slot; unknown roles are never dealt.
    {
        Roster roster;
        roster.Add(2, Role::Tank, WARRIOR, HUMAN);
        roster.Add(1, Role::Unknown, MAGE, GNOME);
        roster.Add(2, Role::Dps, MAGE, GNOME);
        Result const result = Count(roster, Deal(roster.members, { 11 }, Quota{ 0, 0, 30 }, Switches(), 1));
        Require(result.roles.at(11).at(Role::Dps) == 2 && !result.roles.at(11).count(Role::Tank), "tank quota 0: no tank slot");
        Require(result.unplaced == 3, "2 tanks and 1 unknown stay out");
    }

    // 3. Tanks class by class as guild_plan.py, switches off (review 04.10, defect 1). Guild 11 was
    //    founded with 3 warrior tanks among the signers (kept); 4 warriors, 2 bears, 2 paladins, 2
    //    rogues and 1 shaman tank come; 7 tank slots per guild. guild_plan.py --keep --per-guild 7,10,28
    //    deals shaman -> 12, paladins 12/11, rogues 12/11, bears 12/11, then the 4 warriors to the
    //    guild with the fewest warriors: 12, 12, 12, 11 -> 11 = 4 warriors, 12 = 3 warriors.
    {
        Roster roster;
        roster.Add(3, Role::Tank, WARRIOR, HUMAN, 11);
        roster.Add(4, Role::Tank, WARRIOR, HUMAN);
        roster.Add(2, Role::Tank, DRUID, NIGHTELF);
        roster.Add(2, Role::Tank, PALADIN, DWARF);
        roster.Add(2, Role::Tank, ROGUE, HUMAN);
        roster.Add(1, Role::Tank, SHAMAN, DWARF);
        std::vector<std::uint32_t> const guilds = { 11, 12 };
        RoleClass const warrior(Role::Tank, WARRIOR);

        Result const off = Count(roster, Deal(roster.members, guilds, Quota{ 7, 10, 28 }, Switches(), 2));
        Require(off.classes.at(11).at(warrior) == 4 && off.classes.at(12).at(warrior) == 3, "switch off: warriors 4/3 as guild_plan.py (class count first)");
        for (std::uint32_t guild : guilds)
            for (std::uint8_t cls : { DRUID, PALADIN, ROGUE })
                Require(off.classes.at(guild).count(RoleClass(Role::Tank, cls)) == 1, "switch off: bear, paladin and rogue tank in every guild (guild_plan.py)");
        Require(off.classes.at(12).count(RoleClass(Role::Tank, SHAMAN)) == 1 && !off.classes.at(11).count(RoleClass(Role::Tank, SHAMAN)),
            "switch off: the single shaman tank goes to the guild with fewer tanks (guild_plan.py)");
        Require(off.roles.at(11).at(Role::Tank) == 7 && off.roles.at(12).at(Role::Tank) == 7, "switch off: 7 tanks each");

        Switches on;
        on.tankClassSpread = true;
        Result const spread = Count(roster, Deal(roster.members, guilds, Quota{ 7, 10, 28 }, on, 2));
        Require(spread.classes.at(11).at(warrior) <= 4 && spread.classes.at(12).at(warrior) >= 3, "switch on: at most ceil(7/2) = 4 warrior tanks per guild");
        for (std::uint32_t guild : guilds)
            for (std::uint8_t cls : { DRUID, PALADIN, ROGUE })
                Require(spread.classes.at(guild).count(RoleClass(Role::Tank, cls)) == 1, "switch on: bear, paladin and rogue tank in every guild");
        Require(spread.roles.at(11).at(Role::Tank) == 7 && spread.roles.at(12).at(Role::Tank) == 7, "switch on: role counts unchanged");
        // All target guilds exist and guild_plan.py already spreads: the switch changes nothing here.
        Require(Deal(roster.members, guilds, Quota{ 7, 10, 28 }, on, 2) == Deal(roster.members, guilds, Quota{ 7, 10, 28 }, Switches(), 2),
            "switch on, every guild founded: the same deal as guild_plan.py");

        // Charter (signing): 4 warrior tanks have signed, a 5th warrior tank offers; faction has 7, 2 guilds.
        FactionStats const stats = MakeStats(roster.members, 2);
        GuildCounts charter;
        for (int i = 0; i < 4; ++i)
            AddToCounts(charter, roster.members[3 + i]);
        Member candidate = roster.members[0];
        candidate.guild = 0;
        Require(CheckFit(charter, candidate, Quota{ 7, 10, 28 }, Switches(), stats) == Fit::Ok, "switch off: the 5th warrior tank signs");
        Require(CheckFit(charter, candidate, Quota{ 7, 10, 28 }, on, stats) == Fit::TankClassSpread, "switch on: the 5th warrior tank does not sign");
        Require(CheckFit(charter, roster.members[7], Quota{ 7, 10, 28 }, on, stats) == Fit::Ok, "switch on: a bear still signs");
        Require(CheckFit(charter, candidate, Quota{ 4, 10, 28 }, Switches(), stats) == Fit::RoleFull, "tank slots full at quota 4");
    }

    // 4. HealerClassMin: guild 11 has 2 priest healers (signers, kept), 4 healer slots each.
    //    2 shamans, 2 druids, 2 priests come.
    {
        Roster roster;
        roster.Add(2, Role::Healer, PRIEST, HUMAN, 11);
        roster.Add(2, Role::Healer, SHAMAN, DWARF);
        roster.Add(2, Role::Healer, DRUID, NIGHTELF);
        roster.Add(2, Role::Healer, PRIEST, HUMAN);
        std::vector<std::uint32_t> const guilds = { 11, 12 };

        // guild_plan.py --keep --per-guild 0,4,0 (healers): shamans 12/11, druids 12/11, priests 12/12.
        Result const off = Count(roster, Deal(roster.members, guilds, Quota{ 0, 4, 0 }, Switches(), 2));
        for (std::uint32_t guild : guilds)
            Require(off.classes.at(guild).at(RoleClass(Role::Healer, SHAMAN)) == 1 && off.classes.at(guild).at(RoleClass(Role::Healer, DRUID)) == 1,
                "switch off: one shaman and one druid in every guild (guild_plan.py)");
        Require(off.classes.at(11).at(RoleClass(Role::Healer, PRIEST)) == 2 && off.classes.at(12).at(RoleClass(Role::Healer, PRIEST)) == 2,
            "switch off: priests 2/2 (guild_plan.py)");
        Require(off.roles.at(11).at(Role::Healer) == 4 && off.roles.at(12).at(Role::Healer) == 4, "switch off: 4 healers each");

        Switches on;
        on.healerClassMin = true;
        Result const result = Count(roster, Deal(roster.members, guilds, Quota{ 0, 4, 0 }, on, 2));
        Require(HealerClasses(result, 11) == 3 && HealerClasses(result, 12) == 3, "switch on: priest, shaman and druid in every guild");
        Require(result.roles.at(11).at(Role::Healer) == 4 && result.roles.at(12).at(Role::Healer) == 4, "switch on: 4 healers each");

        // Charter: 3 priests signed, 4 healer slots, a shaman and a druid still without a guild: the
        // 4th priest would take the last slot a missing class needs.
        FactionStats const stats = MakeStats(roster.members, 2);
        GuildCounts charter;
        for (int i = 0; i < 3; ++i)
        {
            Member priest = roster.members[0];
            priest.guild = 0;
            AddToCounts(charter, priest);
        }
        Member priest = roster.members[6];
        Require(CheckFit(charter, priest, Quota{ 0, 4, 0 }, on, stats) == Fit::HealerClassReserved, "switch on: the last healer slot waits for a missing class");
        Require(CheckFit(charter, priest, Quota{ 0, 4, 0 }, Switches(), stats) == Fit::Ok, "switch off: the 4th priest signs");
        Require(CheckFit(charter, roster.members[2], Quota{ 0, 5, 0 }, on, stats) == Fit::Ok, "switch on: a shaman fills a missing class");
    }

    // 5. RareComboSpread: 2 dwarf shamans in the faction, 2 guilds -> one per guild.
    {
        Roster roster;
        roster.Add(Role::Healer, SHAMAN, DWARF, 11);
        roster.Add(Role::Healer, PRIEST, HUMAN, 12);
        roster.Add(Role::Dps, SHAMAN, DWARF);
        std::vector<std::uint32_t> const guilds = { 11, 12 };
        RaceClass const dwarfShaman(DWARF, SHAMAN);

        Result const off = Count(roster, Deal(roster.members, guilds, Quota{ 5, 10, 30 }, Switches(), 2));
        Require(off.combos.at(11).at(dwarfShaman) == 2, "switch off: both dwarf shamans in guild 11 (first guild)");

        Switches on;
        on.rareComboSpread = true;
        Result const spread = Count(roster, Deal(roster.members, guilds, Quota{ 5, 10, 30 }, on, 2));
        Require(spread.combos.at(11).at(dwarfShaman) == 1 && spread.combos.at(12).at(dwarfShaman) == 1, "switch on: one dwarf shaman per guild");

        FactionStats const stats = MakeStats(roster.members, 2);
        GuildCounts charter;
        AddToCounts(charter, roster.members[0]);
        Require(CheckFit(charter, roster.members[2], Quota{ 5, 10, 30 }, on, stats) == Fit::RareComboSpread, "switch on: second dwarf shaman does not sign the same charter");
        Require(CheckFit(charter, roster.members[2], Quota{ 5, 10, 30 }, Switches(), stats) == Fit::Ok, "switch off: it signs");
    }

    // 6. No deadlock: when no guild fits the spread rules, the role slot alone counts.
    {
        Roster roster;
        // Guild 11 is full (5 bear tanks), guild 12 already has 4 warrior tanks (cap ceil(5/2) = 3).
        roster.Add(5, Role::Tank, DRUID, NIGHTELF, 11);
        roster.Add(4, Role::Tank, WARRIOR, HUMAN, 12);
        roster.Add(1, Role::Tank, WARRIOR, HUMAN);
        Switches on;
        on.tankClassSpread = true;
        Result const result = Count(roster, Deal(roster.members, { 11, 12 }, Quota{ 5, 10, 30 }, on, 2));
        Require(result.unplaced == 0, "a 5th warrior tank still gets a slot above the spread cap");
        // While a third guild of the target can still come, the cap stays hard: the warrior waits.
        Result const waiting = Count(roster, Deal(roster.members, { 11, 12 }, Quota{ 5, 10, 30 }, on, 3));
        Require(waiting.unplaced == 1, "target 3, 2 guilds founded: the warrior above the cap waits for the third guild");
    }

    // 6b. Build-up of core#281 (review 04.10, defect 2): target 2 guilds, only guild 11 founded yet.
    //     7 warrior, 2 paladin and 1 bear tank without a guild, 7 tank slots per guild.
    {
        Roster roster;
        roster.Add(7, Role::Tank, WARRIOR, HUMAN);
        roster.Add(2, Role::Tank, PALADIN, DWARF);
        roster.Add(1, Role::Tank, DRUID, NIGHTELF);
        RoleClass const warrior(Role::Tank, WARRIOR);
        RoleClass const paladin(Role::Tank, PALADIN);

        // Switch off: only the role slot counts, the first guild takes 7 (bear, 2 paladins, 4 warriors).
        Result const off = Count(roster, Deal(roster.members, { 11 }, Quota{ 7, 10, 28 }, Switches(), 2));
        Require(off.roles.at(11).at(Role::Tank) == 7 && off.classes.at(11).at(paladin) == 2 && off.unplaced == 3,
            "switch off, 1 of 2 guilds: 7 tanks by role slot alone");

        // Switch on: caps ceil(7/2) = 4 warriors and ceil(2/2) = 1 paladin hold although guild 12 is missing.
        Switches on;
        on.tankClassSpread = true;
        std::map<std::uint32_t, std::uint32_t> const first = Deal(roster.members, { 11 }, Quota{ 7, 10, 28 }, on, 2);
        Result const early = Count(roster, first);
        Require(early.classes.at(11).at(paladin) == 1, "switch on, 1 of 2 guilds: the second paladin does not go to guild 11");
        Require(early.classes.at(11).at(warrior) == 4, "switch on, 1 of 2 guilds: at most 4 warrior tanks in guild 11");
        Require(early.roles.at(11).at(Role::Tank) == 6 && early.unplaced == 4, "switch on, 1 of 2 guilds: the rest waits without a guild");

        // Guild 12 is founded: the members dealt to guild 11 joined it; the rest goes to guild 12.
        Roster later = roster;
        for (Member& member : later.members)
        {
            auto it = first.find(member.guid);
            if (it != first.end())
                member.guild = it->second;
        }
        Result const second = Count(later, Deal(later.members, { 11, 12 }, Quota{ 7, 10, 28 }, on, 2));
        Require(second.classes.at(12).at(paladin) == 1 && second.classes.at(12).at(warrior) == 3, "guild 12 gets the waiting paladin and 3 warriors");
        Require(second.classes.at(11).at(paladin) == 1 && second.unplaced == 0, "one paladin per guild, everybody placed");
    }

    // 7. Level band as guild_plan.py: equal counts, the guild with fewer of the band wins.
    {
        Roster roster;
        roster.Add(Role::Dps, MAGE, GNOME, 11, 31);
        roster.Add(Role::Dps, ROGUE, HUMAN, 12, 10);
        roster.Add(Role::Dps, HUNTER, DWARF, 0, 33);
        std::map<std::uint32_t, std::uint32_t> const assigned = Deal(roster.members, { 11, 12 }, Quota{ 5, 10, 30 }, Switches(), 2);
        Require(assigned.at(roster.members[2].guid) == 12, "the level 33 hunter goes to the guild without a member of level 31-35");
    }

    // 8. Plan file (guilds.tsv of OB-40): header, tabs, CRLF, malformed lines.
    {
        std::vector<std::string> const lines = {
            "ordinal\tguid\tfaction\tguild\trole\tclass\trace",
            "1\t1001\tA\tA1\tTANK\t1\t1\r",
            "2\t1002\tA\tA2\tHEALER\t5\t1",
            "3\t1003\tH\tH1\tDPS\t8\t5",
            "",
            "# comment",
            "4\t1004\tX\tA1\tDPS\t8\t1",
            "5\t1005\tA\tA1\tHEALS\t5\t1",
            "6\tabc\tA\tA1\tDPS\t8\t1",
        };
        std::uint32_t rejected = 0;
        std::map<std::uint32_t, PlanEntry> const plan = ParsePlan(lines, rejected);
        Require(plan.size() == 3, "3 valid plan rows");
        Require(rejected == 3, "3 malformed rows counted");
        Require(plan.at(1001).guild == "A1" && plan.at(1001).role == Role::Tank && plan.at(1001).cls == 1 && plan.at(1001).race == 1, "row with CRLF");
        Require(plan.at(1003).faction == "H" && plan.at(1003).role == Role::Dps, "horde row");

        // A guild belongs to its leader's label; the lowest guild id wins a shared label.
        std::map<std::string, std::uint32_t> const labels = LabelGuilds({ { 21, "A1" }, { 20, "A1" }, { 22, "A2" }, { 23, "" } });
        Require(labels.at("A1") == 20 && labels.at("A2") == 22 && labels.size() == 2, "label -> guild");
        Require(SamePlanGuild("A1", "A1") && !SamePlanGuild("A1", "A2"), "charter of the same plan guild only");
        Require(SamePlanGuild("", "A2") && SamePlanGuild("A1", ""), "bots without a plan row are not ruled by the plan");
    }

    // 9. Owner tank mix (assignment v3, config default "1:2-3,11:1,4:1,2:1,7:1"): 2-3 warrior tanks
    //    and one bear, rogue, paladin and shaman tank per guild, as far as the faction has them.
    {
        TankMix const mix = ParseTankMix("1:2-3,11:1,4:1,2:1,7:1");
        Require(mix.size() == 5, "five tank classes in the owner mix");
        Require(mix.at(WARRIOR) == std::make_pair(2u, 3u), "warriors 2-3");
        Require(mix.at(DRUID) == std::make_pair(1u, 0u) && mix.at(ROGUE) == std::make_pair(1u, 0u) &&
            mix.at(PALADIN) == std::make_pair(1u, 0u) && mix.at(SHAMAN) == std::make_pair(1u, 0u), "one bear, rogue, paladin and shaman, no cap");
        TankMix const sloppy = ParseTankMix(" 1 : 2 - 3 , x:1, 12:1, 4:3-2, \"7:1\" ");
        Require(sloppy.size() == 2 && sloppy.at(WARRIOR) == std::make_pair(2u, 3u) && sloppy.count(SHAMAN),
            "spaces and quotes ignored; bad class, class 12 and max < min skipped");
        Require(ParseTankMix("").empty(), "empty = no mix");

        // Faction: 14 tanks, 6 warriors and 2 each of bear, rogue, paladin, shaman; 2 guilds of 7 tanks.
        Roster roster;
        roster.Add(6, Role::Tank, WARRIOR, HUMAN);
        roster.Add(2, Role::Tank, DRUID, NIGHTELF);
        roster.Add(2, Role::Tank, ROGUE, HUMAN);
        roster.Add(2, Role::Tank, PALADIN, DWARF);
        roster.Add(2, Role::Tank, SHAMAN, DWARF);
        std::vector<std::uint32_t> const guilds = { 11, 12 };
        Switches owner;
        owner.tankMix = mix;
        Result const dealt = Count(roster, Deal(roster.members, guilds, Quota{ 7, 10, 28 }, owner, 2));
        for (std::uint32_t guild : guilds)
        {
            Require(dealt.roles.at(guild).at(Role::Tank) == 7, "owner mix: 7 tanks per guild");
            Require(dealt.classes.at(guild).at(RoleClass(Role::Tank, WARRIOR)) == 3, "owner mix: 3 warrior tanks per guild");
            for (std::uint8_t cls : { DRUID, ROGUE, PALADIN, SHAMAN })
                Require(dealt.classes.at(guild).at(RoleClass(Role::Tank, cls)) == 1, "owner mix: one bear, rogue, paladin and shaman tank per guild");
        }

        // Charter: 3 warrior tanks have signed; a 4th warrior hits the class maximum.
        FactionStats const stats = MakeStats(roster.members, 2);
        GuildCounts charter;
        for (int i = 0; i < 3; ++i)
            AddToCounts(charter, roster.members[i]);
        Member warrior = roster.members[3];
        Require(CheckFit(charter, warrior, Quota{ 7, 10, 28 }, owner, stats) == Fit::TankClassMix, "owner mix: no 4th warrior tank");
        Require(CheckFit(charter, warrior, Quota{ 7, 10, 28 }, Switches(), stats) == Fit::Ok, "negative probe: without the mix the 4th warrior signs");
        Require(std::string(FitName(Fit::TankClassMix)) == "tank_class_mix", "trace reason");

        // Charter with 3 warriors, a rogue and a paladin (5 of 7): the 2 free slots stay for the bear and
        // the shaman the faction still has, so a 2nd rogue does not sign ...
        AddToCounts(charter, roster.members[8]);   // rogue
        AddToCounts(charter, roster.members[10]);  // paladin
        Member rogue = roster.members[9];
        Require(CheckFit(charter, rogue, Quota{ 7, 10, 28 }, owner, stats) == Fit::TankClassMix, "owner mix: slots kept for bear and shaman");
        Require(CheckFit(charter, roster.members[6], Quota{ 7, 10, 28 }, owner, stats) == Fit::Ok, "owner mix: the bear signs");
        // ... unless the faction has no unplaced shaman tank left: then only the bear's slot is kept.
        FactionStats noShaman = stats;
        noShaman.unplacedTanks.erase(SHAMAN);
        Require(CheckFit(charter, rogue, Quota{ 7, 10, 28 }, owner, noShaman) == Fit::Ok, "owner mix: only as far as the faction has them");
    }

    // 10. Owner rare pairs (config default "3:7,3:9,5:2", share 0.025): at most round(share x guild
    //     size), at least 1, of a listed race x class pair per guild.
    {
        std::uint8_t const WARLOCK = 9;
        std::set<RaceClass> const pairs = ParseRarePairs("3:7,3:9,5:2");
        Require(pairs.size() == 3 && pairs.count(RaceClass(DWARF, SHAMAN)) && pairs.count(RaceClass(DWARF, WARLOCK)) && pairs.count(RaceClass(5, PALADIN)),
            "dwarf shaman, dwarf warlock, undead paladin");
        Require(ParseRarePairs("3:7, bad, 0:2, 5:\"2\"").size() == 2 && ParseRarePairs("").empty(), "malformed and race 0 skipped; empty = none");
        Require(RarePairCapFor(0.025f, 45) == 1, "2.5 % of 45 = 1 per guild");
        Require(RarePairCapFor(0.025f, 90) == 2, "2.5 % of 90 = 2 per guild");
        Require(RarePairCapFor(0.001f, 45) == 1, "at least 1");
        Require(RarePairCapFor(0.0f, 45) == 0 && RarePairCapFor(0.025f, 0) == 0, "no share or no guild size: no cap");

        // 3 dwarf warlocks and 6 human mages (DPS), 2 guilds of 4 DPS.
        Roster roster;
        roster.Add(3, Role::Dps, WARLOCK, DWARF);
        roster.Add(6, Role::Dps, MAGE, HUMAN);
        std::vector<std::uint32_t> const guilds = { 11, 12 };
        Switches owner;
        owner.rarePairs = pairs;
        owner.rarePairCap = 1;
        RaceClass const dwarfWarlock(DWARF, WARLOCK);

        // A 3rd target guild is still missing: the 3rd dwarf warlock waits for it.
        Result const waiting = Count(roster, Deal(roster.members, guilds, Quota{ 0, 0, 4 }, owner, 3));
        Require(waiting.combos.at(11).at(dwarfWarlock) == 1 && waiting.combos.at(12).at(dwarfWarlock) == 1, "one dwarf warlock per guild");
        Require(waiting.unplaced == 1, "the 3rd dwarf warlock waits for the missing guild");
        Result const plain = Count(roster, Deal(roster.members, guilds, Quota{ 0, 0, 4 }, Switches(), 3));
        Require(plain.unplaced == 1 && (plain.combos.at(11).at(dwarfWarlock) == 2 || plain.combos.at(12).at(dwarfWarlock) == 2),
            "negative probe: without the cap a guild takes 2 dwarf warlocks (a mage waits instead)");

        // Every target guild exists: nobody waits, the cap steps back to the role slot.
        Result const all = Count(roster, Deal(roster.members, guilds, Quota{ 0, 0, 5 }, owner, 2));
        Require(all.unplaced == 0, "all guilds founded: the 3rd dwarf warlock still gets a role slot");

        GuildCounts charter;
        AddToCounts(charter, roster.members[0]);
        FactionStats const stats = MakeStats(roster.members, 2);
        Require(CheckFit(charter, roster.members[1], Quota{ 0, 0, 4 }, owner, stats) == Fit::RarePairCap, "a 2nd dwarf warlock does not sign");
        Require(CheckFit(charter, roster.members[3], Quota{ 0, 0, 4 }, owner, stats) == Fit::Ok, "a mage signs");
        Require(std::string(FitName(Fit::RarePairCap)) == "rare_pair_cap", "trace reason");
    }

    std::cout << "ROSTER_GUILD_ROLE_POLICY=PASS\n";
    return 0;
}
