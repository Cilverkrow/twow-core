#pragma once

#include "playerbot/AdhocGroupPolicy.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class Group;

namespace ai::adhoc_group
{
// twow-repo#365 step 2: the game-state facts of an ad-hoc quest group, read from the
// live objects. Shared by the bot action (AdHoc.WorldThread = 0) and the world-thread
// manager below (twow-repo#628).

// The quest objective the player's active travel target works on (invalid if none).
ObjectiveKey CurrentObjective(Player* player);
// Roster bot, no real player in charge, not in an instance, battleground or raid.
bool IsCandidateBot(Player* player);
std::uint32_t ObjectiveCounter(Player* player, ObjectiveKey const& key);
bool ObjectiveDone(Player* player, ObjectiveKey const& key);
void GroupLevels(Group* group, std::uint32_t& minLevel, std::uint32_t& maxLevel);
bool AllRosterBots(Group* group);

// twow-repo#628 (AiPlayerbot.BotGroups.AdHoc.WorldThread): the core handles the group
// opcodes on the world thread (PACKET_PROCESS_GROUP = PACKET_PROCESS_WORLD), and
// Group::Create / Disband write ObjectMgr::m_GroupMap without a lock. The bot action
// called the handlers on its map thread, and the continents update in parallel. This
// manager forms and leaves the ad-hoc groups instead, from the roster branch of
// RandomPlayerbotMgr::UpdateAIInternal (end of World::Update, all maps idle, the
// #517 CheckBgFill pattern). Same rules as the action: objective key, radius, level
// window, pair cooldown, one invite per bot and pass, leave rules of design 3.2. A
// leave really leaves the group: the action's "leave" with the bot as issuer is a
// no-op for a free bot in a group of two or more (ShouldStayInGroup, #301).
class WorldThreadManager
{
public:
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    // twow-repo#365 HelpUntilDone (OB-10 review #660 B1): helpers are released even after Enabled or WorldThread went to 0 by
    // a config reload (no pass runs then).
    bool HasHelpers() const { return !helpers.empty(); }
    void ReleaseHelpers(std::map<std::uint32_t, Player*> const& bots);

private:
    struct Track
    {
        std::uint32_t group = 0;
        std::uint32_t lastProgress = 0;
        std::uint32_t lastCounter = 0;
        std::uint32_t outOfRangeSince = 0;
        std::uint32_t levelWindowSince = 0;
    };

    struct Minute
    {
        std::uint32_t start = 0;
        std::uint32_t passes = 0;
        std::uint32_t candidates = 0;
        std::uint32_t invites = 0;
        std::uint32_t joins = 0;
        std::uint32_t leaves = 0;
        std::uint32_t passUsMax = 0;
        // twow-repo#628 fix: the slowest single join and leave (the group change on the world thread).
        std::uint32_t joinUsMax = 0;
        std::uint32_t leaveUsMax = 0;
        std::uint32_t scanUsMax = 0;   // leave rules and candidates of all bots, without the leaves
        std::uint32_t bots = 0;
        std::uint32_t slices = 0;      // updates of the last finished pass
    };

    // A candidate by id, not by pointer: with ScanSlices a pass spans several updates, and the pairs resolve the bot and its
    // group again when all slices are done.
    struct Candidate
    {
        std::uint32_t guid = 0;
        std::uint32_t groupId = 0;  // its ad-hoc group (led by it) or 0
        ObjectiveKey key;
    };

    // twow-repo#628 (AiPlayerbot.BotGroups.AdHoc.ScanSlices, default 0): one pass - stage 1 (leave rules) and stage 2
    // (candidates) for a slice of the bots per update, stage 3 (pairs) once the last slice is done.
    struct Pass
    {
        bool active = false;
        std::vector<std::uint32_t> guids;  // the bots at the pass start, in map order
        std::size_t next = 0;
        std::uint32_t updates = 0;  // manager updates the pass took so far
        std::unordered_map<std::uint64_t, std::vector<Candidate>> buckets;
    };

    bool CheckLeave(Player* bot, Group* group, std::uint32_t now);
    void LeaveGroup(Player* bot, Group* group);
    bool Join(Player* inviter, Player* invitee, ObjectiveKey const& key, std::uint32_t now);
    void FlushMinute(std::uint32_t now);
    void ScanSlice(std::map<std::uint32_t, Player*> const& bots, std::size_t end, std::uint32_t now);
    void Pairs(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    void SweepHelpers(std::map<std::uint32_t, Player*> const& bots);

    std::unordered_map<std::uint32_t, Track> tracks;  // world thread only
    // twow-repo#365 HelpUntilDone: the manager's own helpers (bot guid -> the ad-hoc group it helps), never found by role ==
    // 1 (S7 followers carry the same role), and the ones seen helping in the running pass. World thread only.
    std::unordered_map<std::uint32_t, std::uint32_t> helpers;
    std::unordered_set<std::uint32_t> helpingPass;
    std::uint32_t lastPass = 0;
    std::uint32_t passLeaveUs = 0;   // time in LeaveGroup during the running update
    Pass pass;
    Minute minute;
};

WorldThreadManager& WorldManager();
}
