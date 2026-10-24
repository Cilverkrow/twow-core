#pragma once

#include "playerbot/QuestGroupPolicy.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class Group;
class PlayerbotAI;

namespace ai
{
class TravelDestination;
class WorldPosition;

namespace quest_groups
{
// twow-repo#627 S7 (AiPlayerbot.QuestGroups.Enable): the world-thread manager of the help groups, run from the roster branch
// of RandomPlayerbotMgr::UpdateAIInternal next to CheckBgFill (end of World::Update, all maps idle, the #517 pattern).
// Group APIs (invite, accept, leave) and forced travel targets of the members happen only there; strategy changes and AI
// resets are posted to the bot and run on its own thread (twow-repo#628 fix, GroupAiRefresh.h).

// --- queries for the bots' own threads (CLI-Deep hooks: PosStuck, S3c, turn-in recovery, QFO, S5a) ------------------------
// Role of the bot in a help group: 0 = none, 1 = follower, 2 = leader. An atomic in PlayerbotAI, no lock.
bool IsMember(PlayerbotAI* ai);
bool IsFollower(PlayerbotAI* ai);
// PosStuck (CLI-Deep) calls this when it detects a leader without a forced target: the leader made no progress for its window,
// so the manager dissolves the group with reason=leader_stuck on its next pass; PosStuck itself then resets its window and
// leaves the leader alone (no park, unstuck or teleport). Lock-free, any thread.
void NoteLeaderStuck(PlayerbotAI* ai);
// Seconds the bot has waited for help on the quest / on its oldest waiting quest (0 = not waiting). Short mutex, rare calls.
std::uint32_t QueuedAge(std::uint32_t guid, std::uint32_t questId);
std::uint32_t OldestQueuedAge(std::uint32_t guid);
// twow-repo#628 fix, bot thread only (PlayerbotAI::ApplyGroupAiRequests): the follower or turn-in strategy set the manager posted.
void ApplyRoleSet(PlayerbotAI* ai, bool turnIn);

// A member's own travel target before the group, restored after it (no second TravelTarget: its constructor and SetTarget
// have side effects). Not kept: temporary, group-copy, gap-window and band-trip targets.
struct SavedTravel
{
    TravelDestination* dest = nullptr;
    WorldPosition* pos = nullptr;
    std::vector<std::string> conditions;
    bool forced = false;
    std::uint32_t relevance = 0;
    bool valid = false;
};

class WorldThreadManager
{
public:
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    // Running groups are tended (and dissolved) even after Enable went to 0 by a config reload.
    bool HasGroups() const { return !groups.empty(); }

private:

    struct Member
    {
        std::uint32_t guid = 0;
        std::uint32_t lastCounter = 0;
        SavedTravel travel;
    };

    struct HelpGroup
    {
        std::uint32_t groupId = 0;
        std::uint32_t questId = 0;
        std::uint32_t leader = 0;
        std::uint32_t mapId = 0;
        std::vector<Member> members;
        Phase phase = Phase::Rendezvous;
        std::uint32_t formedAt = 0;
        std::uint32_t phaseSince = 0;
        std::uint32_t lastProgress = 0;
        std::uint32_t forces = 0;
        char cohort = 'A';
        bool mixed = false;
        ApproachWatch leaderWatch;   // the leader's approach to its forced target (PosStuck does not see forced targets)
    };

    struct Minute
    {
        std::uint32_t start = 0;
        std::uint32_t passes = 0;
        std::uint32_t candidates = 0;
        std::uint32_t quests = 0;
        std::uint32_t formed = 0;
        std::uint32_t dissolved = 0;
        std::uint32_t scanUsMax = 0;
        // twow-repo#628 fix: the slowest single formation and dissolution (the group change on the world thread).
        std::uint32_t formUsMax = 0;
        std::uint32_t dissolveUsMax = 0;
        std::uint32_t scanOnlyUsMax = 0;   // the quest-log index pass of all bots, without the formations
        std::uint32_t bots = 0;
    };

    void Tend(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, std::uint32_t now, Dissolve& out);
    void Scan(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    bool FormOne(std::map<std::uint32_t, Player*> const& bots, std::vector<Candidate> const& members, std::uint32_t questId,
        std::uint32_t now);
    void DissolveGroup(std::map<std::uint32_t, Player*> const& bots, HelpGroup& g, Dissolve reason, std::uint32_t now);
    void Release(Player* bot, Member const& m, bool leaveGroup);
    void FlushMinute(std::uint32_t now);

    std::unordered_map<std::uint32_t, HelpGroup> groups;        // by core group id; world thread only
    std::unordered_map<std::uint32_t, std::uint32_t> memberOf;  // bot guid -> core group id
    std::uint32_t lastScan = 0;
    std::uint32_t passFormUs = 0;   // time in FormOne during the running scan
    Minute minute;
};

WorldThreadManager& WorldManager();
QueueStore& Queue();
}
}
