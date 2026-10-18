#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::rot_cast
{
// twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace, default 0): cast diagnostics, measurement only.
// Pure rules, no game types, no locks, no config reads. The runtime (counters, budgets, flush) is RotCastTrace.cpp.
//
// Codes (one uint16 per counted outcome):
//   0x00..0xFE  a SpellCastResult byte: CanCastSpell said no with this result (eval), or the server sent this
//               result in SMSG_CAST_RESULT status 2 (server)
//   0xFF        ok (SPELL_CAST_OK): eval possible without a result, or SMSG_CAST_RESULT status 0
//   0x2nn       eval possible although CanCastSpell reported result nn (tolerated: range/LOS/facing passes)
//   0x1nn       eval reasons before CanCastSpell, see Eval*
//   0x3nn       start (CastSpellAction::Execute) and server packet classes, see Start* / Server*
constexpr std::uint8_t KindEval = 0;
constexpr std::uint8_t KindStart = 1;
constexpr std::uint8_t KindServer = 2;

constexpr std::uint16_t ResultOk = 0xFF;
constexpr std::uint16_t TolerFlag = 0x200;

constexpr std::uint16_t EvalMountOk = 0x100;
constexpr std::uint16_t EvalMountInCombat = 0x101;
constexpr std::uint16_t EvalNoTarget = 0x102;
constexpr std::uint16_t EvalNoSpellId = 0x103;
constexpr std::uint16_t EvalNoSpellEntry = 0x104;
constexpr std::uint16_t EvalOutOfReach = 0x105;
constexpr std::uint16_t EvalPetMissing = 0x106;    // no pet, or the pet is not safe
constexpr std::uint16_t EvalPetNotReady = 0x107;   // the pet lacks the spell or it is on cooldown
constexpr std::uint16_t EvalPetTooFar = 0x108;     // the pet is beyond sight distance of the bot
constexpr std::uint16_t EvalPetCannotReach = 0x109;
constexpr std::uint16_t EvalNoResult = 0x10A;      // CanCastSpell said no without reporting a result

constexpr std::uint16_t StartOk = 0x300;
constexpr std::uint16_t StartFail = 0x301;         // PlayerbotAI::CastSpell returned false
constexpr std::uint16_t StartNoTarget = 0x302;     // the target vanished between isPossible and Execute
constexpr std::uint16_t ServerOther = 0x303;       // SMSG_CAST_RESULT with a status other than 0 / 2
constexpr std::uint16_t ServerShort = 0x304;       // SMSG_CAST_RESULT shorter than its classic layout

// The CanCastSpell outcome as a code: possible + no result = ok, possible + result = tolerated, not possible = the
// result (or EvalNoResult). `result` is the SpellCastResult byte the caller passed as checkResult (SPELL_CAST_OK = 0xFF).
inline std::uint16_t EvalCode(bool possible, std::uint8_t result)
{
    if (possible)
        return result == ResultOk ? ResultOk : std::uint16_t(TolerFlag | result);
    return result == ResultOk ? EvalNoResult : std::uint16_t(result);
}

// SMSG_CAST_RESULT, classic layout (twow-core Spell::SendCastResult): u32 spell id, u8 status (0 ok, 2 fail),
// u8 result only with status 2.
inline std::uint16_t ServerCode(std::size_t size, std::uint8_t status, std::uint8_t result)
{
    if (size < 5)
        return ServerShort;
    if (status == 0)
        return ResultOk;
    if (status == 2)
        return size >= 6 ? std::uint16_t(result) : ServerShort;
    return ServerOther;
}

enum class Verdict : std::uint8_t { Ok, Tolerated, Fail };

inline Verdict VerdictOf(std::uint16_t code)
{
    if (code == ResultOk || code == EvalMountOk || code == StartOk)
        return Verdict::Ok;
    if ((code & 0xF00) == TolerFlag)
        return Verdict::Tolerated;
    return Verdict::Fail;
}

inline char const* VerdictText(Verdict verdict)
{
    switch (verdict)
    {
    case Verdict::Ok: return "ok";
    case Verdict::Tolerated: return "tolerated";
    default: return "fail";
    }
}

inline char const* KindText(std::uint8_t kind)
{
    switch (kind)
    {
    case KindEval: return "eval";
    case KindStart: return "start";
    case KindServer: return "server";
    default: return "other";
    }
}

inline std::string ReasonText(std::uint16_t code)
{
    switch (code)
    {
    case ResultOk: return "ok";
    case EvalMountOk: return "mount_ok";
    case EvalMountInCombat: return "mount_in_combat";
    case EvalNoTarget: return "no_target";
    case EvalNoSpellId: return "no_spell_id";
    case EvalNoSpellEntry: return "no_spell_entry";
    case EvalOutOfReach: return "out_of_reach";
    case EvalPetMissing: return "pet_missing";
    case EvalPetNotReady: return "pet_not_ready";
    case EvalPetTooFar: return "pet_too_far";
    case EvalPetCannotReach: return "pet_cannot_reach";
    case EvalNoResult: return "no_result";
    case StartOk: return "ok";
    case StartFail: return "cast_false";
    case StartNoTarget: return "no_target";
    case ServerOther: return "status_other";
    case ServerShort: return "short_packet";
    default: break;
    }
    if (code < 0x100 || (code & 0xF00) == TolerFlag)
        return "res_" + std::to_string(code & 0xFF);
    return "code_" + std::to_string(code);
}

// "Unknown" spells: the action has no spell id (the bot knows no rank of the spell), or CanCastSpell said
// SPELL_FAILED_NOT_KNOWN (passed in by the caller, the policy has no game headers).
inline bool IsUnknown(std::uint16_t code, std::uint8_t notKnown)
{
    return code == EvalNoSpellId || code == notKnown;
}

inline char const* UnknownText(std::uint16_t code)
{
    return code == EvalNoSpellId ? "no_spell_id" : "not_known";
}

// One counter key: class, cohort, kind, id (action slot for eval/start, spell id for server), code. Bit 63 is always
// set, so 0 can mark an empty slot.
struct Key
{
    std::uint8_t cls = 0;
    bool cohortB = false;
    std::uint8_t kind = 0;
    std::uint32_t id = 0;
    std::uint16_t code = 0;
};

constexpr std::uint64_t KeyMark = 1ull << 63;

inline std::uint64_t PackKey(std::uint8_t cls, bool cohortB, std::uint8_t kind, std::uint32_t id, std::uint16_t code)
{
    return KeyMark | (std::uint64_t(cls) << 48) | (std::uint64_t(cohortB ? 1 : 0) << 47) |
        (std::uint64_t(kind & 3u) << 44) | (std::uint64_t(id & 0xFFFFFFu) << 16) | std::uint64_t(code);
}

inline Key UnpackKey(std::uint64_t packed)
{
    Key key;
    key.cls = std::uint8_t((packed >> 48) & 0xFFu);
    key.cohortB = ((packed >> 47) & 1u) != 0;
    key.kind = std::uint8_t((packed >> 44) & 3u);
    key.id = std::uint32_t((packed >> 16) & 0xFFFFFFu);
    key.code = std::uint16_t(packed & 0xFFFFu);
    return key;
}

// Home slot of a key in a table of 2^bits slots: the top bits of the 64-bit Fibonacci product, so class, cohort,
// kind, id and code all move the slot (the low bits of the product would depend on the code alone).
inline std::size_t HomeSlot(std::uint64_t packed, unsigned bits)
{
    return std::size_t((packed * 0x9E3779B97F4A7C15ull) >> (64u - bits));
}

// A per-minute line budget: true (and one line taken) while fewer than `cap` lines were taken in the current
// 60 s window. A clock that went backwards starts a new window.
inline bool BudgetTake(std::uint32_t& windowStart, std::uint32_t& used, std::uint32_t now, std::uint32_t cap)
{
    if (now < windowStart || now >= windowStart + 60)
    {
        windowStart = now;
        used = 0;
    }
    if (used >= cap)
        return false;
    ++used;
    return true;
}

// A log token: at most `cap` characters, spaces and '=' replaced by '_', "..." when cut, "-" when empty.
inline std::string Token(std::string const& text, std::size_t cap)
{
    if (text.empty())
        return "-";
    std::string out = text.size() <= cap ? text : (cap < 3 ? text.substr(0, cap) : text.substr(0, cap - 3) + "...");
    for (char& c : out)
        if (c == ' ' || c == '=' || c == '\t' || c == '\n' || c == '\r')
            c = '_';
    return out;
}

enum Sum : std::uint8_t
{
    SumEvals, SumPossible, SumTolerated, SumStarts, SumStartOk, SumServerFail, SumServerOk, SumServerOther, SumCount
};

struct Summary
{
    std::uint64_t v[SumCount] = {};
};

// Adds one counted outcome to the summary.
inline void AddToSummary(Summary& summary, std::uint8_t kind, std::uint16_t code, std::uint64_t n)
{
    Verdict const verdict = VerdictOf(code);
    if (kind == KindEval)
    {
        summary.v[SumEvals] += n;
        if (verdict != Verdict::Fail)
            summary.v[SumPossible] += n;
        if (verdict == Verdict::Tolerated)
            summary.v[SumTolerated] += n;
    }
    else if (kind == KindStart)
    {
        summary.v[SumStarts] += n;
        if (verdict == Verdict::Ok)
            summary.v[SumStartOk] += n;
    }
    else if (kind == KindServer)
    {
        if (code == ResultOk)
            summary.v[SumServerOk] += n;
        else if (code == ServerOther || code == ServerShort)
            summary.v[SumServerOther] += n;
        else
            summary.v[SumServerFail] += n;
    }
}

// "[RotCast] window_s=60 ..." / "[RotCastTotal] window_min=60 ...".
inline std::string FormatSummary(char const* tag, char const* windowField, std::uint32_t window, Summary const& s,
    std::uint32_t keys, std::uint64_t dropped, std::uint32_t unknownDropped, std::uint32_t roleDropped, std::uint32_t pullDropped)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s %s=%u evals=%llu possible=%llu tolerated=%llu starts=%llu start_ok=%llu "
        "srv_fail=%llu srv_ok=%llu srv_other=%llu keys=%u dropped=%llu unknown_dropped=%u role_dropped=%u pull_dropped=%u",
        tag, windowField, window, (unsigned long long)s.v[SumEvals], (unsigned long long)s.v[SumPossible],
        (unsigned long long)s.v[SumTolerated], (unsigned long long)s.v[SumStarts], (unsigned long long)s.v[SumStartOk],
        (unsigned long long)s.v[SumServerFail], (unsigned long long)s.v[SumServerOk], (unsigned long long)s.v[SumServerOther],
        keys, (unsigned long long)dropped, unknownDropped, roleDropped, pullDropped);
    return buf;
}

// One top row; `subject` comes from ActionSubject (eval/start) or SpellSubject (server) and is already tokenised.
inline std::string FormatRow(char const* tag, std::uint32_t row, Key const& key, std::string const& subject, std::uint64_t n)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s row=%u cls=%u cohort=%c kind=%s %s verdict=%s reason=%s n=%llu", tag, row,
        unsigned(key.cls), key.cohortB ? 'B' : 'A', KindText(key.kind), subject.c_str(),
        VerdictText(VerdictOf(key.code)), ReasonText(key.code).c_str(), (unsigned long long)n);
    return buf;
}

inline std::string ActionSubject(std::string const& action)
{
    return "action=" + Token(action, 80);
}

inline std::string SpellSubject(std::uint32_t spellId, std::string const& name)
{
    return "spell=" + std::to_string(spellId) + " name=" + Token(name, 80);
}

inline std::string FormatUnknownLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, std::uint32_t level,
    char cohort, std::string const& action, std::uint32_t spellId, std::uint16_t code)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[RotCastUnknown] bot=%u name=%s cls=%u level=%u cohort=%c action=%s spell=%u reason=%s",
        guid, Token(name, 40).c_str(), cls, level, cohort, Token(action, 80).c_str(), spellId, UnknownText(code));
    return buf;
}

inline char const* RoleText(bool tank, bool heal)
{
    return tank ? "TANK" : (heal ? "HEAL" : "DPS");
}

inline std::string FormatRoleLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, std::uint32_t level,
    char cohort, int tab, std::string const& path, bool tank, bool heal, std::uint32_t forced, bool freeBot, bool member,
    bool autoLoad, std::uint32_t suppressed, std::string const& combat, std::string const& nonCombat)
{
    char buf[1536];
    std::snprintf(buf, sizeof(buf), "[RotRole] guid=%u name=%s cls=%u level=%u cohort=%c tab=%d path=%s role=%s forced=%u "
        "free=%u member=%u autoload=%u suppressed=%u co=%s nc=%s", guid, Token(name, 40).c_str(), cls, level, cohort, tab,
        Token(path, 40).c_str(), RoleText(tank, heal), forced, freeBot ? 1u : 0u, member ? 1u : 0u, autoLoad ? 1u : 0u,
        suppressed, Token(combat, 512).c_str(), Token(nonCombat, 512).c_str());
    return buf;
}

inline std::string FormatPullLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, std::uint32_t level,
    char cohort, std::uint32_t entry, std::uint32_t targetLevel, std::uint32_t hp, int mana, std::uint32_t attackers, float dist)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[RotPull] bot=%u name=%s cls=%u level=%u cohort=%c entry=%u tlevel=%u hp=%u mana=%d "
        "attackers=%u dist=%.1f", guid, Token(name, 40).c_str(), cls, level, cohort, entry, targetLevel, hp, mana, attackers,
        dist);
    return buf;
}
}
