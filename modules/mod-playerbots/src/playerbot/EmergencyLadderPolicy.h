#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ai::emergency_ladder
{
// twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder, cohort mode 0 off / 1 all / 2 cohort B, default 0).
// Solo free bots below critical health: today panic -> "flee" (ACTION_EMERGENCY + 9) wins every tick, and for a solo random bot the flee is a
// MoveChase around the mob that gains no distance (mobs run 8.0 yd/s, players 7.0) - it pushes aside the class escapes and the killing blow.
// The ladder decides once per episode (HP below critical health): use a class escape that is ready (its existing critical-health node fires
// while panic stays quiet for EscapeWindowMs), fight on (a single, nearly dead, normal target), or flee as before.
// v1 defaults (OB-00 18.10.2026, overridable by the owner before live): healers stay outside (F1); fight only when the target is at or below
// AiPlayerbot.Rotation.EmergencyLadder.FightTargetHp (default 20, F2); solo CC only with the spells of
// AiPlayerbot.Rotation.EmergencyLadder.SoloCcSpells (default polymorph,banish,fear, F3); prot warriors use Last Stand / Shield Wall (F4).
// Pure rules, no game types, no locks, no config reads.
enum class Choice : std::uint8_t { Flee, Escape, Fight };

constexpr std::uint32_t EscapeWindowMs = 2000;     // panic stays quiet this long per episode while an escape is ready
constexpr std::uint32_t MaxLevelGapForFight = 2;   // target at most this many levels above the bot
constexpr std::uint32_t SoloCcMinCurrentTargetHp = 30;   // below this the bot finishes its target before it CCs an add
constexpr std::uint32_t CcBrokenBeforeMs = 8000;   // own CC aura gone sooner than this after it was first seen = broken

struct Situation
{
    bool hasTarget = false;
    bool targetPlayer = false;
    bool targetElite = false;      // elite, rare elite or world boss
    std::uint32_t targetHp = 100;  // percent
    int levelGap = 0;              // target level - bot level
    std::uint32_t attackers = 0;   // units attacking the bot
    bool escapeReady = false;      // a class escape of the bot is known and off cooldown
    std::uint32_t fightTargetHp = 20;
};

// A ready escape first: its class node hangs on "critical health" itself, so it fires whether panic is quiet or not - a fight episode could
// not stop it (review 18.10.2026). Then fight on (no escape ready, a single, nearly dead, normal target), then the old flee.
inline bool FightFits(Situation const& s)
{
    return s.hasTarget && !s.targetPlayer && !s.targetElite && s.attackers <= 1 && s.levelGap <= int(MaxLevelGapForFight) &&
        s.targetHp <= s.fightTargetHp;
}

inline Choice Decide(Situation const& s)
{
    if (s.escapeReady)
        return Choice::Escape;
    if (FightFits(s))
        return Choice::Fight;
    return Choice::Flee;
}

// A fight episode stays a fight only while its conditions still hold (a second mob joins, the target heals). An escape whose cooldown ends
// during the fight is not re-checked: its node may fire (cooldowns of 3-60 min against fights of seconds). Residual: with
// AiPlayerbot.Diet.RetRepentanceKey = 1 (default 0) the Ret "repentance or shield" node can Repentance the fight target.
inline bool FightStillHolds(Situation const& s)
{
    return FightFits(s);
}

// An episode ends when panic is not seen for this long (the trigger is not evaluated outside combat, and a config reload can switch the
// ladder off and on mid-episode): the next evaluation starts a new one. Well above the longest normal gap between two engine passes in
// combat - a cast or channel holds the AI for its duration (Drain Life about 5 s plus the GCD) - so one HP dip stays one episode.
constexpr std::uint32_t EpisodeGapMs = 10000;

inline bool EpisodeStale(std::uint32_t nowMs, std::uint32_t lastSeenMs)
{
    return nowMs - lastSeenMs > EpisodeGapMs;
}

inline bool EscapeWindowOpen(std::uint32_t nowMs, std::uint32_t startMs)
{
    return nowMs - startMs < EscapeWindowMs;   // unsigned: also right across a getMSTime wrap
}

inline char const* ChoiceText(Choice c)
{
    switch (c)
    {
    case Choice::Fight: return "fight";
    case Choice::Escape: return "escape";
    default: return "flee";
    }
}

// "polymorph, Banish ,fear" -> {"polymorph", "banish", "fear"} (lower case, trimmed, empty items dropped).
inline std::vector<std::string> ParseSpellList(std::string const& text)
{
    std::vector<std::string> out;
    std::string item;
    auto flush = [&out, &item]()
    {
        std::size_t b = item.find_first_not_of(" \t");
        std::size_t e = item.find_last_not_of(" \t");
        if (b != std::string::npos)
            out.push_back(item.substr(b, e - b + 1));
        item.clear();
    };
    for (char c : text)
    {
        if (c == ',')
            flush();
        else
            item += char(std::tolower(static_cast<unsigned char>(c)));
    }
    flush();
    return out;
}

inline bool SpellAllowed(std::string const& spell, std::vector<std::string> const& allowed)
{
    for (std::string const& s : allowed)
        if (s == spell)
            return true;
    return false;
}

// Solo CC on an add: a normal mob that attacks the bot (or its pet), without a damage-over-time effect, while the bot's own target is not
// nearly dead (finish it first). Fear only without other mobs near that could be pulled ("possible adds" empty).
struct CcCandidate
{
    bool player = false;
    bool elite = false;
    bool attacksBotOrPet = false;
    bool hasDot = false;
    std::uint32_t currentTargetHp = 100;
};

inline bool SoloCcEligible(CcCandidate const& c)
{
    return !c.player && !c.elite && c.attacksBotOrPet && !c.hasDot && c.currentTargetHp > SoloCcMinCurrentTargetHp;
}

inline bool CcBroken(std::uint32_t firstSeenMs, std::uint32_t goneMs)
{
    return goneMs - firstSeenMs < CcBrokenBeforeMs;
}

// "[EmergencyLadder] bot= ... choice=" once per episode.
inline std::string FormatEpisodeLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, std::uint32_t level, char cohort,
    bool applied, char const* trigger, Choice choice, char const* escape, std::uint32_t hp, int mana, Situation const& s)
{
    char buf[384];
    std::snprintf(buf, sizeof(buf), "[EmergencyLadder] bot=%u name=%.40s cls=%u level=%u cohort=%c applied=%u trigger=%s choice=%s escape=%s "
        "hp=%u mana=%d target_hp=%u target_gap=%d target_elite=%u target_player=%u attackers=%u", guid, name.c_str(), cls, level, cohort,
        applied ? 1u : 0u, trigger, ChoiceText(choice), escape && *escape ? escape : "-", hp, mana, s.hasTarget ? s.targetHp : 0u,
        s.levelGap, s.targetElite ? 1u : 0u, s.targetPlayer ? 1u : 0u, s.attackers);
    std::string out = buf;
    for (char& c : out)
        if (c == '\n' || c == '\r')
            c = '_';
    return out;
}

inline std::string FormatSoloCcLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, char cohort, std::string const& spell,
    std::uint32_t targetEntry, std::uint32_t attackers)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[EmergencyLadder] solo-cc bot=%u name=%.40s cls=%u cohort=%c spell=%.32s target=%u attackers=%u", guid,
        name.c_str(), cls, cohort, spell.c_str(), targetEntry, attackers);
    return buf;
}

// Per class and cohort counters of one hour.
struct Counters
{
    std::uint64_t episodes = 0, flee = 0, escape = 0, fight = 0, escapeFallback = 0, fightAbort = 0, soloCc = 0, ccBroken = 0;
    bool Empty() const { return !episodes && !soloCc && !ccBroken; }
};

inline std::string FormatTotalLine(std::uint32_t windowMin, char cohort, std::uint32_t cls, Counters const& c, std::uint32_t dropped)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf), "[EmergencyLadderTotal] window_min=%u cohort=%c cls=%u episodes=%llu flee=%llu escape=%llu fight=%llu "
        "escape_fallback=%llu fight_abort=%llu solo_cc=%llu cc_broken=%llu dropped=%u", windowMin, cohort, cls,
        (unsigned long long)c.episodes, (unsigned long long)c.flee, (unsigned long long)c.escape, (unsigned long long)c.fight,
        (unsigned long long)c.escapeFallback, (unsigned long long)c.fightAbort, (unsigned long long)c.soloCc,
        (unsigned long long)c.ccBroken, dropped);
    return buf;
}
}
