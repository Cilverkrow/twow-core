#pragma once

#include <cstdint>

namespace ai::gather_purpose
{
// Hotfix 8.5 (twow-repo#329, owner decision 2026-10-01): profession progress is fine when
// the bot declares it. Latchigedap (L15) held a skinning travel target for eight hours at
// skill 1/75 - no skill-up at all (twow-repo#471) - and never handed in eight finished
// quests. A roster bot on its own now starts a declared purpose ([Purpose] state=start)
// with a skill target and a budget; skill-ups count as progress only while it runs. It
// ends when the target is reached, the budget is over, or nothing was gained for a while;
// the last two block that profession for an hour.
constexpr uint32_t BudgetSeconds = 30 * 60;
constexpr uint32_t NoSkillupSeconds = 15 * 60;
constexpr uint32_t BlockSeconds = 60 * 60;
// Hotfix 8.8 (#472): a fishing purpose without a single cast after this long found no water.
constexpr uint32_t NoSpotSeconds = 5 * 60;

enum class End : uint8_t
{
    None,
    TargetReached,
    Budget,
    NoSkillup,
    NoSpot
};

inline char const* ProfessionName(uint32_t skill)
{
    switch (skill)
    {
        case 182: return "herbalism";
        case 186: return "mining";
        case 356: return "fishing";
        case 393: return "skinning";
        default:  return "other";
    }
}

inline char const* EndName(End end)
{
    switch (end)
    {
        case End::TargetReached: return "target_reached";
        case End::Budget:        return "budget";
        case End::NoSkillup:     return "no_skillup";
        case End::NoSpot:        return "no_spot";
        default:                 return "none";
    }
}

struct State
{
    uint32_t skill = 0;          // 0 = no declared purpose
    uint32_t startValue = 0;
    uint32_t lastValue = 0;
    uint32_t target = 0;
    uint32_t start = 0;
    uint32_t lastSkillUp = 0;

    static constexpr uint32_t MaxBlocks = 4;   // fishing, skinning, mining, herbalism
    uint32_t blockedSkill[MaxBlocks] = {};
    uint32_t blockedUntil[MaxBlocks] = {};

    bool Active() const { return skill != 0; }

    bool Blocked(uint32_t gatherSkill, uint32_t now) const
    {
        for (uint32_t i = 0; i < MaxBlocks; ++i)
            if (blockedSkill[i] == gatherSkill && now < blockedUntil[i])
                return true;
        return false;
    }

    void Start(uint32_t gatherSkill, uint32_t value, uint32_t skillTarget, uint32_t now)
    {
        skill = gatherSkill;
        startValue = lastValue = value;
        target = skillTarget;
        start = lastSkillUp = now;
    }

    // Called on every progress check while a purpose runs.
    End Observe(uint32_t value, uint32_t now)
    {
        if (!Active())
            return End::None;

        if (value > lastValue)
        {
            lastValue = value;
            lastSkillUp = now;
        }

        End end = End::None;
        if (value >= target)
            end = End::TargetReached;
        else if (now - start >= BudgetSeconds)
            end = End::Budget;
        else if (now - lastSkillUp >= NoSkillupSeconds)
            end = End::NoSkillup;

        if (end != End::None && end != End::TargetReached)
            Block(skill, now + BlockSeconds);
        if (end != End::None)
            skill = 0;
        return end;
    }

    // Hotfix 8.8: ends the purpose for want of a spot; the profession is blocked like no_skillup.
    End EndNoSpot(uint32_t now)
    {
        if (!Active())
            return End::None;
        Block(skill, now + BlockSeconds);
        skill = 0;
        return End::NoSpot;
    }

private:
    void Block(uint32_t gatherSkill, uint32_t until)
    {
        uint32_t slot = 0;
        for (uint32_t i = 0; i < MaxBlocks; ++i)
        {
            if (blockedSkill[i] == gatherSkill)
            {
                slot = i;
                break;
            }
            if (blockedUntil[i] < blockedUntil[slot])
                slot = i;
        }
        blockedSkill[slot] = gatherSkill;
        blockedUntil[slot] = until;
    }
};

// Whether the gathering purpose for this skill is wanted. Bots with a real player keep the
// old rule (skill behind); a roster bot on its own needs a declared purpose for it.
enum class Decision : uint8_t
{
    No,
    Yes,
    Start
};

inline Decision Decide(State const& state, bool rosterOnItsOwn, uint32_t gatherSkill, uint32_t value,
    uint32_t target, uint32_t now)
{
    if (value >= target)
        return Decision::No;
    if (!rosterOnItsOwn)
        return Decision::Yes;
    if (state.Active())
        return state.skill == gatherSkill ? Decision::Yes : Decision::No;
    if (state.Blocked(gatherSkill, now))
        return Decision::No;
    return Decision::Start;
}
}
