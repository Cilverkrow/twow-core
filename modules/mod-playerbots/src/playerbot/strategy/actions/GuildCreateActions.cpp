
#include "playerbot/playerbot.h"
#include "GuildCreateActions.h"
#include "playerbot/RandomPlayerbotFactory.h"
#include "playerbot/LootObjectStack.h"
#ifndef MANGOSBOT_ZERO
#ifdef CMANGOS
#include "Arena/ArenaTeam.h"
#endif
#ifdef MANGOS
#include "ArenaTeam.h"
#endif
#endif
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/RosterGuildPolicy.h"
#include "playerbot/RosterGuildRolePolicy.h"
#include "playerbot/AiFactory.h"
#include "Guild/GuildMgr.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>

using namespace ai;

// --- twow-repo#485: roster guild plan ---------------------------------------------------------
// Counts per faction for the new path (AiPlayerbot.RosterGuild.BotsPerGuild > 0). Roster size
// from the configured roster and the player cache (never another thread's Player*), guilds and
// petitions as copies from GuildMgr. No SQL; one recount per RosterGuild.SnapshotSeconds and one
// after each founding: <= 360 cache lookups plus two copies of the guild and petition lists.
namespace
{
    // A failed turn-in at a guild master is retried after this many seconds.
    uint32 const RosterGuildRetrySeconds = 600;

    struct RosterGuildFaction
    {
        uint32 roster = 0;
        uint32 target = 0;
        roster_guild::FoundingLedger ledger;    // bot guilds + foundings in flight
        uint32 openCharters = 0;                // open charters of roster bots, purchases since the recount
        uint32 freeNames = 0;
        uint32 traced[5] = { 0, 0, 0, 0, 0 };   // last event=plan line
        bool hasTraced = false;
    };

    struct RosterGuildState
    {
        std::mutex lock;
        time_t builtAt = 0;
        bool dirty = true;
        RosterGuildFaction faction[2];          // 0 alliance, 1 horde
        std::set<std::string> guildNames;       // every guild
        std::set<std::string> charterNames;     // every open charter, purchases since the recount
        std::set<std::string> reservedNames;    // foundings in flight

        // twow-repo#485 / #518: role fill (RosterGuild.Tanks/Healers/Dps, PlanFile). Built at the
        // recount from the player cache, GuildMgr copies and the bots' own role reports.
        std::unordered_map<uint32, uint8> selfRoles;                        // guid -> Role the bot reported itself
        std::map<uint32, roster_guild_role::PlanEntry> plan;                // guid -> row of the plan file
        std::shared_ptr<const std::vector<std::string>> planSource;         // the plan lines `plan` was parsed from
        std::unordered_map<uint32, roster_guild_role::Member> members;      // roster guid -> class, race, level, role, guild
        std::unordered_map<uint32, uint32> assigned;                        // roster guid -> guild it is dealt to
        roster_guild_role::FactionStats stats[2];
        std::set<std::string> foundedLabels;                                // plan guilds with a guild
        std::set<std::string> charterLabels;                                // plan guilds with an open charter
        std::map<uint32, std::array<uint32, 6>> tracedRoles;                // guild -> last event=roles line
        std::array<uint32, 5> tracedDeal[2] = {};                            // last event=deal line per faction
        std::array<uint32, 2> tracedPlan = {};                               // last event=plan_rows line
    };

    RosterGuildState& GetRosterGuildState()
    {
        static RosterGuildState state;
        return state;
    }

    uint8 RosterGuildFactionIndex(Team team)
    {
        return team == ALLIANCE ? 0 : 1;
    }

    char const* RosterGuildFactionName(Team team)
    {
        return team == ALLIANCE ? "alliance" : "horde";
    }

    std::vector<std::string> RosterGuildNames(uint8 faction)
    {
        return roster_guild::SplitNames(faction == 0 ? sPlayerbotAIConfig.rosterGuildNamesAlliance : sPlayerbotAIConfig.rosterGuildNamesHorde);
    }

    // The core's own checks of a charter name (CMSG_PETITION_BUY, MSG_PETITION_RENAME).
    bool CoreAcceptsCharterName(std::string const& name)
    {
        return ObjectMgr::IsValidCharterName(name) && !sObjectMgr.IsReservedName(name);
    }

    // An approved name no guild, no open charter and no founding in flight uses.
    std::string PickFreeRosterGuildName(RosterGuildState const& state, std::vector<std::string> const& names)
    {
        return roster_guild::PickName(names, state.charterNames, [&state](std::string const& name)
        {
            return !state.guildNames.count(name) && !state.reservedNames.count(name) && CoreAcceptsCharterName(name);
        });
    }

    // The charter keeps its own name when it is approved and no guild and no other founding uses it.
    bool KeepsCharterName(RosterGuildState const& state, std::vector<std::string> const& names, std::string const& name)
    {
        return roster_guild::IsApproved(names, name) && !state.guildNames.count(name) && !state.reservedNames.count(name) &&
            CoreAcceptsCharterName(name);
    }

    // --- twow-repo#485 / #518: role fill of roster guilds (RosterGuildRolePolicy.h) ---------------
    roster_guild_role::Quota RosterGuildQuota()
    {
        roster_guild_role::Quota quota;
        quota.tanks = sPlayerbotAIConfig.rosterGuildTanks;
        quota.healers = sPlayerbotAIConfig.rosterGuildHealers;
        quota.dps = sPlayerbotAIConfig.rosterGuildDps;
        return quota;
    }

    roster_guild_role::Switches RosterGuildSwitches()
    {
        roster_guild_role::Switches switches;
        switches.healerClassMin = sPlayerbotAIConfig.rosterGuildHealerClassMin;
        switches.tankClassSpread = sPlayerbotAIConfig.rosterGuildTankClassSpread;
        switches.rareComboSpread = sPlayerbotAIConfig.rosterGuildRareComboSpread;
        // Owner rules (assignment v3): tank class mix and rare race x class pairs. Parsed from short
        // config strings on each call (a recount or a charter decision, never per tick).
        switches.tankMix = roster_guild_role::ParseTankMix(sPlayerbotAIConfig.rosterGuildTankClassMix);
        switches.rarePairs = roster_guild_role::ParseRarePairs(sPlayerbotAIConfig.rosterGuildRarePairs);
        roster_guild_role::Quota const quota = RosterGuildQuota();
        switches.rarePairCap = switches.rarePairs.empty() ? 0 :
            roster_guild_role::RarePairCapFor(sPlayerbotAIConfig.rosterGuildRarePairMaxShare, quota.tanks + quota.healers + quota.dps);
        return switches;
    }

    // A plan file with lines is loaded (a snapshot of the published lines, see PlayerbotAIConfig.h).
    bool RosterGuildPlanLoaded()
    {
        std::shared_ptr<const std::vector<std::string>> const lines = sPlayerbotAIConfig.RosterGuildPlanLines();
        return lines && !lines->empty();
    }

    // A quota or a plan file is set (the roster path is checked by the callers).
    bool RoleFillConfigured()
    {
        return roster_guild_role::QuotaActive(RosterGuildQuota()) || RosterGuildPlanLoaded();
    }

    // Caller holds state.lock. The plan label of a bot, "" = not in the plan.
    std::string PlanLabel(RosterGuildState const& state, uint32 guid)
    {
        auto it = state.plan.find(guid);
        return it == state.plan.end() ? std::string() : it->second.guild;
    }

    // Caller holds state.lock. Role: the plan's, else the bot's own report, else unknown.
    roster_guild_role::Role RoleOf(RosterGuildState const& state, uint32 guid)
    {
        auto planIt = state.plan.find(guid);
        if (planIt != state.plan.end())
            return planIt->second.role;
        auto selfIt = state.selfRoles.find(guid);
        return selfIt == state.selfRoles.end() ? roster_guild_role::Role::Unknown : roster_guild_role::Role(selfIt->second);
    }

    // Caller holds state.lock. Class, race and level from the last recount, the role as of now.
    roster_guild_role::Member MemberOf(RosterGuildState const& state, uint32 guid)
    {
        auto it = state.members.find(guid);
        roster_guild_role::Member member = it == state.members.end() ? roster_guild_role::Member() : it->second;
        member.guid = guid;
        member.role = RoleOf(state, guid);
        return member;
    }

    // Caller holds state.lock. Plan mode: the bot's plan guild has no guild yet (and, for a purchase,
    // no open charter either). Bots without a plan row are not ruled by the plan.
    bool PlanLabelOpen(RosterGuildState const& state, uint32 guid, bool countCharters)
    {
        std::string const label = PlanLabel(state, guid);
        return label.empty() || (!state.foundedLabels.count(label) && (!countCharters || !state.charterLabels.count(label)));
    }

    // Caller holds state.lock (RecountRosterGuilds). Deals every roster bot of a faction to its guild:
    // plan rows first (the guild of the plan label), then the quota deal for everybody else. Members
    // come with class, race and level from the player cache; their guild is a GuildMgr copy.
    void DealRosterGuildRoles(RosterGuildState& state, std::vector<roster_guild_role::Member> (&members)[2],
        std::vector<GuildSummary> const& guilds, std::vector<PetitionSummary> const& petitions,
        std::unordered_map<uint32, uint8> const& rosterFaction)
    {
        // Review 04.10 (reload): a local snapshot of the published plan lines; parsed again only when
        // a config reload published new lines, not at every recount.
        std::shared_ptr<const std::vector<std::string>> const planLines = sPlayerbotAIConfig.RosterGuildPlanLines();
        if (planLines != state.planSource)
        {
            state.planSource = planLines;
            uint32 rejected = 0;
            if (planLines)
                state.plan = roster_guild_role::ParsePlan(*planLines, rejected);
            else
                state.plan.clear();
            if (state.tracedPlan[0] != uint32(state.plan.size()) || state.tracedPlan[1] != rejected)
            {
                state.tracedPlan[0] = uint32(state.plan.size());
                state.tracedPlan[1] = rejected;
                sLog.outBasic("[RosterGuild] event=plan_rows rows=%u rejected=%u", uint32(state.plan.size()), rejected);
            }
        }

        state.members.clear();
        state.assigned.clear();
        state.foundedLabels.clear();
        state.charterLabels.clear();

        // Roster guilds of each faction (the leader is a roster bot), ascending id = deal order.
        std::vector<uint32> factionGuilds[2];
        std::vector<std::pair<uint32, std::string>> leaderLabels;
        for (GuildSummary const& guild : guilds)
        {
            auto it = rosterFaction.find(guild.leaderGuid.GetCounter());
            if (it == rosterFaction.end())
                continue;
            factionGuilds[it->second].push_back(guild.id);
            std::string const label = PlanLabel(state, guild.leaderGuid.GetCounter());
            if (!label.empty())
            {
                leaderLabels.push_back(std::make_pair(guild.id, label));
                state.foundedLabels.insert(label);
            }
        }
        for (PetitionSummary const& petition : petitions)
        {
            std::string const label = PlanLabel(state, petition.ownerGuid.GetCounter());
            if (!label.empty())
                state.charterLabels.insert(label);
        }
        std::map<std::string, uint32> const labelGuild = roster_guild_role::LabelGuilds(leaderLabels);

        roster_guild_role::Quota const quota = RosterGuildQuota();
        roster_guild_role::Switches const switches = RosterGuildSwitches();
        for (uint8 f = 0; f < 2; ++f)
        {
            std::sort(factionGuilds[f].begin(), factionGuilds[f].end());

            std::vector<roster_guild_role::Member> quotaMembers;
            uint32 unknownRole = 0;
            for (roster_guild_role::Member& member : members[f])
            {
                member.guild = sGuildMgr.GetPlayerGuildId(member.guid);
                member.role = RoleOf(state, member.guid);
                auto planIt = state.plan.find(member.guid);
                member.ordinal = planIt != state.plan.end() ? planIt->second.ordinal : member.guid;
                state.members[member.guid] = member;
                if (member.role == roster_guild_role::Role::Unknown)
                    ++unknownRole;

                if (planIt != state.plan.end())
                {
                    auto labelIt = labelGuild.find(planIt->second.guild);
                    if (labelIt != labelGuild.end())
                        state.assigned[member.guid] = labelIt->second;
                    if (!member.guild)
                        continue;   // plan bots without a guild follow the plan, not the quota deal
                }
                quotaMembers.push_back(member);
            }

            uint32 const target = std::max<uint32>(state.faction[f].target, uint32(factionGuilds[f].size()));
            state.stats[f] = roster_guild_role::MakeStats(members[f], target);
            if (roster_guild_role::QuotaActive(quota))
                for (auto const& item : roster_guild_role::Deal(quotaMembers, factionGuilds[f], quota, switches, target))
                    state.assigned.emplace(item.first, item.second);    // a plan row keeps its plan guild

            // event=deal / event=roles only on change.
            uint32 dealtFree = 0;
            uint32 unplaced = 0;
            for (roster_guild_role::Member const& member : members[f])
            {
                if (member.guild)
                    continue;
                if (state.assigned.count(member.guid))
                    ++dealtFree;
                else
                    ++unplaced;
            }
            std::array<uint32, 5> const deal = { { uint32(factionGuilds[f].size()), dealtFree, unplaced, unknownRole, uint32(state.plan.size()) } };
            if (deal != state.tracedDeal[f])
            {
                state.tracedDeal[f] = deal;
                sLog.outBasic("[RosterGuild] event=deal faction=%s mode=%s guilds=%u dealt_without_guild=%u unplaced=%u unknown_role=%u plan_rows=%u",
                    f == 0 ? "alliance" : "horde", state.plan.empty() ? "quota" : "plan", deal[0], deal[1], deal[2], deal[3], deal[4]);
            }

            for (uint32 guildId : factionGuilds[f])
            {
                roster_guild_role::GuildCounts counts;
                uint32 unknown = 0;
                for (roster_guild_role::Member const& member : members[f])
                    if (member.guild == guildId)
                    {
                        roster_guild_role::AddToCounts(counts, member);
                        if (member.role == roster_guild_role::Role::Unknown)
                            ++unknown;
                    }
                uint32 healerClasses = 0;
                uint32 tankClasses = 0;
                for (auto const& item : counts.roleClass)
                {
                    if (item.first.first == roster_guild_role::Role::Healer && item.second)
                        ++healerClasses;
                    if (item.first.first == roster_guild_role::Role::Tank && item.second)
                        ++tankClasses;
                }
                std::array<uint32, 6> const line = { { counts.Get(roster_guild_role::Role::Tank), counts.Get(roster_guild_role::Role::Healer),
                    counts.Get(roster_guild_role::Role::Dps), unknown, healerClasses, tankClasses } };
                auto traced = state.tracedRoles.find(guildId);
                if (traced != state.tracedRoles.end() && traced->second == line)
                    continue;
                state.tracedRoles[guildId] = line;
                sLog.outBasic("[RosterGuild] event=roles faction=%s guild=%u tanks=%u healers=%u dps=%u unknown=%u healer_classes=%u tank_classes=%u quota=%u/%u/%u",
                    f == 0 ? "alliance" : "horde", guildId, line[0], line[1], line[2], line[3], line[4], line[5], quota.tanks, quota.healers, quota.dps);
            }
        }
    }

    // Caller holds state.lock.
    void RecountRosterGuilds(RosterGuildState& state, time_t now)
    {
        // Critic B5.8: the configured roster per faction, not the online count.
        std::unordered_map<uint32, uint8> rosterFaction;
        uint32 roster[2] = { 0, 0 };
        bool const roleFill = RoleFillConfigured();
        std::vector<roster_guild_role::Member> roleMembers[2];
        for (uint32 guid : sRandomPlayerbotMgr.PersistentRosterGuids())
        {
            PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(guid);
            if (!data)
                continue;

            uint8 const faction = RosterGuildFactionIndex(Player::TeamForRace(uint8(data->uiRace)));
            rosterFaction[guid] = faction;
            ++roster[faction];

            // twow-repo#485 / #518: class, race and level for the role fill, from the cache.
            if (roleFill)
            {
                roster_guild_role::Member member;
                member.guid = guid;
                member.cls = uint8(data->uiClass);
                member.race = uint8(data->uiRace);
                member.level = data->uiLevel;
                roleMembers[faction].push_back(member);
            }
        }

        std::vector<GuildSummary> guilds;
        sGuildMgr.CollectGuildSummaries(guilds);
        std::vector<PetitionSummary> petitions;
        sGuildMgr.CollectPetitionSummaries(petitions);

        uint32 botGuilds[2] = { 0, 0 };
        uint32 openCharters[2] = { 0, 0 };
        state.guildNames.clear();
        state.charterNames.clear();
        for (GuildSummary const& guild : guilds)
        {
            state.guildNames.insert(guild.name);
            auto it = rosterFaction.find(guild.leaderGuid.GetCounter());
            if (it != rosterFaction.end())
                ++botGuilds[it->second];
        }
        for (PetitionSummary const& petition : petitions)
        {
            state.charterNames.insert(petition.name);
            auto it = rosterFaction.find(petition.ownerGuid.GetCounter());
            if (it != rosterFaction.end())
                ++openCharters[it->second];
        }

        for (uint8 f = 0; f < 2; ++f)
        {
            RosterGuildFaction& fs = state.faction[f];
            fs.roster = roster[f];
            fs.target = roster_guild::TargetGuilds(roster[f], sPlayerbotAIConfig.rosterGuildBotsPerGuild);
            fs.ledger.guilds = botGuilds[f];
            fs.openCharters = openCharters[f];
            fs.freeNames = 0;
            for (std::string const& name : RosterGuildNames(f))
                if (!state.charterNames.count(name) && !state.guildNames.count(name) && !state.reservedNames.count(name) && CoreAcceptsCharterName(name))
                    ++fs.freeNames;

            // Only on change.
            uint32 const values[5] = { fs.roster, fs.target, fs.ledger.guilds, fs.openCharters, fs.freeNames };
            bool changed = !fs.hasTraced;
            for (uint32 i = 0; i < 5; ++i)
            {
                changed = changed || fs.traced[i] != values[i];
                fs.traced[i] = values[i];
            }
            fs.hasTraced = true;

            if (changed)
                sLog.outBasic("[RosterGuild] event=plan faction=%s roster=%u target=%u guilds=%u open_charters=%u free_names=%u",
                    f == 0 ? "alliance" : "horde", fs.roster, fs.target, fs.ledger.guilds, fs.openCharters, fs.freeNames);
        }

        // twow-repo#485 / #518: role fill only with a quota or a plan file (default: nothing changes).
        if (roleFill)
            DealRosterGuildRoles(state, roleMembers, guilds, petitions, rosterFaction);

        state.builtAt = now;
        state.dirty = false;
    }

    // Caller holds state.lock. The first caller after SnapshotSeconds (10-3600, or after a founding) recounts.
    RosterGuildFaction& FreshRosterGuildFaction(RosterGuildState& state, Team team)
    {
        time_t const now = time(nullptr);
        if (state.dirty || now - state.builtAt >= time_t(roster_guild::SnapshotInterval(sPlayerbotAIConfig.rosterGuildSnapshotSeconds)))
            RecountRosterGuilds(state, now);

        return state.faction[RosterGuildFactionIndex(team)];
    }

    // twow-repo#485 (new path): a complete own charter, the faction below its target and a name
    // available, no retry pending. No capital and no free travel target needed (47 of 47 [Idle]
    // samples of charter owners in capitals had a target active); no second trip while the bot
    // already travels to a guild master, and at most one trip per RosterGuildRetrySeconds.
    bool RosterTurnInUseful(PlayerbotAI* ai, Player* bot)
    {
        AiObjectContext* context = ai->GetAiObjectContext();

        if (bot->GetGuildId())
            return false;

        if (!ai->AllowActivity(TRAVEL_ACTIVITY) || !AI_VALUE(bool, "can move around"))
            return false;

        // == as Petition::IsComplete: the core founds only at exactly MinPetitionSigns (>= only after
        // the core hardening, fixlist rank 24).
        PetitionSummary petition;
        if (!RosterGuildPlan::OwnCharter(bot, petition) ||
            uint32(petition.signatureCount) != sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS))
            return false;

        if (!roster_guild::IsDue(time(nullptr), AI_VALUE2(time_t, "manual time", "roster guild turn in"), RosterGuildRetrySeconds))
            return false;

        if (!RosterGuildPlan::MayTurnIn(bot, petition.name))
            return false;

        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest npcs"))
            if (bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_PETITIONER))
                return true;

        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
        EntryTravelDestination* destination = dynamic_cast<EntryTravelDestination*>(target->GetDestination());
        if (destination && destination->HasNpcFlag(UNIT_NPC_FLAG_PETITIONER))
            return false;

        // At most one guild-master trip per RosterGuildRetrySeconds: the trip expires the current
        // target, and a failed or overridden search must not do that every 5 s.
        return roster_guild::IsDue(time(nullptr), AI_VALUE2(time_t, "manual time", "roster guild travel"), RosterGuildRetrySeconds);
    }
}

bool RosterGuildPlan::UsesRosterPath(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return roster_guild::UsesRosterPath(sPlayerbotAIConfig.rosterGuildBotsPerGuild,
        bot && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), ai->HasRealPlayerMaster());
}

bool RosterGuildPlan::MayFound(Player* bot)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction const& fs = FreshRosterGuildFaction(state, bot->GetTeam());
    return roster_guild::MayFound(fs.target, fs.ledger.guilds + fs.ledger.reserved);
}

bool RosterGuildPlan::MayOfferNearby(PlayerbotAI* ai)
{
    return !UsesRosterPath(ai) || MayFound(ai->GetBot());
}

bool RosterGuildPlan::MayBuyCharter(Player* bot, char const*& reason)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction const& fs = FreshRosterGuildFaction(state, bot->GetTeam());
    if (!roster_guild::MayBuyCharter(fs.target, fs.ledger.guilds + fs.ledger.reserved, fs.openCharters))
    {
        reason = "target_reached";
        return false;
    }

    // twow-repo#485 / #518 (PlanFile): one charter per plan guild, none once it has a guild.
    if (!PlanLabelOpen(state, bot->GetGUIDLow(), true))
    {
        reason = "plan_guild_taken";
        return false;
    }

    if (PickFreeRosterGuildName(state, RosterGuildNames(RosterGuildFactionIndex(bot->GetTeam()))).empty())
    {
        reason = "no_name";
        return false;
    }

    return true;
}

std::string RosterGuildPlan::ReserveCharter(Player* bot)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction& fs = FreshRosterGuildFaction(state, bot->GetTeam());
    if (!roster_guild::MayBuyCharter(fs.target, fs.ledger.guilds + fs.ledger.reserved, fs.openCharters))
        return std::string();

    if (!PlanLabelOpen(state, bot->GetGUIDLow(), true))
        return std::string();

    std::string const name = PickFreeRosterGuildName(state, RosterGuildNames(RosterGuildFactionIndex(bot->GetTeam())));
    if (name.empty())
        return name;

    // Counted at once; the next recount confirms the charter or drops a failed purchase.
    state.charterNames.insert(name);
    ++fs.openCharters;
    std::string const label = PlanLabel(state, bot->GetGUIDLow());
    if (!label.empty())
        state.charterLabels.insert(label);
    return name;
}

bool RosterGuildPlan::MayTurnIn(Player* bot, std::string const& charterName)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction const& fs = FreshRosterGuildFaction(state, bot->GetTeam());
    if (!roster_guild::MayFound(fs.target, fs.ledger.guilds + fs.ledger.reserved))
        return false;

    if (!PlanLabelOpen(state, bot->GetGUIDLow(), false))
        return false;

    std::vector<std::string> const names = RosterGuildNames(RosterGuildFactionIndex(bot->GetTeam()));
    return KeepsCharterName(state, names, charterName) || !PickFreeRosterGuildName(state, names).empty();
}

bool RosterGuildPlan::ReserveFounding(Player* bot, std::string const& charterName, std::string& name, uint32& target, char const*& reason)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction& fs = FreshRosterGuildFaction(state, bot->GetTeam());
    // twow-repo#485 / #518 (PlanFile): no second guild of one plan guild.
    if (!PlanLabelOpen(state, bot->GetGUIDLow(), false))
    {
        reason = "plan_guild_taken";
        return false;
    }

    // Critic B5.3: slot and name are taken under the lock, before HandleTurnInPetitionOpcode.
    if (!roster_guild::TryReserveFounding(fs.target, fs.ledger))
    {
        reason = "target_reached";
        return false;
    }

    std::vector<std::string> const names = RosterGuildNames(RosterGuildFactionIndex(bot->GetTeam()));
    name = KeepsCharterName(state, names, charterName) ? charterName : PickFreeRosterGuildName(state, names);
    if (name.empty())
    {
        roster_guild::FinishFounding(fs.ledger, false);
        reason = "no_name";
        return false;
    }

    state.reservedNames.insert(name);
    target = fs.target;
    return true;
}

void RosterGuildPlan::FinishFounding(Player* bot, std::string const& name, bool founded)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    RosterGuildFaction& fs = state.faction[RosterGuildFactionIndex(bot->GetTeam())];
    roster_guild::FinishFounding(fs.ledger, founded);
    state.reservedNames.erase(name);
    if (!founded)
        return;

    state.guildNames.insert(name);
    if (fs.openCharters)
        --fs.openCharters;
    std::string const label = PlanLabel(state, bot->GetGUIDLow());
    if (!label.empty())
        state.foundedLabels.insert(label);

    // The new guild is in GuildMgr now: recount on the next call.
    state.dirty = true;
}

bool RosterGuildPlan::IsDue(PlayerbotAI* ai, char const* key, uint32 intervalSeconds)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    time_t const now = time(nullptr);
    if (!roster_guild::IsDue(now, AI_VALUE2(time_t, "manual time", key), intervalSeconds))
        return false;

    SET_AI_VALUE2(time_t, "manual time", key, now);
    return true;
}

Item* RosterGuildPlan::OwnCharter(Player* bot, PetitionSummary& out, uint32 accountId)
{
    // Every charter item counts, not only the first one: count, offer and turn-in use the same one.
    Item* found = nullptr;
    bot->ApplyForAllItems([&](Item* item)
    {
        if (!found && item->GetEntry() == 5863 && sGuildMgr.GetPetitionSummaryByCharterGuid(item->GetObjectGuid(), out, accountId) &&
            out.ownerGuid == bot->GetObjectGuid())
            found = item;
    });

    if (!found)
        out = PetitionSummary();

    return found;
}

// twow-repo#485 / #518: role fill of roster guilds.
bool RosterGuildPlan::UsesRoleFill(PlayerbotAI* ai)
{
    return roster_guild_role::UsesRoleFill(UsesRosterPath(ai), RosterGuildQuota(), RosterGuildPlanLoaded());
}

uint8 RosterGuildPlan::ReportOwnRole(Player* bot)
{
    // The bot's own talents, on its own thread (never another bot's Player*). Without spent
    // talents GetPlayerSpecTab returns a class default: no role yet.
    std::map<uint32, int32> tabs = AiFactory::GetPlayerSpecTabs(bot);
    bool const hasTalents = tabs[0] + tabs[1] + tabs[2] > 0;
    roster_guild_role::Role const role = roster_guild_role::RoleFromBits(hasTalents ? uint32(AiFactory::GetPlayerRoles(bot)) : 0, hasTalents);

    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    // Read at the next recount (SnapshotSeconds); no recount per report.
    state.selfRoles[bot->GetGUIDLow()] = uint8(role);
    return uint8(RoleOf(state, bot->GetGUIDLow()));
}

uint32 RosterGuildPlan::AssignedGuild(uint32 guidLow, Team team)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    FreshRosterGuildFaction(state, team);
    auto it = state.assigned.find(guidLow);
    return it == state.assigned.end() ? 0 : it->second;
}

bool RosterGuildPlan::MaySignForRole(Player* bot, PetitionSummary const& offered, char const*& reason)
{
    if (!RoleFillConfigured())
        return true;

    ReportOwnRole(bot);

    // Signers as a copy under the petition lock, taken before the plan's lock.
    std::vector<ObjectGuid> signers;
    sGuildMgr.GetPetitionSignerGuids(offered.id, signers);

    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    FreshRosterGuildFaction(state, bot->GetTeam());

    // PlanFile: only a charter of the bot's own plan guild.
    std::string const ownLabel = PlanLabel(state, bot->GetGUIDLow());
    if (!roster_guild_role::SamePlanGuild(ownLabel, PlanLabel(state, offered.ownerGuid.GetCounter())))
    {
        reason = "plan_guild";
        return false;
    }
    if (!ownLabel.empty())
        return true;

    roster_guild_role::Quota const quota = RosterGuildQuota();
    if (!roster_guild_role::QuotaActive(quota))
        return true;

    // The charter's future members: owner and signers, by role, class and race.
    roster_guild_role::GuildCounts charter;
    roster_guild_role::AddToCounts(charter, MemberOf(state, offered.ownerGuid.GetCounter()));
    for (ObjectGuid const& signer : signers)
        if (signer != bot->GetObjectGuid() && signer != offered.ownerGuid)
            roster_guild_role::AddToCounts(charter, MemberOf(state, signer.GetCounter()));

    roster_guild_role::Member candidate = MemberOf(state, bot->GetGUIDLow());
    candidate.guild = 0;
    roster_guild_role::Fit const fit = roster_guild_role::CheckFit(charter, candidate, quota, RosterGuildSwitches(),
        state.stats[RosterGuildFactionIndex(bot->GetTeam())]);
    if (fit == roster_guild_role::Fit::Ok)
        return true;

    reason = roster_guild_role::FitName(fit);
    return false;
}

void RosterGuildPlan::NoteJoined(Player* bot, uint32 guildId)
{
    RosterGuildState& state = GetRosterGuildState();
    std::lock_guard<std::mutex> guard(state.lock);
    // The member list changed: recount (and deal again) on the next call.
    state.dirty = true;
    sLog.outBasic("[RosterGuild] event=joined bot=%u guild=%u role=%s", bot->GetGUIDLow(), guildId,
        roster_guild_role::RoleName(RoleOf(state, bot->GetGUIDLow())));
}
// --- end of the roster guild plan ---------------------------------------------------------------

bool BuyPetitionAction::Execute(Event& event)
{
    std::list<ObjectGuid> vendors = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest npcs")->Get();
    bool vendored = false, result = false;
    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_PETITIONER);
        if (!pCreature)
            continue;

        // twow-repo#485 (new path): an approved, unused name of the faction, the charter counted at
        // once - no DB query (the stock generator costs two synchronous queries per call).
        bool const rosterPath = RosterGuildPlan::UsesRosterPath(ai);
        std::string guildName = rosterPath ? RosterGuildPlan::ReserveCharter(bot) : RandomPlayerbotFactory::CreateRandomGuildName();
        if (guildName.empty() && rosterPath)
            return false;

        if (guildName.empty())
            continue;

        WorldPacket data(CMSG_PETITION_BUY);

        data << pCreature->GetObjectGuid();
        data << uint32(0);
        data << uint64(0);
        data << guildName.c_str();
#ifdef MANGOSBOT_TWO
        data << std::string("");
#else
        data << uint32(0);
#endif
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint16(0);
        data << uint8(0);

#ifdef MANGOSBOT_TWO
        for (int i = 0; i < 10; ++i)
            data << std::string("");
#endif

        data << uint32(0); // index
        data << uint32(0);

        bot->GetSession()->HandlePetitionBuyOpcode(data);

        return true;
    }

    return false;
}

bool BuyPetitionAction::isUseful()
{
    return canBuyPetition(bot);
};

bool BuyPetitionAction::canBuyPetition(Player* bot)
{
    if (!sPlayerbotAIConfig.randomBotFormGuild)
        return false;

    if (bot->GetGuildId())
        return false;

    if (bot->GetGuildIdInvited())
        return false;    

    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    // twow-repo#485: the former "item count" check passed an unparsable qualifier (always 0), and the
    // core silently refuses a second petition of the same owner (PetitionsHandler.cpp:91-93):
    // 256 futile purchase attempts per 2.68 h on v24, each with two guild-name queries. Presence
    // checks only - no Petition* is dereferenced.
    if (bot->HasItemCount(5863, 1))
        return false;

    if (sGuildMgr.GetPetitionByOwnerGuid(bot->GetObjectGuid()))
        return false;

    if (ai->GetGuilderType() == GuilderType::SOLO)
        return false;

    if (ai->GetGrouperType() == GrouperType::SOLO)
        return false;

    if (!ai->HasStrategy("guild", BotState::BOT_STATE_NON_COMBAT))
        return false;

    uint32 cost = 1000; //GUILD_CHARTER_COST;

    if (AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) < cost)
        return false;

    // twow-repo#485 (new path): no charter once bot guilds + open bot charters cover the faction
    // target, and none without a free approved name.
    if (RosterGuildPlan::UsesRosterPath(ai))
    {
        char const* reason = "";
        if (!RosterGuildPlan::MayBuyCharter(bot, reason))
        {
            if (RosterGuildPlan::IsDue(ai, "roster guild buy trace", HOUR))
                sLog.outBasic("[RosterGuild] event=buy_blocked bot=%u reason=%s", bot->GetGUIDLow(), reason);
            return false;
        }
    }

    return true;
}

bool PetitionOfferAction::Execute(Event& event)
{
    uint32 petitionEntry = 5863; //GUILD_CHARTER
    std::list<Item*> petitions = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(5863));

    if (petitions.empty())
        return false;

    ObjectGuid guid = event.getObject();

    Player* master = GetMaster();
    if (!master)
    {
        if (!guid)
            guid = bot->GetSelectionGuid();
    }
    else {
        if (!guid)
            guid = master->GetSelectionGuid();
    }

    if (!guid)
        return false;

    Player* player = sObjectMgr.GetPlayer(guid);

    if (!player)
        return false;

    // twow-repo#485 (new path): the charter whose petition the bot owns, with a copy of that petition
    // (the stock path keeps the first charter item).
    bool const rosterPath = RosterGuildPlan::UsesRosterPath(ai);
    PetitionSummary petition;
    Item* charter = rosterPath ? RosterGuildPlan::OwnCharter(bot, petition, player->GetSession()->GetAccountId()) : petitions.front();
    if (!charter)
        return false;

    WorldPacket data(CMSG_OFFER_PETITION);

#ifndef MANGOSBOT_ZERO
    data << uint32(0);
#endif
    data << charter->GetObjectGuid();
    data << guid;

    // twow-repo#485 (new path): the petition from the core's memory (a copy under its lock) instead
    // of the two queries below, which use the charter item guid and never match (petition_sign
    // holds the petition id): 12,611 per 2.67 h on v24. Offer only within the faction, to a charter
    // that is not full and to an account that has not signed it.
    if (rosterPath)
    {
        if (player->GetTeam() != bot->GetTeam() ||
            uint32(petition.signatureCount) >= sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS) || petition.signedByAccount)
            return false;

        bot->GetSession()->HandleOfferPetitionOpcode(data);
        context->GetValue<uint8>("petition signs")->Set(petition.signatureCount);
        return true;
    }

    auto result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = '%u' AND petitionguid = '%u'", player->GetSession()->GetAccountId(), petitions.front()->GetObjectGuid().GetCounter());

    if (result)
    {
        return false;
    }

    bot->GetSession()->HandleOfferPetitionOpcode(data);

    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitions.front()->GetObjectGuid().GetCounter());
    uint8 signs = result ? (uint8)result->GetRowCount() : 0;

    context->GetValue<uint8>("petition signs")->Set(signs);

    return true;
};

bool PetitionOfferNearbyAction::Execute(Event& event)
{
    uint32 found = 0;

    std::list<ObjectGuid> nearGuids = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get();
    for (auto& i : nearGuids)
    {
        Player* player = sObjectMgr.GetPlayer(i);

        if (!player)
            continue;

        if (player->GetGuildId())
            continue;

        if (player->GetGuildIdInvited())
            continue;

        if (!sPlayerbotAIConfig.randomBotInvitePlayer && IsRealPlayer(player))
            continue;

        PlayerbotAI* botAi = GetBotAI(player);

        if (botAi)
        {
            if (botAi->HasActivePlayerMaster()) //Do not invite alts of active players. 
                continue;
        }

        if (sServerFacade.GetDistance2d(bot, player) > sPlayerbotAIConfig.sightDistance)
            continue;

        // twow-repo#485/#478 (owner 02.10., point 7): no /say towards bots - only a real player hears it.
        if (sPlayerbotAIConfig.inviteChat && IsRealPlayer(player) && sServerFacade.GetDistance2d(bot, player) < sPlayerbotAIConfig.spellDistance && (sRandomPlayerbotMgr.IsFreeBot(bot) || !ai->HasActivePlayerMaster()))
        {
            std::map<std::string, std::string> placeholders;
            placeholders["%name"] = player->GetName();

            if(urand(0,3))
                bot->Say(BOT_TEXT2("Hey %name do you want create a guild together?", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
            else
                bot->Say(BOT_TEXT2("Hey do you want to form a guild?", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        }

        //Parse rpg target to quest action.
        WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
        p << i;
        p.rpos(0);

        Event petitionOfferEvent = Event("petition offer nearby", p);
        if (PetitionOfferAction::Execute(petitionOfferEvent))
            found++;
    }

    return found > 0;
};

bool PetitionTurnInAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::list<ObjectGuid> vendors = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest npcs")->Get();
    bool vendored = false, result = false;

    std::list<Item*> petitions = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(5863));

    if (petitions.empty())
        return false;

    for (std::list<ObjectGuid>::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_PETITIONER);
        if (!pCreature)
            continue;

        WorldPacket data(CMSG_TURN_IN_PETITION, 8);

        Item* petition = petitions.front();

        if (!petition)
            return false;

        // twow-repo#485 (new path): a founding slot of the faction and the guild name are taken under
        // the plan's lock first (critic B5.3: never more guilds than the target). A charter without an
        // approved, unused name is renamed by the core under its petition lock (DB write: UPDATE
        // petition SET name). The turn-in itself runs on this map thread, as for a player
        // (CMSG_TURN_IN_PETITION is PACKET_PROCESS_MAP).
        bool const rosterPath = RosterGuildPlan::UsesRosterPath(ai);
        std::string guildName;
        uint32 target = 0;
        if (rosterPath)
        {
            PetitionSummary summary;
            char const* reason = "no_petition";
            if (Item* charter = RosterGuildPlan::OwnCharter(bot, summary))
            {
                // The charter whose petition the bot owns, not merely the first charter item.
                petition = charter;
                if (RosterGuildPlan::ReserveFounding(bot, summary.name, guildName, target, reason) && guildName != summary.name &&
                    !sGuildMgr.RenamePetition(petition->GetObjectGuid(), bot->GetObjectGuid(), guildName))
                {
                    RosterGuildPlan::FinishFounding(bot, guildName, false);
                    guildName.clear();
                    reason = "rename_failed";
                }
            }

            if (guildName.empty())
            {
                SET_AI_VALUE2(time_t, "manual time", "roster guild turn in", time(nullptr));
                sLog.outBasic("[RosterGuild] event=found_blocked bot=%u charter=%u reason=%s",
                    bot->GetGUIDLow(), petition->GetObjectGuid().GetCounter(), reason);
                return false;
            }
        }

        uint32 const charterLow = petition->GetObjectGuid().GetCounter();    // a founding destroys the item
        data << petition->GetObjectGuid();

        bot->GetSession()->HandleTurnInPetitionOpcode(data);

        if (rosterPath)
        {
            RosterGuildPlan::FinishFounding(bot, guildName, bot->GetGuildId() != 0);
            if (!bot->GetGuildId())
            {
                SET_AI_VALUE2(time_t, "manual time", "roster guild turn in", time(nullptr));
                sLog.outBasic("[RosterGuild] event=found_failed bot=%u charter=%u name=%s",
                    bot->GetGUIDLow(), charterLow, guildName.c_str());
            }
        }

        if (bot->GetGuildId())
        {
            Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
            uint32 st, cl, br, bc, bg;
            bg = urand(0, 51);
            bc = urand(0, 17);
            cl = urand(0, 17);
            br = urand(0, 7);
            st = urand(0, 180);
            guild->SetEmblem(st, cl, br, bc, bg);           

            //LANG_GUILD_VETERAN -> can invite, private and initiate -> personal note.
            guild->SetRankRights(2, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK | GR_RIGHT_INVITE | GR_RIGHT_EPNOTE);
            guild->SetRankRights(3, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK | GR_RIGHT_EPNOTE);
            guild->SetRankRights(4, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK | GR_RIGHT_EPNOTE);

            if (rosterPath)
                sLog.outBasic("[RosterGuild] event=founded bot=%u guild=%u name=%s faction=%s members=%u target=%u",
                    bot->GetGUIDLow(), guild->GetId(), guildName.c_str(), RosterGuildFactionName(bot->GetTeam()), guild->GetMemberSize(), target);
        }

        return true;
    }

    //Select a new target to travel to. 
    TravelTarget newTarget = TravelTarget(ai);

    ai->TellDebug(requester, "Handing in guild petition", "debug travel");

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    if (oldTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (oldTarget->GetDestination())
    {
        TravelDestination* dest = oldTarget->GetDestination();

        EntryTravelDestination* eDest = dynamic_cast<EntryTravelDestination*>(dest);

        if (eDest && eDest->HasNpcFlag(UNIT_NPC_FLAG_PETITIONER))
            return false;
    }

    // twow-repo#485 (new path): one trip request per RosterGuildRetrySeconds (see RosterTurnInUseful).
    if (RosterGuildPlan::UsesRosterPath(ai))
    {
        SET_AI_VALUE2(time_t, "manual time", "roster guild travel", time(nullptr));
        sLog.outBasic("[RosterGuild] event=travel bot=%u", bot->GetGUIDLow());
    }

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);

    return ai->DoSpecificAction("request named travel target::petition", Event("can hand in petition"), true);
};

bool PetitionTurnInAction::isUseful()
{
    if (!sPlayerbotAIConfig.randomBotFormGuild)
        return false;

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT))
        return false;

    // twow-repo#485 (new path): see RosterTurnInUseful - no capital, no free travel target.
    if (RosterGuildPlan::UsesRosterPath(ai))
        return RosterTurnInUseful(ai, bot);

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    bool inCity = false;
    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
    if (areaEntry)
    {
        if (areaEntry->zone)
            areaEntry = GetAreaEntryByAreaID(areaEntry->zone);

        if (areaEntry && areaEntry->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    return inCity && !bot->GetGuildId() && AI_VALUE2(uint32, "item count", chat->formatQItem(5863)) && AI_VALUE(uint8, "petition signs") >= sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS) && !AI_VALUE(bool, "travel target traveling");
};

bool BuyTabardAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool canBuy = ai->DoSpecificAction("buy", Event("buy tabard", "|cHitem:5976:|r"),true);

    if (canBuy && AI_VALUE2(uint32, "item count", chat->formatQItem(5976)))
        return true;

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    if (oldTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (oldTarget->GetDestination())
    {
        TravelDestination* dest = oldTarget->GetDestination();

        EntryTravelDestination* eDest = dynamic_cast<EntryTravelDestination*>(dest);

        if (eDest && eDest->HasNpcFlag(UNIT_NPC_FLAG_TABARDDESIGNER))
            return false;
    }

    return ai->DoSpecificAction("request named travel target::tabard", Event("can buy tabard"), true);  
};

bool BuyTabardAction::isUseful()
{
    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT))
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            return false;

    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    bool inCity = false;
    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
    if (areaEntry)
    {
        if (areaEntry->zone)
            areaEntry = GetAreaEntryByAreaID(areaEntry->zone);

        if (areaEntry && areaEntry->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    return inCity && bot->GetGuildId() && !AI_VALUE2(uint32, "item count", chat->formatQItem(5976)) && AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) >= 10000 && !AI_VALUE(bool, "travel target traveling");
};