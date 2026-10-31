//
// BotActionLog — per-bot detailed action log. See BotActionLog.h for design.
//

#include "playerbot/playerbot.h"
#include "BotActionLog.h"
#include "PlayerbotAI.h"
#include "BotDiagnostics.h"  // for SC_LOG + IsActionLogEnabled
#include "PlayerbotAIConfig.h"
#include "playerbot/ActionLogSamplePolicy.h"   // twow-repo#601 / #614: AiPlayerbot.ActionLog.Guids
#include "playerbot/CastResultNames.h"        // twow-repo#602: CAST_FAIL reason=
#include "playerbot/DamageLogPolicy.h"         // twow-repo#653: AiPlayerbot.ActionLog.DamageSampleEvery
#include "Objects/Player.h"
#include "Objects/Unit.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Spells/SpellAuraDefines.h"  // MAX_AURAS
#include "Spells/SpellMgr.h"
#include "Log.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace ai { namespace botdiag {

std::unordered_map<uint32, std::FILE*> BotActionLog::sFiles;

// sFiles is reachable from any map thread (Spell.cpp / Unit.cpp hooks fire
// on whatever thread is updating that bot's map). Guard every read/write so
// the unordered_map doesn't get torn under concurrent insert/erase. With
// AiPlayerbot.Perf.LogActionFastPath=1, GetHandle skips the lock while the log
// is off and no file is open (MayWrite); otherwise every Write takes it.
static std::mutex sFilesMutex;
// twow-repo#541 (audit A11): == sFiles.size(), stored under sFilesMutex after every insert/erase.
static std::atomic<std::size_t> sOpenFiles{0};

bool BotActionLog::MayWrite()
{
    return ai::botdiag::IsActionLogEnabled() || sOpenFiles.load(std::memory_order_acquire) != 0;
}

static void MakeDirIdempotent(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

void BotActionLog::EnsureLogDir()
{
    // mangosd's CWD depends on how it was launched. The typical live-server
    // layout puts it in bin/ (so logs/ is at ../logs), but CMake test-runs
    // launch it from the build dir where logs/ is adjacent. Try both;
    // MakeDirIdempotent silently ignores EEXIST.
    const char* roots[] = { "../logs", "logs", nullptr };
    for (int i = 0; roots[i]; ++i)
    {
        MakeDirIdempotent(roots[i]);
        std::string sub = std::string(roots[i]) + "/bots";
        MakeDirIdempotent(sub.c_str());
    }
}

std::string BotActionLog::BuildPath(Player* bot)
{
    if (!bot)
        return "";

    // Filename: <botname>_acc<sessionId>_<YYYYMMDD-HHMMSS>.log. The timestamp
    // distinguishes multiple summons of the same bot within one mangosd run:
    // a bounce-and-resummon writes a fresh file rather than appending.
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d-%H%M%S", &lt);

    uint32 sessionId = bot->GetSession() ? bot->GetSession()->GetAccountId() : 0u;

    std::ostringstream oss;
    oss << "../logs/bots/"
        << bot->GetName() << "_acc" << sessionId << "_" << tsBuf << ".log";
    return oss.str();
}

std::FILE* BotActionLog::Open(PlayerbotAI* ai)
{
    // Gate: when AiPlayerbot.EnableActionLog is off, never open log files.
    // All Write* methods route through Open and become silent no-ops.
    if (!ai::botdiag::IsActionLogEnabled()) return nullptr;
    if (!ai) return nullptr;
    Player* bot = ai->GetBot();
    if (!bot) return nullptr;

    uint32 guid = bot->GetGUIDLow();
    // twow-repo#601 / #614 (AiPlayerbot.ActionLog.Guids, owner 18.10.2026: a fixed sample per behaviour arm): a bot outside
    // the sample gets no file. Empty list = every bot, as before.
    if (!ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, guid))
        return nullptr;

    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sFiles.find(guid);
        if (it != sFiles.end() && it->second)
            return it->second;
    }

    EnsureLogDir();
    std::string path = BuildPath(bot);
    if (path.empty()) return nullptr;

    // Append rather than truncate: BuildPath embeds a timestamp so collisions
    // are unlikely, but append is the right default if one ever happens
    // (e.g. clock-skew, manual file resurrection).
    FILE* f = fopen(path.c_str(), "a");
    if (!f)
    {
        // Fallback: mangosd may have been launched from the layout root rather
        // than from bin/, in which case ../logs/bots/ doesn't exist but
        // logs/bots/ does. Retry with the bare path.
        const char* prefix = "../logs/bots/";
        std::string alt = path;
        if (alt.compare(0, std::strlen(prefix), prefix) == 0)
            alt = alt.substr(3); // drop the "../"
        f = fopen(alt.c_str(), "a");
        if (f) path = alt;
    }
    if (!f)
    {
        SC_LOG("BotActionLog::Open FAILED for bot=%s path=%s",
               bot->GetName(), path.c_str());
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        // Another thread may have raced us through the open()+fopen window
        // for the same guid. If so, close our handle and use theirs.
        auto race = sFiles.find(guid);
        if (race != sFiles.end() && race->second)
        {
            fclose(f);
            return race->second;
        }
        sFiles[guid] = f;
        sOpenFiles.store(sFiles.size(), std::memory_order_release);
    }
    SC_LOG("BotActionLog opened bot=%s path=%s", bot->GetName(), path.c_str());

    // Header line: human-readable; subsequent lines are tag-prefixed
    // events.
    fprintf(f, "# BotActionLog v1 — bot=%s guid=%u accountId=%u opened=%s\n",
            bot->GetName(), guid,
            bot->GetSession() ? bot->GetSession()->GetAccountId() : 0u,
            path.c_str());
    fflush(f);

    return f;
}

void BotActionLog::Close(PlayerbotAI* ai)
{
    if (!ai) return;
    Player* bot = ai->GetBot();
    if (!bot) return;

    uint32 guid = bot->GetGUIDLow();
    std::FILE* handle = nullptr;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sFiles.find(guid);
        if (it == sFiles.end()) return;
        handle = it->second;
        sFiles.erase(it);
        // twow-repo#541 (audit A11): Close runs at every logout; the count only moves after a
        // successful erase (the early return above leaves it untouched, as it leaves sFiles).
        sOpenFiles.store(sFiles.size(), std::memory_order_release);
    }
    if (handle)
    {
        fprintf(handle, "# end-of-log (Close called)\n");
        fflush(handle);
        fclose(handle);
    }
    SC_LOG("BotActionLog closed bot=%s", bot->GetName());
}

std::FILE* BotActionLog::GetHandle(PlayerbotAI* ai)
{
    if (!ai) return nullptr;
    // twow-repo#541 (audit A11, AiPlayerbot.Perf.LogActionFastPath): log off and no file open ->
    // the lookup below finds nothing and Open() refuses; skip the global mutex. Write() and
    // LogState() (engine tee, cast/aura/damage hooks of all bots) reach the lock only through here.
    if (sPlayerbotAIConfig.perfLogActionFastPath && !MayWrite())
        return nullptr;
    Player* bot = ai->GetBot();
    if (!bot) return nullptr;
    // twow-repo#601 / #614 (AiPlayerbot.ActionLog.Guids): a bot outside the sample never takes the global mutex below (with
    // 540 bots every engine line of every bot would otherwise queue on it). Empty list = every bot, as before.
    if (!ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, bot->GetGUIDLow()))
        return nullptr;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sFiles.find(bot->GetGUIDLow());
        if (it != sFiles.end() && it->second)
            return it->second;
    }
    // Lazy-open on first use. Open() itself gates on AiPlayerbot.EnableActionLog,
    // returning nullptr (and not creating any file) when the flag is off.
    return Open(ai);
}

static std::string TimestampWithMs()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[32];
    int n = (int)std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
    snprintf(buf + n, sizeof(buf) - n, ".%03d", (int)ms.count());
    return buf;
}

void BotActionLog::Write(PlayerbotAI* ai, const char* tag, const char* fmt, ...)
{
    FILE* f = GetHandle(ai);
    if (!f) return;

    char line[2048];
    int prefixLen = snprintf(line, sizeof(line), "[%s] [%s] ",
                              TimestampWithMs().c_str(), tag ? tag : "?");
    if (prefixLen < 0 || prefixLen >= (int)sizeof(line))
    {
        // prefix didn't fit (shouldn't happen) — bail
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + prefixLen, sizeof(line) - prefixLen, fmt, ap);
    va_end(ap);

    fprintf(f, "%s\n", line);
    fflush(f);
}

void BotActionLog::LogState(PlayerbotAI* ai, const char* reason)
{
    if (!ai) return;
    Player* bot = ai->GetBot();
    if (!bot) return;
    FILE* f = GetHandle(ai);
    if (!f) return;

    // Build a compact one-liner with all the per-tick state we usually
    // care about.
    uint32 hp     = bot->GetMaxHealth() ? (bot->GetHealth() * 100u / bot->GetMaxHealth()) : 0u;
    uint32 maxMana = bot->GetMaxPower(bot->GetPowerType());
    uint32 manaCur = bot->GetPower(bot->GetPowerType());
    uint32 mp     = maxMana ? (manaCur * 100u / maxMana) : 0u;
    bool inCombat = bot->IsInCombat();
    Unit* tgt     = bot->GetVictim();
    const char* tgtName = tgt ? tgt->GetName() : "(none)";

    Write(ai, "STATE",
          "reason=%s hp=%u%% mp=%u%% combat=%d alive=%d level=%u "
          "pos=%.1f,%.1f,%.1f,%.1f map=%u zone=%u "
          "target=%s targetGuid=0x%llx",
          reason ? reason : "tick",
          hp, mp, (int)inCombat, (int)bot->IsAlive(), bot->GetLevel(),
          bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(),
          bot->GetMapId(), bot->GetZoneId(),
          tgtName, (unsigned long long)(tgt ? tgt->GetObjectGuid().GetRawValue() : 0));

    // Aura snapshot: dump every aura currently
    // on the bot from m_spellAuraHolders. This lets us correlate what the user *sees*
    // in the client buff bar with what's actually in the server-side aura state. The
    // AURA_ATTEMPT hook only fires when a holder is being added; this dump shows the
    // steady-state list, so a buff that's been on the bot since before the log opened
    // (e.g. login-loaded persistent aura) still shows up here.
    //
    // We also dump UNIT_FIELD_AURA slot fields directly so we can detect mismatch
    // between the holder map and the visible-aura slots (which is what the client
    // actually renders).
    {
        std::string holders;
        holders.reserve(256);
        auto const& hm = bot->GetSpellAuraHolderMap();
        for (auto const& it : hm)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%u,", it.first);
            holders += buf;
        }
        if (holders.empty()) holders = "(none)";

        std::string slots;
        slots.reserve(256);
        for (uint32 slot = 0; slot < MAX_AURAS; ++slot)
        {
            uint32 sid = bot->GetUInt32Value(UNIT_FIELD_AURA + slot);
            if (sid)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "[%u]=%u ", slot, sid);
                slots += buf;
            }
        }
        if (slots.empty()) slots = "(none)";

        Write(ai, "AURA_DUMP", "holders=%s slots=%s", holders.c_str(), slots.c_str());
    }
}

void BotActionLog::LogCastStart(PlayerbotAI* ai, uint32 spellId, ObjectGuid targetGuid, uint32 castTimeMs)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "CAST_START",
          "spell=%s(%u) targetGuid=0x%llx castTime=%ums",
          spellName, spellId,
          (unsigned long long)targetGuid.GetRawValue(),
          castTimeMs);
}

void BotActionLog::LogCastResult(PlayerbotAI* ai, uint32 spellId, uint8 result, const char* phase, uint8 reason, uint8 state)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    if (result == 0)
    {
        Write(ai, "CAST_OK", "spell=%s(%u) result=%u phase=%s", spellName, spellId, (unsigned)result, phase ? phase : "?");
        return;
    }
    // twow-repo#602 (L1-b, OB-10): result=1 alone could not tell SPELL_IN_PROGRESS from LINE_OF_SIGHT or a bot-side cancel;
    // reason=none = no result went through this spell's member SendCastResult. With state=null that is usually a cast the bot
    // cancelled before any server check (PlayerbotAI::CastSpell, e.g. while moving), but a rejection sent only through the static
    // Spell::SendCastResult(Player*, SpellEntry const*, ...) overload reads the same - do not read "none" as "moving cancel" alone.
    char const* const name = ai::cast_result::Name(reason);
    Write(ai, "CAST_FAIL", "spell=%s(%u) result=%u phase=%s reason=%s code=0x%02x state=%s",
          spellName, spellId, (unsigned)result, phase ? phase : "?", name ? name : "none", (unsigned)reason,
          ai::cast_result::StateName(state));
}

void BotActionLog::LogAuraApply(PlayerbotAI* ai, uint32 spellId, int32 durationMs, ObjectGuid casterGuid, bool force)
{
    // Filter heuristic: skip very-short transient auras unless force=true.
    if (!force && durationMs > 0 && durationMs < 3000)
        return;

    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "AURA_APPLY",
          "spell=%s(%u) duration=%dms caster=0x%llx",
          spellName, spellId, durationMs,
          (unsigned long long)casterGuid.GetRawValue());
}

void BotActionLog::LogAuraRemove(PlayerbotAI* ai, uint32 spellId, ObjectGuid casterGuid, bool force)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "AURA_REMOVE",
          "spell=%s(%u) caster=0x%llx",
          spellName, spellId,
          (unsigned long long)casterGuid.GetRawValue());
}

}}  // namespace ai::botdiag — close so wrappers below are at global
    // scope where the host's forward-declarations expect them.

// ============================================================================
// Host-code hook wrappers. game.lib (Unit.cpp, Spell.cpp, etc.) calls these
// without #including the cmangos/playerbots header chain. Symbols resolve
// at the final mangosd.exe link step — same pattern as SC_PHASE.
//
// Bot-only filtering is done HERE so the host-code hooks just call the
// wrapper unconditionally and pay only the (very cheap) bot-AI null check
// when the player is a real human.
// ============================================================================
using ai::botdiag::BotActionLog;

namespace {
    inline PlayerbotAI* AiFor(Unit* u) {
        if (!u || !u->IsPlayer()) return nullptr;
        return GetBotAI(static_cast<Player*>(u));
    }
}

void BotActionLog_LogAuraApply(Unit* target, uint32 spellId, int32 durationMs, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    BotActionLog::LogAuraApply(ai, spellId, durationMs, ObjectGuid(casterGuidRaw), /*force*/ true);
}

// AURA_ATTEMPT: top-of-AddSpellAuraHolder hook. Logs every incoming aura BEFORE early-returns
// (refresh, stack, dead, debuff-limit, etc). Lets us see what's *trying* to land on a bot,
// even when it gets merged into an existing holder. Tag is AURA_ATTEMPT so the log clearly
// distinguishes from the post-success AURA_APPLY tag.
void BotActionLog_LogAuraAttempt(Unit* target, uint32 spellId, int32 durationMs, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    BotActionLog::Write(ai, "AURA_ATTEMPT",
                        "spell=%s(%u) duration=%dms caster=0x%llx",
                        spellName, spellId, durationMs,
                        (unsigned long long)casterGuidRaw);
}

void BotActionLog_LogAuraRemove(Unit* target, uint32 spellId, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    BotActionLog::LogAuraRemove(ai, spellId, ObjectGuid(casterGuidRaw), /*force*/ true);
}

void BotActionLog_LogCastStart(WorldObject* caster, uint32 spellId, uint64 targetGuidRaw, uint32 castTimeMs)
{
    if (!caster || !caster->IsPlayer()) return;
    PlayerbotAI* ai = GetBotAI(static_cast<Player*>(caster));
    if (!ai) return;
    BotActionLog::LogCastStart(ai, spellId, ObjectGuid(targetGuidRaw), castTimeMs);
}

void BotActionLog_LogCastResult(WorldObject* caster, uint32 spellId, uint8 result, const char* phase, uint8 reason, uint8 state)
{
    if (!caster || !caster->IsPlayer()) return;
    PlayerbotAI* ai = GetBotAI(static_cast<Player*>(caster));
    if (!ai) return;
    BotActionLog::LogCastResult(ai, spellId, result, phase, reason, state);
}

void BotActionLog_LogDamage(Unit* attacker, Unit* victim, uint32 damage, uint32 spellId, const char* damageType)
{
    // Throttle: only log every Nth damage event per bot to avoid log-spam in
    // raid-trash scenarios. Per-bot counters via thread_local maps keyed by guid.
    // twow-repo#653 (AiPlayerbot.ActionLog.DamageSampleEvery, default 5; DamageLogPolicy.h): dealt and taken have their own
    // counter (one shared counter made a 2nd attacker's first logged hit ~10 s late), and a bot's pet / totem / guardian damage
    // goes to the owner's log as DMG_DEALT with via= (it was never logged).
    // OB-10 review 31.10.2026: with the action log off (production, perf runs) nothing below runs - no minion lookup, no counter;
    // the same gate as every Write (EnableActionLog, or files still open after a runtime switch-off).
    if (!BotActionLog::MayWrite())
        return;
    static thread_local std::unordered_map<uint64, uint32> sDealt;
    static thread_local std::unordered_map<uint64, uint32> sTaken;
    uint32 const every = sPlayerbotAIConfig.actionLogDamageSampleEvery;

    PlayerbotAI* attackerAi = AiFor(attacker);
    PlayerbotAI* victimAi   = AiFor(victim);
    // The minion's owner: a creature whose charmer / owner guid is a player (one field read before anything else, so the
    // creature-on-creature hits of the world stay two pointer checks and a guid test).
    Unit* via = nullptr;
    Unit* dealer = attacker;
    if (!attackerAi && attacker && !attacker->IsPlayer() && attacker->GetCharmerOrOwnerGuid().IsPlayer())
    {
        if (Player* owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            if (PlayerbotAI* ownerAi = GetBotAI(owner))
            {
                attackerAi = ownerAi;
                via = attacker;
                dealer = owner;
            }
        }
    }
    // A bot outside AiPlayerbot.ActionLog.Guids gets no file: it is not counted either (the sample check before the counters).
    if (attackerAi && !ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, dealer->GetGUIDLow()))
        attackerAi = nullptr;
    if (victimAi && !ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, victim->GetGUIDLow()))
        victimAi = nullptr;
    if (!attackerAi && !victimAi) return;

    auto bucket = [&](std::unordered_map<uint64, uint32>& counter, Unit* who) -> bool {
        uint64 g = who ? who->GetObjectGuid().GetRawValue() : 0;
        return ai::damage_log::Due(++counter[g], every);
    };

    if (attackerAi && bucket(sDealt, dealer)) {
        if (via)
            BotActionLog::Write(attackerAi, "DMG_DEALT",
                "victim=0x%llx victimName=%s spell=%u dmg=%u type=%s via=0x%llx viaName=%s",
                (unsigned long long)(victim ? victim->GetObjectGuid().GetRawValue() : 0),
                victim ? victim->GetName() : "?",
                spellId, damage, damageType ? damageType : "?",
                (unsigned long long)via->GetObjectGuid().GetRawValue(), via->GetName());
        else
            BotActionLog::Write(attackerAi, "DMG_DEALT",
                "victim=0x%llx victimName=%s spell=%u dmg=%u type=%s",
                (unsigned long long)(victim ? victim->GetObjectGuid().GetRawValue() : 0),
                victim ? victim->GetName() : "?",
                spellId, damage, damageType ? damageType : "?");
    }
    if (victimAi && bucket(sTaken, victim)) {
        BotActionLog::Write(victimAi, "DMG_TAKEN",
            "attacker=0x%llx attackerName=%s spell=%u dmg=%u type=%s",
            (unsigned long long)(attacker ? attacker->GetObjectGuid().GetRawValue() : 0),
            attacker ? attacker->GetName() : "?",
            spellId, damage, damageType ? damageType : "?");
    }
}

