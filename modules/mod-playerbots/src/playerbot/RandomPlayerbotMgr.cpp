// See PlayerbotMgr.cpp: botpch.h supplied <regex>, and the module build has
// no precompiled header.
#include <regex>
#include <limits>
#include <array>
#include <map>
#include <set>

#include "Config/Config.h"

#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PersistentRosterBagPolicy.h"
#include "playerbot/PersistentRosterStarterOutfitPolicy.h"
#include "playerbot/ProfessionPair.h"
#include "PlayerbotDatabaseContract.h"
#include "playerbot/ActionBudgetPolicy.h"
#include "playerbot/PacketTracePolicy.h"
#include "playerbot/UnreachableTargetPolicy.h"
#include "playerbot/BuffTargetPolicy.h"
#include "playerbot/ZoneExitPolicy.h"
#include "playerbot/QuestFailureClassPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "playerbot/PerformanceMonitor.h"
#include "strategy/values/LastMovementValue.h"
#include "AccountMgr.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"
#include "BotDiagnostics.h"  // for SC_LOG
#include "Objects/Player.h"
#include "playerbot/AiFactory.h"
#include "playerbot/StartZonePolicy.h"
#include "PlayerbotCommandServer.h"
#include "MemoryMonitor.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "FleeManager.h"
#include "playerbot/ServerFacade.h"

#include "Battlegrounds/BattleGround.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include "Battlegrounds/BattleGroundWS.h"
#include "Chat/ChannelMgr.h"
#include "Guild/GuildMgr.h"
#include "World/WorldState.h"
#include "PlayerbotLoginMgr.h"
#include "PersistentActiveRoster.h"
#include "PersistentActiveRosterDatabase.h"
#include "LoginWavePolicy.h"
#include "playerbot/MemStoresPolicy.h"
#include "playerbot/BotUpdateTrace.h"
#include "playerbot/CombatIdlePolicy.h"
#include "playerbot/PathReusePolicy.h"
#include "playerbot/RotCastTrace.h"   // twow-repo#602 ROT-01 D0
#if defined(__linux__) && defined(__GLIBC__)
#include <malloc.h>
#endif
#include <cstdio>
#include "playerbot/StallGuardPolicy.h"
#include "playerbot/PathFailPolicy.h"
#include "playerbot/RoutePrecheckPolicy.h"
#include "playerbot/RouteTimePolicy.h"
#include "playerbot/PathUnitlessBotPolicy.h"
#include "playerbot/BgFillPolicy.h"
#include "playerbot/strategy/actions/BattleGroundJoinAction.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "Maps/MapManager.h"
#include "Maps/Map.h"
#include "Maps/GridMap.h"
#include "ObjectAccessor.h"
#include "playerbot/BotSlots.h"
#include "playerbot/ShortcutPolicy.h"
#include <fstream>
#include <sstream>
#include <functional>
#include "Transports/Transport.h"

#ifndef MANGOSBOT_ZERO
#ifdef CMANGOS
#include "Arena/ArenaTeam.h"
#endif
#ifdef MANGOS
#include "ArenaTeam.h"
#endif
#endif

#include "playerbot/TravelMgr.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <float.h>

#if PLATFORM == PLATFORM_WINDOWS
#include "windows.h"
#include "psapi.h"
#endif

using namespace ai;
using namespace MaNGOS;

INSTANTIATE_SINGLETON_1(RandomPlayerbotMgr);

namespace
{
// #391: login waves after an L1 reset. Built once from the roster members the
// reset flagged (characters.at_login & 6 == 6); the core clears that flag at
// the first login, so a restart in the middle simply continues with the rest.
struct RosterLoginWaves
{
    bool built = false;
    time_t start = 0;
    std::map<uint32, uint32> waveOf;     // guid -> wave
    std::vector<uint32> botsPerWave;
    uint32 loggedWaves = 0;             // waves already announced
    bool completeLogged = false;
};

RosterLoginWaves& LoginWaves()
{
    static RosterLoginWaves waves;
    return waves;
}

void BuildRosterLoginWaves(std::vector<uint32> const& desired, std::function<bool(uint32)> isMember)
{
    RosterLoginWaves& waves = LoginWaves();
    waves.built = true;
    waves.start = time(nullptr);
    if (!sPlayerbotAIConfig.persistentActiveRosterLoginWaveSize)
        return;

    std::map<uint32, uint32> startZone;
    if (auto result = CharacterDatabase.Query("SELECT guid, race, class FROM characters WHERE (at_login & 6) = 6"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const guid = fields[0].GetUInt32();
            if (!isMember(guid))
                continue;
            PlayerInfo const* info = sObjectMgr.GetPlayerInfo(fields[1].GetUInt8(), fields[2].GetUInt8());
            startZone[guid] = info ? ((info->mapId << 16) | (info->areaId & 0xFFFF)) : 0;
        } while (result->NextRow());
    }

    std::vector<login_wave::Candidate> candidates;
    for (uint32 guid : desired)     // roster order
    {
        auto const it = startZone.find(guid);
        if (it != startZone.end())
            candidates.push_back({ guid, it->second });
    }

    waves.waveOf = login_wave::AssignWaves(candidates, sPlayerbotAIConfig.persistentActiveRosterLoginWaveSize);
    for (auto const& [guid, wave] : waves.waveOf)
    {
        if (waves.botsPerWave.size() <= wave)
            waves.botsPerWave.resize(wave + 1, 0);
        ++waves.botsPerWave[wave];
    }
    if (!waves.waveOf.empty())
        sLog.outBasic("[RosterLoginWave] planned bots=%u waves=%u wave_size=%u interval_seconds=%u",
            uint32(waves.waveOf.size()), uint32(waves.botsPerWave.size()),
            sPlayerbotAIConfig.persistentActiveRosterLoginWaveSize, sPlayerbotAIConfig.persistentActiveRosterLoginWaveIntervalSeconds);
}

// True when this roster bot may log in now. Announces every newly opened wave.
bool RosterLoginWaveOpen(uint32 guid)
{
    RosterLoginWaves& waves = LoginWaves();
    if (waves.waveOf.empty())
        return true;

    uint32 const total = uint32(waves.botsPerWave.size());
    uint32 const elapsed = uint32(time(nullptr) - waves.start);
    uint32 const open = login_wave::OpenWaves(total, elapsed, sPlayerbotAIConfig.persistentActiveRosterLoginWaveIntervalSeconds);
    while (waves.loggedWaves < open)
    {
        uint32 waiting = 0;
        for (uint32 w = waves.loggedWaves + 1; w < total; ++w)
            waiting += waves.botsPerWave[w];
        sLog.outBasic("[RosterLoginWave] wave=%u/%u opened bots=%u waiting=%u",
            waves.loggedWaves + 1, total, waves.botsPerWave[waves.loggedWaves], waiting);
        ++waves.loggedWaves;
    }
    if (open == total && !waves.completeLogged)
    {
        waves.completeLogged = true;
        sLog.outBasic("[RosterLoginWave] complete waves=%u waiting=0", total);
    }

    auto const it = waves.waveOf.find(guid);
    return it == waves.waveOf.end() || login_wave::IsOpen(it->second, elapsed, sPlayerbotAIConfig.persistentActiveRosterLoginWaveIntervalSeconds);
}
}

#ifdef CMANGOS
#include <boost/thread/thread.hpp>
#endif

#ifdef MANGOS
class CheckBgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckBgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckBgQueueThread()
{
    sRandomPlayerbotMgr.CheckBgQueue();
}
#endif

#ifdef MANGOS
class CheckLfgQueueThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckLfgQueue(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckLfgQueueThread()
{
    sRandomPlayerbotMgr.CheckLfgQueue();
}
#endif

#ifdef MANGOS
class CheckPlayersThread : public ACE_Task <ACE_MT_SYNCH>
{
public:
    int svc(void) { sRandomPlayerbotMgr.CheckPlayers(); return 0; }
};
#endif
#ifdef CMANGOS
void CheckPlayersThread()
{
    sRandomPlayerbotMgr.CheckPlayers();
}
#endif

class botPIDImpl
{
public:
    botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd);
    ~botPIDImpl();
    double calculate(double setpoint, double pv);
    void adjust(double Kp, double Ki, double Kd) { _Kp = Kp; _Ki = Ki; _Kd = Kd; }
    void reset() { _integral = 0; }

private:
    double _dt;
    double _max;
    double _min;
    double _Kp;
    double _Ki;
    double _Kd;
    double _pre_error;
    double _integral;
};


botPID::botPID(double dt, double max, double min, double Kp, double Ki, double Kd)
{
    pimpl = new botPIDImpl(dt, max, min, Kp, Ki, Kd);
}
void botPID::adjust(double Kp, double Ki, double Kd)
{
    pimpl->adjust(Kp, Ki, Kd);
}
void botPID::reset()
{
    pimpl->reset();
}
double botPID::calculate(double setpoint, double pv)
{
    return pimpl->calculate(setpoint, pv);
}
botPID::~botPID()
{
    delete pimpl;
}


/**
 * Implementation
 */
botPIDImpl::botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd) :
    _dt(dt),
    _max(max),
    _min(min),
    _Kp(Kp),
    _Ki(Ki),
    _Kd(Kd),
    _pre_error(0),
    _integral(0)
{
}

double botPIDImpl::calculate(double setpoint, double pv)
{

    // Calculate error
    double error = setpoint - pv;

    // Proportional term
    double Pout = _Kp * error;

    // Integral term
    _integral += error * _dt;

    double Iout = _Ki * _integral;

    // Derivative term
    double derivative = (error - _pre_error) / _dt;
    double Dout = _Kd * derivative;

    // Calculate total output
    double output = Pout + Iout + Dout;

    // Restrict to max/min
    if (output > _max)
    {
        output = _max;
        _integral -= error * _dt; //Stop integral buildup at max
    }
    else if (output < _min)
    {
        output = _min;
        _integral -= error * _dt; //Stop integral buildup at min
    }

    // Save error to previous error
    _pre_error = error;

    return output;
}

botPIDImpl::~botPIDImpl()
{
}

RandomPlayerbotMgr::RandomPlayerbotMgr() 
: PlayerbotHolder()
, processTicks(0)
, loginProgressBar(NULL)
{
    if (sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.randomBotAutologin)
    {
        sPlayerbotCommandServer.Start();
        PrepareTeleportCache();

        for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
        {
            for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
            {
                BgPlayers[j][i][0] = 0;
                BgPlayers[j][i][1] = 0;
                BgBots[j][i][0] = 0;
                BgBots[j][i][1] = 0;
                ArenaBots[j][i][0][0] = 0;
                ArenaBots[j][i][0][1] = 0;
                ArenaBots[j][i][1][0] = 0;
                ArenaBots[j][i][1][1] = 0;
                NeedBots[j][i][0] = false;
                NeedBots[j][i][1] = false;
            }
        }

        //1) Proportional: Amount activity is adjusted based on diff being above or below wanted diff. (100 wanted diff & 0.1 p = 150 diff = -5% activity)
        //2) Integral: Same as proportional but builds up each tick. (100 wanted diff & 0.01 i = 150 diff = -0.5% activity each tick)
        //3) Derative: Based on speed of diff. (+5 diff last tick & 0.05 d = -0.25% activity)
        pid.adjust(0.05,0.001,0.05);
        BgCheckTimer = 0;
        LfgCheckTimer = 0;
        PlayersCheckTimer = 0;
        EventTimeSyncTimer = 0;
        OfflineGroupBotsTimer = 0;
        guildsDeleted = false;
        arenaTeamsDeleted = false;

        std::list<uint32> availableBots = GetBots();

        for (auto& bot : availableBots)
        {
            if(GetEventValue(bot,"login"))
                SetEventValue(bot, "login", 0, 0);
        }

#ifndef MANGOSBOT_ZERO
        // load random bot team members
        auto results = CharacterDatabase.PQuery("SELECT guid FROM arena_team_member");
        if (results)
        {
            sLog.outString("Loading arena team bot members...");
            do
            {
                Field* fields = results->Fetch();
                uint32 lowguid = fields[0].GetUInt32();
                arenaTeamMembers.push_back(lowguid);
            } while (results->NextRow());
        }
#endif
        // sync event timers
        SyncEventTimers();

        for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
        {
            if (!sMapStore.LookupEntry(i))
                continue;

            uint32 mapId = sMapStore.LookupEntry(i)->MapID;
            facingFix[mapId] = {};
        }

        showLoginWarning = true;
    }
}

RandomPlayerbotMgr::~RandomPlayerbotMgr()
{
    if (persistentRoster)
        persistentRoster->Stop();
}

bool RandomPlayerbotMgr::InitializePersistentRoster()
{
    if (!sPlayerbotAIConfig.persistentActiveRosterEnabled)
        return true;
    if (persistentRosterInitialized)
        return PersistentRosterAdmissionOpen();
    persistentRosterInitialized = true;
    persistentRoster.reset(new ai::roster::Service(std::unique_ptr<ai::roster::Store>(new ai::roster::CharacterDatabaseStore())));
    std::string error;
    bool ok = persistentRoster->Start(true, sPlayerbotAIConfig.asyncBotLogin, error);
    if (!ok)
        sLog.outError("[PersistentRoster] admission closed: state=%s code=%s",
            ai::roster::RuntimeStateName(persistentRoster->State()).c_str(), error.c_str());
    else
        sLog.outString("[PersistentRoster] loaded version " UI64FMTD " with %u ordered GUIDs",
            persistentRoster->CurrentVersionId(), static_cast<uint32>(persistentRoster->Desired().size()));
    return ok;
}

bool RandomPlayerbotMgr::IsPersistentRosterMember(uint32 guidLow) const
{
    return sPlayerbotAIConfig.persistentActiveRosterEnabled && persistentRoster && persistentRoster->IsMember(guidLow);
}

std::vector<uint32> RandomPlayerbotMgr::PersistentRosterGuids() const
{
    if (!sPlayerbotAIConfig.persistentActiveRosterEnabled || !persistentRoster)
        return {};
    std::vector<uint32_t> const& desired = persistentRoster->Desired();
    return std::vector<uint32>(desired.begin(), desired.end());
}

bool RandomPlayerbotMgr::PersistentRosterAdmissionOpen() const
{
    if (!sPlayerbotAIConfig.persistentActiveRosterEnabled)
        return true;
    if (sPlayerbotAIConfig.persistentActiveRosterMaintenanceMode)
        return false;
    if (!persistentRoster)
        return false;
    ai::roster::RuntimeState state = persistentRoster->State();
    return state == ai::roster::RuntimeState::STARTING || state == ai::roster::RuntimeState::HEALTHY ||
        state == ai::roster::RuntimeState::DEGRADED;
}

bool RandomPlayerbotMgr::PersistentRosterDestructiveMutationAllowed(uint32 guidLow) const
{
    if (!sPlayerbotAIConfig.persistentActiveRosterEnabled)
        return true;
    if (!persistentRoster)
        return false;
    ai::roster::RuntimeState state = persistentRoster->State();
    if (state == ai::roster::RuntimeState::INVALID_FAIL_CLOSED || state == ai::roster::RuntimeState::LOADING)
        return false;
    return !persistentRoster->IsMember(guidLow);
}

std::string RandomPlayerbotMgr::GetPersistentRosterState() const
{
    return persistentRoster ? ai::roster::RuntimeStateName(persistentRoster->State()) : "DISABLED";
}

bool RandomPlayerbotMgr::ApplyPersistentRosterRequest(std::string const& canonicalRequest, std::string& result)
{
    bool rosterBotActuallyOnline = false;
    if (persistentRoster)
        for (uint32 guid : persistentRoster->Desired())
            if (GetPlayerBot(guid))
            {
                rosterBotActuallyOnline = true;
                break;
            }
    result = ai::roster::EvaluateRosterApplyGate(sPlayerbotAIConfig.persistentActiveRosterEnabled,
        persistentRoster != nullptr, sPlayerbotAIConfig.persistentActiveRosterMaintenanceMode,
        persistentRoster ? persistentRoster->Status().rosterOnline : 0, rosterBotActuallyOnline);
    if (!result.empty())
    {
        return false;
    }
    ai::roster::ApplyResult applied = persistentRoster->Apply(canonicalRequest);
    result = applied.code;
    if (applied.accepted)
    {
        currentBots.clear();
        sLog.outString("[PersistentRoster] operation result=%s replay=%u restart_required=%u version=" UI64FMTD,
            result.c_str(), applied.replayed ? 1u : 0u, applied.restartRequired ? 1u : 0u, applied.versionId);
    }
    else
        sLog.outError("[PersistentRoster] operation rejected code=%s", result.c_str());
    return applied.accepted;
}

int RandomPlayerbotMgr::GetMaxAllowedBotCount()
{
    return GetEventValue(0, "bot_count");
}

inline void print_line(Unit* bot, const std::vector<std::pair<int, int>> line, bool is_sqDist_greater_200)
{
    std::ostringstream out;
    out << bot->GetName() << ",";
    out << std::fixed << std::setprecision(1);
    out << "\"LINESTRING(";
    for (auto& p : line)
    {
        out << p.first << " " << p.second << (&p == &line.back() ? "" : ",");
    }    
    out << ")\",";
    out << bot->GetOrientation() << ",";
    out << std::to_string(bot->getRace()) << ",";
    out << std::to_string(bot->getClass()) << ",";
    out << (is_sqDist_greater_200 ? "1" : "0");
    sPlayerbotAIConfig.log("player_paths.csv", out.str().c_str());
}

inline void print_path(Unit* bot, std::vector<std::pair<int, int>>& log)
{
    std::vector<std::pair<int, int>> line;

    std::pair<int, int> lastP = {0, 0};

    for (auto& p : log)
    {
        if (lastP.first && lastP.second && pow(lastP.first - p.first, 2) + pow(lastP.second - p.second, 2) > 200 * 200)
        {
            if (line.size()>1)
                print_line(bot, line, false);      //Print previous path.
            print_line(bot, {lastP, p}, true); //Print jump.
            line.clear();
        }
        line.push_back(p);
        lastP = p;
    }
    if (line.size() > 1)
        print_line(bot, line, false); //Print remaining path.
}

void RandomPlayerbotMgr::LogPlayerLocation()
{
    botCount = 0;
    activeBots = 0;
    if (sPlayerbotAIConfig.randomBotAutologin)
    {
        ForEachPlayerbot([&](Player* bot) {
            if (GetBotAI(bot))
            {

                botCount++;
                if (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY))
                {
                    activeBots++;
                }
            }
        });
    }

    for (auto i : GetPlayers())
    {
        Player* bot = i.second;
        if (!bot)
            continue;
        if (GetBotAI(bot))
        {
            botCount++;
            if (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY))
                activeBots++;
        }
    }

    if (sPlayerbotAIConfig.hasLog("player_location.csv"))
    {
        try
        {
            sPlayerbotAIConfig.openLog("player_location.csv", "w");

            if (sPlayerbotAIConfig.hasLog("player_route.csv"))
                sPlayerbotAIConfig.openLog("player_route.csv", "w");

            if (sPlayerbotAIConfig.randomBotAutologin)
            {
                ForEachPlayerbot([&](Player* bot) {
                    std::ostringstream out;
                    out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                    out << "RND" << ",";
                    out << bot->GetName() << ",";
                    out << std::fixed << std::setprecision(2);
                    WorldPosition(bot).printWKT(out);
                    out << bot->GetOrientation() << ",";
                    out << std::to_string(bot->getRace()) << ",";
                    out << std::to_string(bot->getClass()) << ",";
                    out << bot->GetMapId() << ",";
                    out << bot->GetLevel() << ",";
                    out << bot->GetHealth() << ",";
                    out << bot->GetPowerPercent() << ",";
                    out << bot->GetMoney() << ",";

                    if (GetBotAI(bot))
                    {
                        out << std::to_string(uint8(GetBotAI(bot)->GetGrouperType())) << ",";
                        out << std::to_string(uint8(GetBotAI(bot)->GetGuilderType())) << ",";
                        out << (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                        out << (GetBotAI(bot)->IsActive() ? "active" : "delay") << ",";
                        out << GetBotAI(bot)->HandleRemoteCommand("state") << ",";
                        PlayerbotAI* ai = GetBotAI(bot);
                        AiObjectContext* context = ai->GetAiObjectContext();

                        out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";

                        if (sPlayerbotAIConfig.hasLog("player_route.csv") && WorldPosition(bot))
                        {
                            LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");

                            std::vector<PathNodePoint> fullPath = lastMove.lastPath.getPath();

                            if (!fullPath.empty())
                            {
                                std::vector<std::pair<std::vector<WorldPosition>, bool>> splitPath;

                                bool currentWalkable = fullPath[0].isWalkable();
                                std::vector<WorldPosition> currentSegment;
                                currentSegment.push_back(fullPath[0].point);

                                for (size_t i = 1; i < fullPath.size(); i++)
                                {
                                    bool walkable = fullPath[i].isWalkable();

                                    if (walkable != currentWalkable)
                                    {
                                        // End current segment, start new one beginning with the last point
                                        splitPath.push_back({currentSegment, currentWalkable});
                                        currentSegment.clear();
                                        currentSegment.push_back(fullPath[i - 1].point); // shared junction point
                                        currentWalkable = walkable;
                                    }

                                    currentSegment.push_back(fullPath[i].point);
                                }

                                splitPath.push_back({currentSegment, currentWalkable});

                                uint32 segmentNr = 0;

                                for (auto& [segement, walkable] : splitPath)
                                {
                                    segmentNr++;
                                    std::ostringstream out;
                                    out << bot->GetName() << ",";
                                    out << std::fixed << std::setprecision(1);

                                    out << segmentNr << ",";

                                    WorldPosition().printWKT(segement, out, 1, false);

                                    out << bot->GetOrientation() << ",";
                                    out << std::to_string(bot->getRace()) << ",";
                                    out << std::to_string(bot->getClass()) << ",";
                                    out << (walkable ? "1" : "0") << ",";
                                    out << lastMove.moveEvent.getSource();
                                    sPlayerbotAIConfig.log("player_route.csv", out.str().c_str());
                                }
                            }
                        }
                    }
                    else
                    {
                        out << 0 << "," << 0 << ",err,err,err,err,";
                    }

                    out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                    out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                    if (bot->GetGroup())
                        WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                    sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                    if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                    {
                        auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                        std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                        botMoveLog.push_back(curDisplayPos);

                        if (botMoveLog.size() > 100)
                        {
                            print_path(bot, botMoveLog);
                            botMoveLog.clear();
                            botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                        }
                    }
                });
            }

            for (auto i : GetPlayers())
            {
                Player* bot = i.second;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "PLR" << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->getRace()) << ",";
                out << std::to_string(bot->getClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPercent() << ",";
                out << bot->GetMoney() << ",";
                if (GetBotAI(bot))
                {
                    out << std::to_string(uint8(GetBotAI(bot)->GetGrouperType())) << ",";
                    out << std::to_string(uint8(GetBotAI(bot)->GetGuilderType())) << ",";
                    out << (GetBotAI(bot)->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (GetBotAI(bot)->IsActive() ? "active" : "delay") << ",";
                    out << GetBotAI(bot)->HandleRemoteCommand("state") << ",";
                    PlayerbotAI* ai = GetBotAI(bot);
                    AiObjectContext* context = ai->GetAiObjectContext();

                    out << (AI_VALUE(bool, "should get money") ? "should get money" : "has enough money") << ",";
                }
                else
                {
                    out << 0 << "," << 0 << ",player,player,player,player,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->IsDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive") << ",";

                if (bot->GetGroup())
                    WorldPosition(bot).printWKT({bot, sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid())}, out, 1);

                sPlayerbotAIConfig.log("player_location.csv", out.str().c_str());

                if (sPlayerbotAIConfig.hasLog("player_paths.csv") && WorldPosition(bot))
                {
                    auto& botMoveLog = playerBotMoveLog[bot->GetObjectGuid().GetCounter()];

                    std::pair<int32, int32> curDisplayPos = std::make_pair(WorldPosition(bot).getDisplayX(), WorldPosition(bot).getDisplayY());

                    botMoveLog.push_back(curDisplayPos);

                    if (botMoveLog.size() > 100)
                    {
                        print_path(bot, botMoveLog);
                        botMoveLog.clear();
                        botMoveLog.push_back(curDisplayPos); //Start next path at current position.
                    }
                }
            }
        }
        catch (...)
        {
            return;
            //This is to prevent some thread-unsafeness. Crashes would happen if bots get added or removed.
            //We really don't care here. Just skip a log. Making this thread-safe is not worth the effort.
        }
    }
    if (sPlayerbotAIConfig.hasLog("transport.csv"))
    {
        sPlayerbotAIConfig.openLog("transport.csv", "w");
        for (auto& [mapId, map] : sMapMgr.Maps())
        {
            for (auto& transport : WorldPosition(map->GetId(), 1, 1).getTransports())
            {
                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                if (transport->GetName() == nullptr || transport->GetName()[0] == '\0')
                {
                    GameObjectInfo const* data = sGOStorage.LookupEntry<GameObjectInfo>(transport->GetEntry());
                    out << data->name << ",";
                }
                else
                    out << transport->GetName() << ",";

                out << transport->GetEntry() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(transport).printWKT(out);
                out << transport->GetOrientation();

                sPlayerbotAIConfig.log("transport.csv", out.str().c_str());
            }
        }
    }
}

namespace
{
// twow-repo#416 / #319 (7.3): one [MemStores] line per hour - resident memory
// next to the loaded grids and objects per map and the bounded bot stores, to
// find the ~300 MB/h growth. Runs on the world thread; the map list is read
// under the MapManager lock, the object stores under their own locks.
void ReportMemStores(PlayerBotMap const& bots)
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!ai::mem_stores::Due(lastReport, now))
        return;
    lastReport = now;

    uint64 rssKb = 0;
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::stringstream text;
    text << status.rdbuf();
    rssKb = ai::mem_stores::ParseVmRssKb(text.str());
#endif

    // #452: glibc allocator - held by malloc (sbrk + mmap), in use, free but held,
    // arenas. In use growing with RSS = real growth; free or arenas = fragmentation.
    uint64 heapKb = 0, inuseKb = 0, freeKb = 0;
    uint32 arenas = 0;
#if defined(__linux__) && defined(__GLIBC__)
    struct mallinfo2 const info = mallinfo2();
    heapKb = (info.arena + info.hblkhd) / 1024;
    inuseKb = (info.uordblks + info.hblkhd) / 1024;
    freeKb = info.fordblks / 1024;
    char* xml = nullptr;
    size_t xmlSize = 0;
    if (FILE* out = open_memstream(&xml, &xmlSize))
    {
        malloc_info(0, out);
        fclose(out);
        arenas = ai::mem_stores::CountArenas(std::string(xml, xmlSize));
    }
    free(xml);
#endif

    std::vector<ai::mem_stores::MapStat> stats;
    ai::mem_stores::MapStat total;
    {
        MaNGOS::ClassLevelLockable<MapManager, std::recursive_mutex>::Lock guard(sMapMgr);
        for (auto const& [id, map] : sMapMgr.Maps())
        {
            if (!map)
                continue;

            std::pair<uint32, uint32> const objects = map->CountStoredObjects();
            ai::mem_stores::MapStat stat;
            stat.mapId = map->GetId();
            stat.instanceId = map->GetInstanceId();
            stat.grids = map->GetLoadedGridCount();
            stat.creatures = objects.first;
            stat.gameobjects = objects.second;
            stat.players = map->GetPlayersCountExceptGMs();
            total.grids += stat.grids;
            total.creatures += stat.creatures;
            total.gameobjects += stat.gameobjects;
            total.players += stat.players;
            stats.push_back(stat);
        }
    }

    // #416 (7.3): value caches of the bots - counted by each bot on its own
    // thread (atomics), only summed here; the names of the largest bot.
    uint64 botValues = 0, evicted = 0;
    // Hotfix 8.1: more per-bot stores and bots whose UpdateAI stopped.
    uint64 whispers = 0, chatQueue = 0, packetQueue = 0, recorded = 0, createdObjects = 0;
    uint32 staleBots = 0;
    uint32 botValuesMax = 0, sharedValues = 0, botCount = 0;
    std::string topValues;
    for (auto const& [guid, bot] : bots)
    {
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (!ai)
            continue;
        uint32 const own = ai->GetOwnValueCount();
        evicted += ai->TakeEvictedValues();
        botValues += own;
        whispers += ai->GetWhisperCount();
        chatQueue += ai->GetChatQueueCount();
        packetQueue += ai->GetPacketQueueCount();
        recorded += ai->GetRecordedCount();
        createdObjects += ai->GetCreatedObjectCount();
        if (ai::quest_search::UpdateStale(ai->GetLastUpdateTime(), now))
            ++staleBots;
        ++botCount;
        sharedValues = std::max(sharedValues, ai->GetSharedValueCount());
        if (own > botValuesMax)
        {
            botValuesMax = own;
            topValues = ai->GetTopValueNames();
        }
    }

    sLog.outBasic("[MemStores] rss_kb=%llu maps=%u grids=%u creatures=%u gameobjects=%u players=%u"
        " route_cooldowns=%u parked_jobs=%u top_maps=\"%s\""
        " bots=%u bot_values=%llu bot_values_avg=%u bot_values_max=%u shared_values=%u top_values=\"%s\" values_evicted=%llu"
        " heap_kb=%llu inuse_kb=%llu free_kb=%llu arenas=%u items=%lld loots=%lld path_points=%lld"
        " corpses=%lld aura_holders=%lld dynobjects=%lld bot_whispers=%llu bot_chatq=%llu bot_packetq=%llu"
        " bot_recorded=%llu bot_objects=%llu stale_bots=%u",
        (unsigned long long)rssKb, uint32(stats.size()), total.grids, total.creatures, total.gameobjects, total.players,
        uint32(ai::stall_guard::RouteCooldowns().Size()), uint32(FutureDestinations::ParkedCount()),
        ai::mem_stores::TopMaps(stats, 5).c_str(),
        botCount, (unsigned long long)botValues, botCount ? uint32(botValues / botCount) : 0u, botValuesMax, sharedValues,
        topValues.c_str(), (unsigned long long)evicted,
        (unsigned long long)heapKb, (unsigned long long)inuseKb, (unsigned long long)freeKb, arenas,
        (long long)TrackedCount<Item>::Total(), (long long)TrackedCount<Loot>::Total(),
        (long long)TravelNodePath::StoredPathPoints(),
        (long long)TrackedCount<Corpse>::Total(), (long long)TrackedCount<SpellAuraHolder>::Total(),
        (long long)TrackedCount<DynamicObject>::Total(), (unsigned long long)whispers, (unsigned long long)chatQueue,
        (unsigned long long)packetQueue, (unsigned long long)recorded, (unsigned long long)createdObjects, staleBots);
}

// twow-repo#563: one [BotInbox] line per minute - packets for bots dropped past the inbox bound
// and the largest batch one bot handled in a single update. Both are reset per line.
void ReportBotInbox()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (now < lastReport + 60)
        return;
    lastReport = now;

    uint64 const dropped = ai::InboxDroppedTotal().exchange(0, std::memory_order_relaxed);
    uint64 const largest = ai::InboxLargestDrain().exchange(0, std::memory_order_relaxed);
    auto take = [](ai::InboxDropClass dropClass) { return (unsigned long long)ai::InboxDroppedByClass(dropClass).exchange(0, std::memory_order_relaxed); };
    unsigned long long const chat = take(ai::InboxDropChat);
    unsigned long long const spell = take(ai::InboxDropSpell);
    unsigned long long const knockback = take(ai::InboxDropKnockback);
    unsigned long long const handler = take(ai::InboxDropHandler);
    // twow-repo#563 (X3c): value writes for other bots - dropped past the bound, or with no bot AI at the target.
    unsigned long long const write = take(ai::InboxDropWrite);
    unsigned long long const writeGone = (unsigned long long)ai::ContextWriteTargetGone().exchange(0, std::memory_order_relaxed);
    sLog.outBasic("[BotInbox] dropped=%llu largest_batch=%llu capacity=%u drop_chat=%llu drop_spell=%llu drop_knockback=%llu drop_handler=%llu drop_write=%llu write_target_gone=%llu",
        (unsigned long long)dropped, (unsigned long long)largest, uint32(ai::BotPacketInboxCapacity),
        chat, spell, knockback, handler, write, writeGone);

    // twow-repo#587 Z4 (always on): the most often unreachable turn-in targets, top 5 per 5 minutes and top 10 per hour -
    // "entry:map:x:y:quest:count:bots". OB-30's live monitor takes the hourly line into the #541 summary.
    {
        static uint32 unreachableMinutes = 0;
        ++unreachableMinutes;
        if (unreachableMinutes % 5 == 0)
        {
            uint32 total = 0;
            auto const top = ai::unreachable_target::ShortWindow().TakeTop(5, total);
            if (total)
                sLog.outBasic("[UnreachableTop] window_min=5 total=%u targets=%u top=%s", total, uint32(top.size()),
                    ai::unreachable_target::Format(top).c_str());
        }
        if (unreachableMinutes % 60 == 0)
        {
            uint32 total = 0;
            auto const top = ai::unreachable_target::LongWindow().TakeTop(10, total);
            sLog.outBasic("[UnreachableTop] window_min=60 total=%u targets=%u top=%s", total, uint32(top.size()),
                top.empty() ? "-" : ai::unreachable_target::Format(top).c_str());
        }
    }

    // twow-repo#588 (AiPlayerbot.Buff.RealPlayerStyle): buff targets chosen in the last minute, per category.
    if (sPlayerbotAIConfig.buffRealPlayerStyle)
    {
        auto take = [](ai::buff_target::Category c) { return (unsigned long long)ai::buff_target::Counter(c).exchange(0, std::memory_order_relaxed); };
        unsigned long long const group = take(ai::buff_target::Category::Group);
        unsigned long long const guild = take(ai::buff_target::Category::Guild);
        unsigned long long const strangerBot = take(ai::buff_target::Category::StrangerBot);
        unsigned long long const strangerPlayer = take(ai::buff_target::Category::StrangerPlayer);
        unsigned long long const cooldown = take(ai::buff_target::Category::StrangerOnCooldown);
        sLog.outBasic("[BuffTargets] group=%llu guild=%llu stranger_bot=%llu stranger_player=%llu stranger_on_cooldown=%llu cooldown_s=%u",
            group, guild, strangerBot, strangerPlayer, cooldown, sPlayerbotAIConfig.buffStrangerCooldownSeconds);
    }

    // twow-repo#541 (AiPlayerbot.Perf.ActionBudget): expensive actions of the last minute.
    if (sPlayerbotAIConfig.perfActionBudget)
    {
        auto take = [](ai::action_budget::Count count) { return (unsigned long long)ai::action_budget::Counter(count).exchange(0, std::memory_order_relaxed); };
        unsigned long long const executed = take(ai::action_budget::Executed);
        unsigned long long const deferred = take(ai::action_budget::Deferred);
        unsigned long long const forced = take(ai::action_budget::Forced);
        sLog.outBasic("[ActionBudget] mode=%u executed=%llu deferred=%llu forced=%llu max_deferrals=%u",
            sPlayerbotAIConfig.perfActionBudget, executed, deferred, forced, ai::action_budget::MaxDeferrals);
        // twow-repo#591: travel target choices of bots without a target that skipped the budget, and the limit in use.
        unsigned long long const startExempted = take(ai::action_budget::StartExempted);
        if (sPlayerbotAIConfig.perfActionBudgetStartExempt || sPlayerbotAIConfig.perfActionBudgetLimit)
            sLog.outBasic("[ActionBudget] start_exempt=%llu limit=%u", startExempted,
                ai::action_budget::LimitFor(sPlayerbotAIConfig.perfActionBudget, sPlayerbotAIConfig.perfActionBudgetLimit));
    }

    // twow-repo#541 A39 (AiPlayerbot.DeathLoop.Escape): evacuations of the last minute.
    if (sPlayerbotAIConfig.deathLoopEscape)
    {
        auto takeEscape = [](ai::death_loop::EscapeCount count) { return (unsigned long long)ai::death_loop::EscapeCounter(count).exchange(0, std::memory_order_relaxed); };
        unsigned long long const detected = takeEscape(ai::death_loop::EscapeDetected);
        unsigned long long const escaped = takeEscape(ai::death_loop::EscapeEscaped);
        unsigned long long const rebound = takeEscape(ai::death_loop::EscapeRebound);
        unsigned long long const repeat = takeEscape(ai::death_loop::EscapeRepeat);
        unsigned long long const hostileDeaths = takeEscape(ai::death_loop::EscapeHostileDeath);
        sLog.outBasic("[DeathLoop] state=minute detected=%llu escaped=%llu rebound=%llu repeat_after_escape=%llu hostile_death=%llu",
            detected, escaped, rebound, repeat, hostileDeaths);
    }

    // twow-repo#563 (X3b): reads of other bots' published targets and group conditions in the last minute.
    if (sPlayerbotAIConfig.x3bPublishedTargets || sPlayerbotAIConfig.x3bPublishedConditions)
    {
        auto takeRead = [](ai::x3b::SnapshotRead read) { return (unsigned long long)ai::x3b::SnapshotCount(read).exchange(0, std::memory_order_relaxed); };
        auto takeCondition = [](ai::x3b::ConditionRead read) { return (unsigned long long)ai::x3b::ConditionCount(read).exchange(0, std::memory_order_relaxed); };
        unsigned long long const hit = takeRead(ai::x3b::SnapshotHit);
        unsigned long long const stale = takeRead(ai::x3b::SnapshotStale);
        unsigned long long const missing = takeRead(ai::x3b::SnapshotMissing);
        unsigned long long const condHit = takeCondition(ai::x3b::ConditionHit);
        unsigned long long const condMiss = takeCondition(ai::x3b::ConditionMiss);
        unsigned long long const condDropped = takeCondition(ai::x3b::ConditionDropped);
        unsigned long long const condEvaluated = takeCondition(ai::x3b::ConditionEvaluated);
        sLog.outBasic("[X3bSnapshot] hit=%llu stale=%llu missing=%llu max_age_ms=%u cond_hit=%llu cond_miss=%llu cond_dropped=%llu cond_evaluated=%llu",
            hit, stale, missing, uint32(ai::x3b::MaxAgeMs), condHit, condMiss, condDropped, condEvaluated);
    }

    // twow-repo#541 (AiPlayerbot.PacketTrace): packets of the last minute per map bucket. Per class
    // "read/ignored/real" counts; kib_* = bytes per receiver. ignored = built for a bot that never reads it.
    if (sPlayerbotAIConfig.packetTrace)
    {
        namespace pt = ai::packet_trace;
        pt::Totals& totals = pt::Global();
        char const* const bucketNames[pt::MapBuckets] = { "eastern", "kalimdor", "other" };
        char const* const classNames[pt::Classes] = { "move", "update", "spell", "combat", "chat", "other" };
        for (int bucket = 0; bucket < pt::MapBuckets; ++bucket)
        {
            std::ostringstream classes;
            unsigned long long kib[pt::Receivers] = {};
            unsigned long long ignoredBytes[pt::Classes] = {};
            for (int cls = 0; cls < pt::Classes; ++cls)
            {
                unsigned long long counts[pt::Receivers];
                for (int receiver = 0; receiver < pt::Receivers; ++receiver)
                {
                    counts[receiver] = totals.count[receiver][bucket][cls].exchange(0, std::memory_order_relaxed);
                    unsigned long long const bytes = totals.bytes[receiver][bucket][cls].exchange(0, std::memory_order_relaxed);
                    kib[receiver] += bytes;
                    if (receiver == pt::BotIgnored)
                        ignoredBytes[cls] = bytes;
                }
                classes << " " << classNames[cls] << "=" << counts[pt::BotRead] << "/" << counts[pt::BotIgnored] << "/" << counts[pt::Real];
            }
            // OB-00 11.10.2026: bytes per class of the packets built for bots and never read (KiB).
            classes << " kib_ignored_by_class=";
            for (int cls = 0; cls < pt::Classes; ++cls)
                classes << (cls ? "," : "") << classNames[cls] << ":" << ignoredBytes[cls] / 1024;
            sLog.outBasic("[BotPackets] map=%s kib_bot_read=%llu kib_bot_ignored=%llu kib_real=%llu%s",
                bucketNames[bucket], kib[pt::BotRead] / 1024, kib[pt::BotIgnored] / 1024, kib[pt::Real] / 1024, classes.str().c_str());
        }
        // OB-00 11.10.2026: the five opcodes behind most of the "other" packets built for bots and never read.
        std::ostringstream top;
        for (auto const& [opcode, count] : pt::TakeTopOtherIgnored(totals, 5))
            top << " " << LookupOpcodeName(uint16(opcode)) << "=" << count;
        sLog.outBasic("[BotPackets] other_ignored_top5%s", top.str().c_str());
        unsigned long long const samples = totals.inboxSamples.exchange(0, std::memory_order_relaxed);
        unsigned long long const sampledNs = totals.inboxSampledNs.exchange(0, std::memory_order_relaxed);
        sLog.outBasic("[BotPackets] inbox_copy_samples=%llu inbox_copy_avg_ns=%llu sample_every=%u flush_every=%u",
            samples, samples ? sampledNs / samples : 0ull, pt::TimeSampleEvery, pt::FlushEvery);
    }

    // twow-repo#541 A47 (AiPlayerbot.BotSave.IntervalSeconds): own bot saves of the last minute and their cost.
    if (sPlayerbotAIConfig.botSaveIntervalSeconds)
    {
        auto take = [](ai::bot_save::Count count) { return (unsigned long long)ai::bot_save::Counter(count).exchange(0, std::memory_order_relaxed); };
        unsigned long long const saves = take(ai::bot_save::Saves);
        unsigned long long const deferred = take(ai::bot_save::Deferred);
        unsigned long long const micros = take(ai::bot_save::SaveMicros);
        sLog.outBasic("[BotSave] interval_s=%u max_per_tick=%u saves=%llu deferred=%llu avg_us=%llu total_ms=%llu",
            sPlayerbotAIConfig.botSaveIntervalSeconds, sPlayerbotAIConfig.botSaveMaxPerTick, saves, deferred,
            saves ? micros / saves : 0ull, micros / 1000);
    }
}

// twow-repo#541 (spikes per region update, AiPlayerbot.BotUpdateTrace): one [BotUpdate] line per minute
// with the distribution of one bot's UpdateAI wall time over all map threads, and one [BotUpdateSlow]
// line per slowest call (bot, map, combat, last action). Nothing is timed or written when off.
void ReportBotUpdate()
{
    static bool tracing = false;
    static uint32 lastReport = 0;
    if (!sPlayerbotAIConfig.botUpdateTrace)
    {
        tracing = false;
        return;
    }
    uint32 const now = uint32(time(nullptr));
    if (!tracing)
    {
        // Switched on (start or config reload): what an earlier period left is dropped, the first line comes after a
        // full minute (same pattern as ReportShortcut; twow-repo#601 review 18.10.).
        tracing = true;
        lastReport = now;
        ai::bot_update::Global().Take();
        ai::bot_update::Prevalidate().Take();
        return;
    }
    if (now < lastReport + 60)
        return;
    lastReport = now;

    ai::bot_update::Snapshot const s = ai::bot_update::Global().Take();
    ai::bot_update::CountedSnapshot const pv = ai::bot_update::Prevalidate().Take();

    // twow-repo#601 S1 (OB-10 18.10.): every PreValidateTurnIns call of the minute - its time and its work (open turn-ins
    // checked) and result (validated). Only with AiPlayerbot.QuestFirstProgression.FailureClasses (the only caller).
    if (sPlayerbotAIConfig.questFirstProgressionFailureClasses)
        sLog.outBasic("[PreValidate] calls=%llu sum_us=%llu p50_us=%llu p99_us=%llu max_us=%llu open_sum=%llu ok_sum=%llu",
            (unsigned long long)pv.time.calls, (unsigned long long)pv.time.totalUs,
            (unsigned long long)ai::bot_update::PercentileUs(pv.time, 0.50),
            (unsigned long long)ai::bot_update::PercentileUs(pv.time, 0.99), (unsigned long long)pv.time.maxUs,
            (unsigned long long)pv.openSum, (unsigned long long)pv.okSum);

    sLog.outBasic("[BotUpdate] calls=%llu avg_us=%llu p50_us=%llu p90_us=%llu p99_us=%llu p999_us=%llu max_us=%llu over10ms=%llu over20ms=%llu",
        (unsigned long long)s.calls, (unsigned long long)(s.calls ? s.totalUs / s.calls : 0),
        (unsigned long long)ai::bot_update::PercentileUs(s, 0.50), (unsigned long long)ai::bot_update::PercentileUs(s, 0.90),
        (unsigned long long)ai::bot_update::PercentileUs(s, 0.99), (unsigned long long)ai::bot_update::PercentileUs(s, 0.999),
        (unsigned long long)s.maxUs,
        (unsigned long long)ai::bot_update::CallsAtLeastUs(s, 10000), (unsigned long long)ai::bot_update::CallsAtLeastUs(s, 20000));
    for (ai::bot_update::Slow const& slow : s.slowest)
        sLog.outBasic("[BotUpdateSlow] us=%llu %s", (unsigned long long)slow.us, slow.who.c_str());
}

// twow-repo#541 (audit A17, AiPlayerbot.Perf.CombatIdleYield): one [CombatIdle] line per minute - stretched combat-idle
// waits (waits granted, not passes saved) and the ones the wake check ended early because the auto attack changed.
// Waits ended or overridden by other existing resets are not counted as woken. Nothing is written when off.
void ReportCombatIdle()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfCombatIdleYield || now < lastReport + 60)
        return;
    lastReport = now;
    sLog.outBasic("[CombatIdle] stretched=%llu woken=%llu",
        (unsigned long long)ai::combat_idle::stretched.exchange(0, std::memory_order_relaxed),
        (unsigned long long)ai::combat_idle::woken.exchange(0, std::memory_order_relaxed));
}

// twow-repo#541 (audit A21, AiPlayerbot.Perf.LazyEngineInit): one [LazyEngineInit] line per minute. deferred = engine
// rebuilds put off because the engine was not running, caught = rebuilds run later at the start of Engine::DoNextAction
// (Reset(false) path, mid-walk engine change), saved = deferred - caught (a window can catch rebuilds deferred in an
// earlier one, so saved is floored at 0). World thread only, same pattern as ReportBotUpdate. Nothing is written when off.
void ReportLazyEngineInit()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfLazyEngineInit || now < lastReport + 60)
        return;
    lastReport = now;

    uint64 const deferred = ai::lazy_engine_init::deferred.exchange(0, std::memory_order_relaxed);
    uint64 const caught = ai::lazy_engine_init::caught.exchange(0, std::memory_order_relaxed);
    sLog.outBasic("[LazyEngineInit] deferred=%llu caught=%llu saved=%llu",
        (unsigned long long)deferred, (unsigned long long)caught,
        (unsigned long long)(deferred > caught ? deferred - caught : 0));
}

// twow-repo#541 (L1, AiPlayerbot.Perf.PartialPathReuse): one [PathReuse] line per minute. hits = path rebuilds skipped
// because the path last built for the same destination was used again, misses = rebuilds that ran with the switch on
// (both only counted after the existing end-near-destination reuse had missed). World thread only, same pattern as
// ReportBotUpdate. Nothing is counted or written when off.
void ReportPathReuse()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfPartialPathReuse || now < lastReport + 60)
        return;
    lastReport = now;

    uint64 const hits = ai::path_reuse::hits.exchange(0, std::memory_order_relaxed);
    uint64 const misses = ai::path_reuse::misses.exchange(0, std::memory_order_relaxed);
    sLog.outBasic("[PathReuse] hits=%llu misses=%llu max_age_ms=%u",
        (unsigned long long)hits, (unsigned long long)misses, sPlayerbotAIConfig.perfPartialPathReuseMs);
}

// twow-repo#541 (L1b, AiPlayerbot.Perf.RouteFailCooldownSeconds): one [RouteFail] line per minute. blocks = node route
// searches without a route that set a cooldown, skips = searches skipped by a route cooldown (failure or budget).
// World thread only, same pattern as ReportPathReuse. Nothing is counted or written when off.
void ReportRouteFail()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfRouteFailCooldownSeconds || now < lastReport + 60)
        return;
    lastReport = now;

    uint64 const blocks = ai::stall_guard::routeFailBlocks.exchange(0, std::memory_order_relaxed);
    uint64 const skips = ai::stall_guard::routeCooldownSkips.exchange(0, std::memory_order_relaxed);
    // twow-repo#592 (AiPlayerbot.Perf.RouteFailEarlySkip): early_skips / early_wrong only with that switch on; off, the line
    // stays as it was.
    if (!sPlayerbotAIConfig.perfRouteFailEarlySkip)
        sLog.outBasic("[RouteFail] blocks=%llu skips=%llu cooldown_seconds=%u",
            (unsigned long long)blocks, (unsigned long long)skips, sPlayerbotAIConfig.perfRouteFailCooldownSeconds);
    else
        sLog.outBasic("[RouteFail] blocks=%llu skips=%llu cooldown_seconds=%u early_skips=%llu early_wrong=%llu",
            (unsigned long long)blocks, (unsigned long long)skips, sPlayerbotAIConfig.perfRouteFailCooldownSeconds,
            (unsigned long long)ai::stall_guard::routeEarlySkips.exchange(0, std::memory_order_relaxed),
            (unsigned long long)ai::stall_guard::routeEarlyWrong.exchange(0, std::memory_order_relaxed));
}

// twow-repo#592 (PATH-02, AiPlayerbot.Perf.RouteTimeTrace): one [RouteTime] line per minute - every node route build
// (TravelNodeMap::getFullPath) by phase, the build-time counters and the DispatchMovement path search (dispatch_*).
// World thread only, same pattern as ReportRouteFail. Nothing is timed, counted or written when off.
void ReportRouteTime()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfRouteTimeTrace || now < lastReport + 60)
        return;
    lastReport = now;

    sLog.outBasic("%s", ai::route_time::FormatLine(ai::route_time::Global().Take()).c_str());
}

// twow-repo#592 (PATH-02, AiPlayerbot.Perf.RoutePrecheck): one [RoutePrecheck] line per minute - target checks (and their
// hits per direct-attempt class), start and direct checks, exemptions, wrong predictions and the work left out. World
// thread only, same pattern as ReportRouteFail. Nothing is checked, counted or written when off.
void ReportRoutePrecheck()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!sPlayerbotAIConfig.perfRoutePrecheck || now < lastReport + 60)
        return;
    lastReport = now;

    sLog.outBasic("%s", ai::route_precheck::FormatLine(sPlayerbotAIConfig.perfRoutePrecheck, ai::route_precheck::Global().Take()).c_str());
}

// twow-repo#594 / P3 (AiPlayerbot.DirectTargets.Enable / TraceInactive): one [DirectTarget] state=summary line per minute.
// fresh_rejections = rejected quest routes of fresh bots, fresh_backoffs = of them with the short backoff, rescue_skips =
// of them that a Z1b rescue took, fresh_grind = grind targets fresh bots chose; inactive = the IsActive reasons (P3-0).
// World thread only, same pattern as ReportRouteFail. Nothing is written with both switches off.
void ReportDirectTargets()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if ((!sPlayerbotAIConfig.directTargetsEnabled && !sPlayerbotAIConfig.directTargetsTraceInactive) || now < lastReport + 60)
        return;
    lastReport = now;

    ai::quest_search::DirectTargetCounters& c = ai::quest_search::DirectTargetStats();
    ai::quest_search::RejectCounts reasons;
    for (uint32 reason = 0; reason < ai::quest_search::RejectCounts::InactiveReasons; ++reason)
        reasons.notActiveReason[reason] = c.inactive[reason].exchange(0, std::memory_order_relaxed);
    sLog.outBasic("[DirectTarget] state=summary enabled=%u fresh_rejections=%u fresh_backoffs=%u rescue_skips=%u fresh_grind=%u "
        "fresh_max_level=%u backoff_seconds=%u grind_failures=%u%s", sPlayerbotAIConfig.directTargetsEnabled ? 1u : 0u,
        c.freshRejections.exchange(0, std::memory_order_relaxed), c.freshBackoffs.exchange(0, std::memory_order_relaxed),
        c.rescueSkips.exchange(0, std::memory_order_relaxed), c.freshGrind.exchange(0, std::memory_order_relaxed),
        sPlayerbotAIConfig.directTargetsFreshMaxLevel, sPlayerbotAIConfig.directTargetsFreshBackoffSeconds,
        sPlayerbotAIConfig.directTargetsFreshGrindFailures, reasons.FormatInactiveReasons().c_str());
}

// twow-repo#601 GrindMode (AiPlayerbot.QuestFirstProgression.GrindMode = 2): one "[QuestIdle] minute fallback= money=
// opportunistic= no_target=" line per minute - the grind decisions turned down on the map threads since the last line
// (atomics in PlayerbotAI.cpp). World thread only, same pattern as ReportDirectTargets. Nothing with GrindMode 0 / 1.
void ReportQuestIdle()
{
    if (!ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))
        return;   // GrindMode 0 / 1: not even the clock is read

    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (now < lastReport + 60)
        return;
    lastReport = now;

    sLog.outBasic("%s", PlayerbotAI::TakeQuestIdleMinuteLine().c_str());
}

// twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace): [RotCast] once per minute, [RotCastTotal] once per hour
// (RotCastTrace.cpp). World thread only. The first call after the switch went on clears and starts a window.
void ReportRotCast()
{
    static bool tracing = false;
    if (!sPlayerbotAIConfig.rotationCastTrace)
    {
        tracing = false;
        return;   // off: not even the clock is read
    }

    ai::rot_cast_rt::Report(!tracing, uint32(time(nullptr)));
    tracing = true;
}

// twow-repo#601 S3a (AiPlayerbot.QuestFeasibility.Enable): one "[QuestFeasibility] minute skips= drops= giver_inactive="
// line per minute - accept / item skips, infeasible drops and inactive giver relations counted on the map threads since
// the last line (atomics in PlayerbotAI.cpp). World thread only, same pattern as ReportQuestIdle; the first line comes
// after a full minute. Nothing with Enable 0.
void ReportQuestFeasibility()
{
    if (!sPlayerbotAIConfig.questFeasibilityEnable)
        return;   // Enable 0: not even the clock is read

    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (!lastReport)
    {
        lastReport = now;
        return;
    }
    if (now < lastReport + 60)
        return;
    lastReport = now;

    sLog.outBasic("%s", PlayerbotAI::TakeQuestFeasibilityMinuteLine().c_str());
}

// twow-repo#592 (PATH-02): the bot side of pathfinding without a Unit, no switch - every counted case crashed before
// #592 or does not happen with today's data. At most once a minute, and only while one of the rare counters is above 0
// (until then no clock read): "[PathUnitlessBot] reject=bot_transport n=" (a map-mode PathFinder met a bot on a
// transport, getPathStepFrom returned no path without calculate) and "[PathUnitlessBot] route_incomplete= mode_map="
// (route steps moved to the node instead of a rebuild at runtime; map-mode PathFinders getPathFromPath built since the
// last line - Unit mode is not counted, it is the hot path). World thread only, same pattern as ReportRouteFail.
void ReportPathUnitlessBot()
{
    using namespace ai::path_unitless_bot;
    if (!transportRejects.load(std::memory_order_relaxed) && !routeIncomplete.load(std::memory_order_relaxed) &&
        !modeMap.load(std::memory_order_relaxed))
        return;

    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if (now < lastReport + 60)
        return;
    lastReport = now;

    uint64 const rejects = transportRejects.exchange(0, std::memory_order_relaxed);
    uint64 const incomplete = routeIncomplete.exchange(0, std::memory_order_relaxed);
    uint64 const mapMode = modeMap.exchange(0, std::memory_order_relaxed);
    if (rejects)
        sLog.outBasic("[PathUnitlessBot] reject=bot_transport n=%llu", (unsigned long long)rejects);
    if (incomplete || mapMode)
        sLog.outBasic("[PathUnitlessBot] route_incomplete=%llu mode_map=%llu",
            (unsigned long long)incomplete, (unsigned long long)mapMode);
}

// twow-repo#595 P4 S0: steady milliseconds for the shortcut snapshot (build time, staleness).
uint64 ShortcutNowMs()
{
    return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// twow-repo#595 P4 S0: one snapshot entry for a real player / watcher and one for its camera body if that differs.
// World thread, map threads idle (after sMapMgr.Update).
void AddShortcutEntries(ai::shortcut::Snapshot& snapshot, Player* player, bool watcher, float reactDistance)
{
    ai::shortcut::Entry entry;
    entry.map = player->GetMapId();
    player->GetZoneAndAreaId(entry.zone, entry.area);
    entry.x = player->GetPositionX();
    entry.y = player->GetPositionY();
    Map* map = player->GetMap();
    entry.defaultRadius = ai::shortcut::DefaultRadius(map ? map->GetVisibilityDistance() : 0.0f, reactDistance);
    entry.watcher = watcher;
    snapshot.entries.push_back(entry);

    WorldObject* body = player->GetCamera().GetBody();
    if (body && body != player && body->IsInWorld())
    {
        ai::shortcut::Entry view = entry;
        view.map = body->GetMapId();
        body->GetZoneAndAreaId(view.zone, view.area);
        view.x = body->GetPositionX();
        view.y = body->GetPositionY();
        snapshot.entries.push_back(view);
    }
}
}

// twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.*): where the real players are, for ShortcutNear. Called only from
// UpdateAIInternal: the world thread after sMapMgr.Update, the map threads are idle, so reading the players is safe.
// O(real players + watchers), at most once a second, and nothing at all while Trace = 0 and NearScope = 0.
void RandomPlayerbotMgr::BuildShortcutSnapshot()
{
    if (!sPlayerbotAIConfig.shortcutTrace && !sPlayerbotAIConfig.shortcutNearScope)
    {
        if (shortcutLastBuildMs)
        {
            // Switched off by a config reload: drop the snapshot, ShortcutNear answers Unknown again.
            shortcutLastBuildMs = 0;
            std::shared_ptr<ai::shortcut::Snapshot const> dropped;
            {
                std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);
                shortcutSnapshot.swap(dropped);
            }
        }
        return;
    }

    uint64 const nowMs = ShortcutNowMs();
    if (!ai::shortcut::BuildDue(shortcutLastBuildMs, nowMs))
        return;
    shortcutLastBuildMs = nowMs;

    std::shared_ptr<ai::shortcut::Snapshot> snapshot = std::make_shared<ai::shortcut::Snapshot>();
    snapshot->builtAtMs = nowMs;
    float const reactDistance = sPlayerbotAIConfig.reactDistance;

    // The real players (OnPlayerLogin / OnPlayerLogout keep this map, the world thread reads it here). GMs only while
    // visible, as PlayerbotAI::HasPlayerNearby; alt bots (a bot AI without a real session) are not players.
    for (auto const& [guid, player] : players)
    {
        if (!player || !player->IsInWorld() || !IsRealPlayer(player))
            continue;
        if (!player->IsGameMaster() || player->isGMVisible())
        {
            AddShortcutEntries(*snapshot, player, false, reactDistance);
            ++snapshot->players;
        }
    }

    // TEST ONLY (AiPlayerbot.Shortcut.TestWatcherGuids, at most 64): these characters count as real players.
    for (uint32 guid : sPlayerbotAIConfig.shortcutTestWatcherGuids)
    {
        Player* watcher = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
        if (!watcher || !watcher->IsInWorld())
            continue;
        AddShortcutEntries(*snapshot, watcher, true, reactDistance);
        ++snapshot->watchers;
    }

    std::shared_ptr<ai::shortcut::Snapshot const> published = std::move(snapshot);
    {
        std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);
        shortcutSnapshot.swap(published);
    }
    // the previous snapshot (now in published) is freed here, outside the lock
}

std::shared_ptr<ai::shortcut::Snapshot const> RandomPlayerbotMgr::ShortcutSnapshot()
{
    std::lock_guard<std::mutex> lock(shortcutSnapshotMutex);
    return shortcutSnapshot;
}

ai::shortcut::Near RandomPlayerbotMgr::ShortcutNear(uint32 mapId, float x, float y, uint32 zoneId)
{
    std::shared_ptr<ai::shortcut::Snapshot const> const snapshot = ShortcutSnapshot();
    return ai::shortcut::Evaluate(snapshot.get(), ShortcutNowMs(), sPlayerbotAIConfig.shortcutNearScope,
        sPlayerbotAIConfig.shortcutNearRadius, mapId, x, y, [zoneId]() { return zoneId; });
}

// twow-repo#595 P4 S0 (AiPlayerbot.Shortcut.Trace): measurement only, on the bot's map thread. Reads the snapshot copy
// and the bot (owned by this thread); never the real-player map. The zone is looked up only when scope 2 meets a
// snapshot entry on that map outside the radius: the bot's cached zone, the target's zone on the bot's own terrain
// (another map: zone 0 = unknown there).
void RandomPlayerbotMgr::CountShortcut(ai::shortcut::Mech mech, Player* bot, uint32 toMapId, float toX, float toY, float toZ)
{
    if (!sPlayerbotAIConfig.shortcutTrace || !bot)
        return;

    std::shared_ptr<ai::shortcut::Snapshot const> const snapshot = ShortcutSnapshot();
    uint64 const nowMs = ShortcutNowMs();
    uint32 const scope = ai::shortcut::MeasureScope(sPlayerbotAIConfig.shortcutNearScope);
    float const radius = sPlayerbotAIConfig.shortcutNearRadius;

    ai::shortcut::Near const source = ai::shortcut::Evaluate(snapshot.get(), nowMs, scope, radius,
        bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), [bot]() { return bot->GetCachedZoneId(); });
    ai::shortcut::Near const destination = ai::shortcut::Evaluate(snapshot.get(), nowMs, scope, radius,
        toMapId, toX, toY, [bot, toMapId, toX, toY, toZ]() -> uint32
        {
            TerrainInfo const* terrain = toMapId == bot->GetMapId() ? bot->GetTerrain() : nullptr;
            return terrain ? terrain->GetZoneId(toX, toY, toZ) : 0;
        });
    ai::shortcut::Record(ai::shortcut::SharedCounters(), mech, source, destination);
}

// twow-repo#595 P4 S0: [Shortcut] once a minute (AiPlayerbot.Shortcut.Trace), world thread.
void RandomPlayerbotMgr::ReportShortcut()
{
    static bool tracing = false;
    static uint32 lastReport = 0;
    if (!sPlayerbotAIConfig.shortcutTrace)
    {
        tracing = false;
        return;
    }

    uint32 const now = uint32(time(nullptr));
    ai::shortcut::Counters& counters = ai::shortcut::SharedCounters();
    if (!tracing)
    {
        // Trace switched on (start or config reload): counts left over from an earlier Trace = 1 period are dropped,
        // the first line comes after a full minute.
        tracing = true;
        lastReport = now;
        for (uint32 i = 0; i < uint32(ai::shortcut::Mech::Count); ++i)
            ai::shortcut::Drain(counters.mech[i]);
        return;
    }
    if (now < lastReport + 60)
        return;
    lastReport = now;

    // stale=1: the snapshot is older than StaleMs (every answer Unknown), e.g. RandomBotUpdateInterval >= about 4 s.
    std::shared_ptr<ai::shortcut::Snapshot const> const snapshot = ShortcutSnapshot();
    uint64 const nowMs = ShortcutNowMs();
    sLog.outBasic("[Shortcut] state=snapshot players=%u watchers=%u entries=%u age_ms=%llu stale=%u scope=%u radius=%.0f",
        snapshot ? snapshot->players : 0u, snapshot ? snapshot->watchers : 0u,
        snapshot ? uint32(snapshot->entries.size()) : 0u,
        (unsigned long long)(snapshot && nowMs >= snapshot->builtAtMs ? nowMs - snapshot->builtAtMs : 0),
        snapshot && ai::shortcut::Fresh(snapshot->builtAtMs, nowMs) ? 0u : 1u,
        ai::shortcut::MeasureScope(sPlayerbotAIConfig.shortcutNearScope), sPlayerbotAIConfig.shortcutNearRadius);

    for (uint32 i = 0; i < uint32(ai::shortcut::Mech::Count); ++i)
    {
        ai::shortcut::MechTotals const t = ai::shortcut::Drain(counters.mech[i]);
        if (t.nearAny || t.notNear || t.unknown)
            sLog.outBasic("[Shortcut] mech=%s teleports=%u near=%u notnear=%u unknown=%u near_src=%u near_dst=%u",
                ai::shortcut::MechName(ai::shortcut::Mech(i)), t.nearAny + t.notNear + t.unknown, t.nearAny, t.notNear,
                t.unknown, t.nearSrc, t.nearDst);
    }
}

// twow-repo#541 ([WorldBots]): microseconds between two clock reads. A free function, not a lambda in
// UpdateAIInternal: perfmon_init_reachable reads the first `return` of that body as an early return.
using WorldBotsClock = std::chrono::steady_clock;
static uint64 usSince(WorldBotsClock::time_point from, WorldBotsClock::time_point to)
{
    return uint64(std::chrono::duration_cast<std::chrono::microseconds>(to - from).count());
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)
{
    // twow-repo#541 ([WorldBots], AiPlayerbot.WorldBotsTrace): timing of this pass in the world thread.
    WorldBotsClock::time_point const passStart = WorldBotsClock::now();
    uint32 const acksBefore = TeleportAcks();

    ReportMemStores(GetAllBots());
    ProcessQuestRescues();
    BuildShortcutSnapshot(); // twow-repo#595 P4 S0: real-player snapshot, only with AiPlayerbot.Shortcut.Trace / NearScope
    ReportShortcut();     // twow-repo#595 P4 S0: [Shortcut] once per minute (AiPlayerbot.Shortcut.Trace)
    ProcessParkedBots();  // twow-repo#541/#551 (rndbot park), no-op without parked bots
    ReportBotInbox();     // twow-repo#563: [BotInbox] once per minute
    ReportBotUpdate();    // twow-repo#541: [BotUpdate] once per minute (AiPlayerbot.BotUpdateTrace)
    ReportParkSleep();    // twow-repo#541: core switches + [ParkSleep] once per minute (AiPlayerbot.Park.SleepGrid)
    ReportCombatIdle();   // twow-repo#541 (audit A17): [CombatIdle] once per minute (AiPlayerbot.Perf.CombatIdleYield)
    ReportLazyEngineInit(); // twow-repo#541 (audit A21): [LazyEngineInit] once per minute (AiPlayerbot.Perf.LazyEngineInit)
    ReportPathReuse();    // twow-repo#541 (L1): [PathReuse] once per minute (AiPlayerbot.Perf.PartialPathReuse)
    ReportRouteFail();    // twow-repo#541 (L1b): [RouteFail] once per minute (AiPlayerbot.Perf.RouteFailCooldownSeconds)
    ReportRouteTime();    // twow-repo#592 (PATH-02): [RouteTime] once per minute (AiPlayerbot.Perf.RouteTimeTrace)
    ReportRoutePrecheck(); // twow-repo#592 (PATH-02): [RoutePrecheck] once per minute (AiPlayerbot.Perf.RoutePrecheck)
    ReportDirectTargets(); // twow-repo#594 / P3: [DirectTarget] state=summary once per minute (AiPlayerbot.DirectTargets.*)
    ReportQuestIdle();    // twow-repo#601 GrindMode: [QuestIdle] minute once per minute (AiPlayerbot.QuestFirstProgression.GrindMode = 2)
    ReportRotCast();      // twow-repo#602 ROT-01 D0: [RotCast] once per minute (AiPlayerbot.Rotation.CastTrace)
    ReportQuestFeasibility(); // twow-repo#601 S3a: [QuestFeasibility] minute once per minute (AiPlayerbot.QuestFeasibility.Enable)
    ReportPathUnitlessBot(); // twow-repo#592 (PATH-02): [PathUnitlessBot] at most once per minute, only with rare counts

#ifdef MEMORY_MONITOR
    sMemoryMonitor.Print();
    sMemoryMonitor.LogCount(sConfig.GetStringDefault("LogsDir") + "/" + "memory.csv");
#endif

    // tick random bots' sessions so
    // teleport ACKs (HandleTeleportAck) and queued packets get processed.
    // See PlayerbotMgr::UpdateAIInternal for the rationale — same call,
    // same purpose, applied to the random-bot pool.
    WorldBotsClock::time_point const sessionsStart = WorldBotsClock::now();
    UpdateSessions(elapsed);
    WorldBotsClock::time_point const sessionsEnd = WorldBotsClock::now();

    // Register the performance monitor's per-map buckets.
    //
    // This must stay ABOVE every early return in this function, and it did not
    // use to be: the loop sat near the bottom of the legacy random-roster path,
    // behind the randomBotAutologin/enabled gate below and behind both
    // `return`s of the persistentActiveRosterEnabled branch. Init() is the ONLY
    // writer of PerformanceMonitor::mapsData, and start() returns nullptr for an
    // unregistered (mapId, instanceId) -- so on any server that runs a
    // persistent roster, or with autologin off, mapsData stayed empty forever,
    // every start() handed back nullptr, nothing was ever timed, and
    // `.perfmon tick` printed nothing while `.perfmon toggle` cheerfully
    // reported "Performance monitor enabled". The whole instrument was inert.
    //
    // Init() itself is a no-op unless perfMonEnabled, so the cost when the
    // monitor is off is one bool test plus a walk of the loaded-map list, once
    // per random-bot update interval.
    //
    // It also has to run on THIS thread. mapsData is a std::map with no lock
    // (see PerformanceMonitor.h); the design is that the world thread creates
    // the (mapId, instanceId) buckets here and bot threads only ever mutate the
    // sub-map they own. Creating buckets lazily inside start() would look
    // simpler and would corrupt the top-level map.
    for (auto& [mapId, map] : sMapMgr.Maps())
        sPerformanceMonitor.Init(map->GetId(), map->GetInstanceId());

    // The map-less bucket. PlayerbotAIBase::UpdateAI times "FullTick", and every
    // PERF_MON_RNDBOT probe in PlayerbotFactory/RandomPlayerbotMgr calls the
    // start() overload that defaults mapId and instanceId to 0. Those land in
    // mapsData[0][0], which the loop above only creates when map 0 (Eastern
    // Kingdoms) happens to be instantiated -- and PrintStats(perTick) divides by
    // the FullTick count, so without this bucket the per-tick report is either
    // absent or a division by zero.
    sPerformanceMonitor.Init(0, 0);

    if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
        return;

    if (!playersLevel)
        playersLevel = sPlayerbotAIConfig.syncLevelNoPlayer;

    if (sPlayerbotAIConfig.persistentActiveRosterEnabled)
    {
        if (!persistentRosterInitialized)
            InitializePersistentRoster();
        if (!PersistentRosterAdmissionOpen())
        {
            SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);
            PlayerbotHolder::UpdateAIInternal(elapsed, minimal);
            return;
        }

        ScaleBotActivity();
        std::list<uint32> desiredBots = GetBots();
        if (!LoginWaves().built)
            BuildRosterLoginWaves(persistentRoster->Desired(), [this](uint32 guid) { return IsPersistentRosterMember(guid); });
        uint32 maxLogins = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;
        uint32 const loginsAllowed = maxLogins;
        WorldBotsClock::time_point const processStart = WorldBotsClock::now();
        for (uint32 bot : desiredBots)
        {
            if (GetPlayerBot(bot))
                ProcessBot(bot);
            else if (maxLogins && sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") < 10 * IN_MILLISECONDS)
            {
                // #391: after an L1 reset, flagged bots log in wave by wave.
                if (!RosterLoginWaveOpen(bot))
                    continue;
                if (ProcessBot(bot))
                    --maxLogins;
            }
        }
        WorldBotsClock::time_point const processEnd = WorldBotsClock::now();
        SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);
        LoginFreeBots();
        // twow-repo#517 stage a: roster bots fill a battleground a real player queues for, chosen and queued by this
        // manager. The legacy BG queue count stays off on this path, so BgPlayers and NeedBots stay 0 and BGJoinAction
        // never queues a roster bot on its own (no overshoot). BgBots can wrap below 0 when a fill bot gets TIME_REMOVE
        // (BGStatusAction decrements it), but nothing reads it while BgPlayers is 0. Stage b runs in the same pass: the
        // pair option and slot release (SlotRelease) need no PlayerFill, the bot-only cleanup needs PlayerFill and
        // BotOnlyCleanup (CheckBgFill). Stage c (Announce: announced bot-own battlegrounds) needs neither PlayerFill nor
        // SlotRelease, and opens the pass only with its full gate BgOwnEnabled (Enable && Announce && IntervalMinutes &&
        // MaxConcurrent): Announce alone with IntervalMinutes or MaxConcurrent 0 does no work at all (C1).
        if (sPlayerbotAIConfig.bgEnable && (sPlayerbotAIConfig.bgPlayerFill || sPlayerbotAIConfig.bgSlotRelease || BgOwnEnabled()))
            CheckBgFill();
        PlayerbotHolder::UpdateAIInternal(elapsed, minimal);
        if (sPlayerbotAIConfig.worldBotsTrace)
            TraceWorldBots(desiredBots.size(), usSince(sessionsStart, sessionsEnd), usSince(processStart, processEnd),
                usSince(passStart, sessionsStart) + usSince(sessionsEnd, processStart) + usSince(processEnd, WorldBotsClock::now()),
                loginsAllowed - maxLogins, TeleportAcks() - acksBefore);
        return;
    }

    ScaleBotActivity();
    if (sPlayerbotAIConfig.asyncBotLogin)
    {
        auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "AsyncBotLogin");
        sPlayerBotLoginMgr.Update(players);
        pmo.reset();
    }

    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");
    if (!maxAllowedBotCount || ((uint32)maxAllowedBotCount < sPlayerbotAIConfig.minRandomBots || (uint32)maxAllowedBotCount > sPlayerbotAIConfig.maxRandomBots))
    {
        maxAllowedBotCount = urand(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
        SetEventValue(0, "bot_count", maxAllowedBotCount,
            urand(sPlayerbotAIConfig.randomBotCountChangeMinInterval, sPlayerbotAIConfig.randomBotCountChangeMaxInterval));
    }

    std::list<uint32> availableBots = GetBots();    
    uint32 availableBotCount = availableBots.size();
    uint32 onlineBotCount = GetPlayerbotsAmount();
    
    SetAIInternalUpdateDelay(sPlayerbotAIConfig.randomBotUpdateInterval);

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT,
        onlineBotCount < maxAllowedBotCount ? "RandomPlayerbotMgr::Login" : "RandomPlayerbotMgr::UpdateAIInternal");

    if (time(nullptr) > (EventTimeSyncTimer + 30))
        SaveCurTime();

    if (availableBotCount < maxAllowedBotCount && !sWorld.IsShutdowning())
    {
        bool logInAllowed = true;
        if (sPlayerbotAIConfig.randomBotLoginWithPlayer)
        {
            logInAllowed = !players.empty();
        }

        if (logInAllowed)
        {
            AddRandomBots();
            EnsurePinnedBotsOnline();
        }
    }

    if (sPlayerbotAIConfig.syncLevelWithPlayers && players.size())
    {
        if (time(nullptr) > (PlayersCheckTimer + 60))
            CheckPlayers();
    }

    if (sPlayerbotAIConfig.randomBotJoinLfg && players.size())
    {
        if (time(nullptr) > (LfgCheckTimer + 30))
            CheckLfgQueue();
    }

    if (sPlayerbotAIConfig.randomBotJoinBG/* && players.size()*/)
    {
        if (time(nullptr) > (BgCheckTimer + 30))
            CheckBgQueue();
    }

    if (time(nullptr) > (OfflineGroupBotsTimer + 5) && players.size())
        AddOfflineGroupBots();

    uint32 updateBots = sPlayerbotAIConfig.randomBotsPerInterval == 0 ? UINT32_MAX : sPlayerbotAIConfig.randomBotsPerInterval;

    // twow-repo#541: the free random-bot path is timed like the roster path above.
    WorldBotsClock::time_point const legacyProcessStart = WorldBotsClock::now();

    //Update bots
    for (auto bot : availableBots)
    {
        if (GetPlayerBot(bot))
        {
            if (ProcessBot(bot))
                updateBots--;

            if (!updateBots)
                break;
        }
    }

    uint32 maxLogins = sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval;

    //Log in bots
    if (sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") < 10 * IN_MILLISECONDS && !sPlayerbotAIConfig.asyncBotLogin && onlineBotCount < maxAllowedBotCount && maxLogins > 0)
    {
        for (auto bot : availableBots)
        {
            if (GetPlayerBot(bot))
                continue;   

            if (HasCachedEvents(bot) && GetEventValue(bot, "login"))
            {
                onlineBotCount++;
                continue;
            }

            if (GetEventValue(bot, "login"))
                onlineBotCount++;

            if (onlineBotCount >= maxAllowedBotCount)
                break;

            if (ProcessBot(bot)) {
                --maxLogins;
            }

            if (maxLogins == 0)
                break;
        }
    }
    WorldBotsClock::time_point const legacyProcessEnd = WorldBotsClock::now();

    LoginFreeBots();

    //sLog.outString("[char %d, bot %d]", CharacterDatabase.m_threadBody->m_sqlQueue.size(), CharacterDatabase.m_threadBody->m_sqlQueue.size());
   
    LogPlayerLocation();

    DelayedFacingFix();

    MirrorAh();

    // The sPerformanceMonitor.Init() loop used to live here, which is inside the
    // legacy random-roster path and below three early returns. It is now at the
    // top of this function; see the comment there.

    //Ping character database.
    CharacterDatabase.AsyncPQuery(&RandomPlayerbotMgr::DatabasePing, sWorld.GetCurrentMSTime(), std::string("CharacterDatabase"), "SELECT 1");

    PlayerbotHolder::UpdateAIInternal(elapsed, minimal);
    if (sPlayerbotAIConfig.worldBotsTrace)
        TraceWorldBots(availableBotCount, usSince(sessionsStart, sessionsEnd), usSince(legacyProcessStart, legacyProcessEnd),
            usSince(passStart, sessionsStart) + usSince(sessionsEnd, legacyProcessStart) + usSince(legacyProcessEnd, WorldBotsClock::now()),
            sPlayerbotAIConfig.randomBotsMaxLoginsPerInterval - maxLogins, TeleportAcks() - acksBefore);
}

void RandomPlayerbotMgr::ScaleBotActivity()
{
    float activityPercentage = getActivityPercentage();

    //if (activityPercentage >= 100.0f || activityPercentage <= 0.0f) pid.reset(); //Stop integer buildup during max/min activity

    //    % increase/decrease                   wanted diff                                         , avg diff
    float activityPercentageMod = pid.calculate(sRandomPlayerbotMgr.GetPlayers().empty() ? sPlayerbotAIConfig.diffEmpty : sPlayerbotAIConfig.diffWithPlayer, sWorld.GetAverageDiff());

    activityPercentage = activityPercentageMod + 50;

    //Cap the percentage between 0 and 100.
    activityPercentage = std::max(0.0f, std::min(100.0f, activityPercentage));

    setActivityPercentage(activityPercentage);

    if (sPlayerbotAIConfig.hasLog("activity_pid.csv"))
    {
        double virtualMemUsedByMe = 0;
#if PLATFORM == PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        virtualMemUsedByMe = pmc.PrivateUsage;
#endif

        std::ostringstream out;
        out << sWorld.GetCurrentMSTime() << ", ";

        out << sWorld.GetCurrentDiff() << ",";
        out << sWorld.GetAverageDiff() << ",";
        out << sWorld.GetMaxDiff() << ",";
        out << virtualMemUsedByMe << ",";
        out << activityPercentage << ",";
        out << activityPercentageMod << ",";
        out << activeBots << ",";
        out << GetPlayerbotsAmount() << ",";

        float totalLevel = 0, totalGold = 0, totalGearscore = 0;

        if (sPlayerbotAIConfig.randomBotAutologin)
        {
            ForEachPlayerbot([&](Player* bot)
            {
                if (GetBotAI(bot)->AllowActivity())
                {
                    std::string bracket = "level:" + std::to_string(bot->GetLevel() / 10);

                    float level = GetBotAI(bot)->GetLevelFloat();
                    totalLevel += level;
                    float gold = bot->GetMoney() / 10000;
                    totalGold += gold;
                    float gearscore = GetBotAI(bot)->GetEquipGearScore(bot, false, false);
                    totalGearscore += gearscore;

                    const uint32 botGuid = bot->GetObjectGuid().GetCounter();
                    PushMetric(botPerformanceMetrics[bracket], botGuid, level);
                    PushMetric(botPerformanceMetrics["gold"], botGuid, gold);
                    PushMetric(botPerformanceMetrics["gearscore"], botGuid, gearscore);
                }
            });
        }

        out << std::fixed << std::setprecision(4);
        out << totalLevel << ",";

        for (uint8 i = 0; i < (DEFAULT_MAX_LEVEL / 10) + 1; i++)
        {
            out << GetMetricDelta(botPerformanceMetrics["level:" + std::to_string(i)]) * 12 * 60 << ",";
        }

        out << totalGold << ",";
        out << GetMetricDelta(botPerformanceMetrics["gold"]) * 12 * 60 << ",";
        out << totalGearscore << ",";
        out << GetMetricDelta(botPerformanceMetrics["gearscore"]) * 12 * 60 << ",";
        //out << CharacterDatabase.m_threadBody->m_sqlQueue.size();

        sPlayerbotAIConfig.log("activity_pid.csv", out.str().c_str());
    }
}

// twow-repo#541: one [WorldBots] line per minute - what UpdateAIInternal cost the serial world thread.
// share_pm = busy time of the minute in per mille of 60 s; teleport_ack_calls counts HandleTeleportAck
// calls (a bot mid-teleport is called once per pass until it arrives).
void RandomPlayerbotMgr::TraceWorldBots(uint64 desiredBots, uint64 sessionsUs, uint64 processUs, uint64 otherUs, uint32 logins, uint32 acks)
{
    uint64 const now = uint64(time(nullptr));
    if (worldBotsWindow.Due(now))
    {
        ai::world_bots::Window const& w = worldBotsWindow;
        // twow-repo#597 (TICK-01, AiPlayerbot.WorldBotsHookTrace): hook times at the end; empty when off.
        std::string const hook = w.HookSuffix(sPlayerbotAIConfig.worldBotsHookTrace);
        sLog.outBasic("[WorldBots] passes=%u bots_avg=%u sessions_ms=%u process_ms=%u other_ms=%u max_pass_ms=%u share_pm=%u logins=%u teleport_ack_calls=%u online=%u%s",
            w.passes, w.passes ? uint32(w.bots / w.passes) : 0u, uint32(w.sessionsUs / 1000), uint32(w.processUs / 1000),
            uint32(w.otherUs / 1000), uint32(w.maxPassUs / 1000), w.PerMille(), w.logins, w.teleportAcks, uint32(GetPlayerbotsAmount()),
            hook.c_str());
        worldBotsWindow.Reset(now);
    }
    worldBotsWindow.Add(desiredBots, sessionsUs, processUs, otherUs, logins, acks);
}

void RandomPlayerbotMgr::LoginFreeBots()
{
    if (!sPlayerbotAIConfig.freeAltBots.empty() && sPlayerbotAIConfig.botAutologin != BotAutoLogin::LOGIN_ONLY_ALWAYS_ACTIVE)
    {
        std::vector<std::pair<uint32, uint32>> botsToRemove;

        for (auto [accountId, botGuid] : sPlayerbotAIConfig.freeAltBots)
        {
            ObjectGuid guid(ObjectGuid(HIGHGUID_PLAYER, botGuid));
            Player* bot = sObjectMgr.GetPlayer(guid, false);

            if (!bot)
            {
                sLog.outDetail("Add player %d", botGuid);
                AddPlayerBot(botGuid, accountId);
            }
            else if (!bot->IsBeingTeleported())
            {
                if (sRandomPlayerbotMgr.GetValue(botGuid, "create levelup"))
                {
                    PlayerbotFactory factory(bot, bot->GetLevel());
                    factory.Randomize(true, false);

                    sRandomPlayerbotMgr.SetValue(botGuid, "create levelup", 0);
                }

                Player* master = nullptr;

                if (sRandomPlayerbotMgr.GetValue(botGuid, "create group"))
                {
                    std::string groupWith = sRandomPlayerbotMgr.GetData(botGuid, "create group");

                    if (!groupWith.empty())
                    {
                        master = sObjectAccessor.FindPlayerByName(groupWith.c_str());

                        if (master)
                        {
                            GetBotAI(bot)->DoSpecificAction("join", Event("create group", "", master));
                        }
                    }

                    sRandomPlayerbotMgr.SetValue(botGuid, "create group", 0);
                }

                if (sRandomPlayerbotMgr.GetValue(botGuid, "create gear"))
                {
                    std::string gear = sRandomPlayerbotMgr.GetData(botGuid, "create gear");
                    if (gear == "empty")
                    {
                        for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                        {
                            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                        }
                    }

                    else if (gear == "green" || gear == "uncommon")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_UNCOMMON);
                        factory.EquipGear();
                    }
                    else if (gear == "blue" || gear == "rare")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_RARE);
                        factory.EquipGear();
                    }
                    else if (gear == "purple" || gear == "epic")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel(), ITEM_QUALITY_EPIC);
                        factory.EquipGear();
                    }
                    else if (gear == "upgrade")
                    {
                        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(false);
                    }
                    else if (gear == "sync")
                    {
                        PlayerbotFactory factory(bot, master ? master->GetLevel() : bot->GetLevel(), ITEM_QUALITY_NORMAL);
                        factory.UpgradeGear(true);
                    }
                    else if (gear == "best")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearBest();
                    }
                    else if (gear == "partial")
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGearPartialUpgrade();
                    }
                    else
                    {
                        PlayerbotFactory factory(bot, bot->GetLevel());
                        factory.EquipGear();
                    }
                }

                if (GetEventValue(botGuid, "test"))
                {
                    PlayerbotAI* ai = GetBotAI(bot);
                    AiObjectContext* context = ai->GetAiObjectContext();
                    std::string testName = GetEventData(botGuid, "test");
                    testName = std::regex_replace(testName, std::regex("\\'"), "'");
                    std::string strategyName = "test::" + testName;
                    ai->ChangeStrategy("+" + strategyName, BotState::BOT_STATE_NON_COMBAT);
                    SET_AI_VALUE2(bool, "manual bool", "is running test", true);

                    sRandomPlayerbotMgr.SetValue(botGuid, "test", 0);
                }

                if (!IsRandomBot(bot) && GetPlayerBot(guid)) //Place bot in player manager.
                {
                    for (auto& [mGuid, master] : players)
                    {
                        ObjectGuid masterGuid(ObjectGuid(HIGHGUID_PLAYER, mGuid));
                        if (accountId == sObjectMgr.GetPlayerAccountIdByGUID(masterGuid))
                        {
                            PlayerbotMgr* mgr = GetBotMgr(master);
                            if (mgr)
                            {
                                MovePlayerBot(guid, mgr);
                            }
                        }
                    }
                }

                if (master)
                    bot->TeleportTo(WorldPosition(master));

                BotAlwaysOnline always = BotAlwaysOnline(sRandomPlayerbotMgr.GetValue(botGuid, "always"));
                if (always != BotAlwaysOnline::ACTIVE)
                {
                    botsToRemove.push_back({accountId, botGuid});
                }
            }
        }

        sPlayerbotAIConfig.freeAltBots.remove_if([&](const std::pair<uint32, uint32>& entry) {
            return std::find(botsToRemove.begin(), botsToRemove.end(), entry) != botsToRemove.end();
        });
    }
}

void RandomPlayerbotMgr::DelayedFacingFix()
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    for (auto& fMap : facingFix) {
        for (auto& fInstance : fMap.second) {
            for (auto obj : fInstance.second) {
                if (time(0) - obj.second > 5)
                {
                    if (!obj.first.IsCreature())
                        continue;

                    GuidPosition guidP(obj.first, WorldPosition(fMap.first, 0, 0, 0));

                    Creature* unit = guidP.GetCreature(fInstance.first);

                    if (!unit)
                        continue;

                    CreatureData const* data = guidP.GetCreatureData();

                    if (!data)
                        continue;

                    if (unit->GetOrientation() == data->position.orientation)
                        continue;

                    unit->SetFacingTo(data->position.orientation);
                }
            }
        }
        facingFix[fMap.first].clear();
    }
}

void RandomPlayerbotMgr::DatabasePing(QueryResult* result, uint32 pingStart, std::string db)
{
    sRandomPlayerbotMgr.SetDatabaseDelay(db, sWorld.GetCurrentMSTime() - pingStart);
    delete result;
}

void RandomPlayerbotMgr::LoadNamedLocations()
{
    namedLocations.clear();

    auto result = WorldDatabase.Query("SELECT `name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation` FROM `ai_playerbot_named_location` WHERE `name` NOT LIKE 'FISH_LOCATION%'");

    if (!result)
    {
        sLog.outString(">> Loaded 0 named locations - table is empty!");
        sLog.outString();
        return;
    }

    uint32 count = 0;
    do
    {
        ++count;

        Field* fields = result->Fetch();

        std::string name = fields[0].GetCppString();
        uint32 mapId = fields[1].GetUInt32();
        float positionX = fields[2].GetFloat();
        float positionY = fields[3].GetFloat();
        float positionZ = fields[4].GetFloat();
        float orientation = fields[5].GetFloat();

        AddNamedLocation(name, WorldLocation(mapId, positionX, positionY, positionZ, orientation));
    } while (result->NextRow());

    sLog.outString(">> Loaded %u named locations", count);
    sLog.outString();
}

bool RandomPlayerbotMgr::AddNamedLocation(std::string const& name, WorldLocation const& location)
{
    if (namedLocations.find(name) != namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::AddNamedLocation: Failed to add named location '%s' - already exists!", name.c_str());
        return false;
    }

    namedLocations[name] = location;

    return true;
}

bool RandomPlayerbotMgr::GetNamedLocation(std::string const& name, WorldLocation& location)
{
    auto itr = namedLocations.find(name);
    if (itr == namedLocations.end())
    {
        sLog.outError("RandomPlayerbotMgr::GetNamedLocation: Named location '%s' not found! Please ensure that your ai_playerbot_named_location table is up to date.", name.c_str());
        return false;
    }

    location = itr->second;

    return true;
}

uint32 RandomPlayerbotMgr::AddRandomBots()
{
    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");    
    uint32 currentAllowedBotCount = maxAllowedBotCount;

    uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    float currentAvgLevel = 0, wantedAvgLevel = 0, randomAvgLevel = 0;

    if(sPlayerbotAIConfig.asyncBotLogin)
        return 0;
  
    if (currentBots.size() < currentAllowedBotCount)
    {
        if (sPlayerbotAIConfig.syncLevelWithPlayers)
        {
            maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

            wantedAvgLevel = maxLevel / 2;
            uint32 botsAmount = 0;
            ForEachPlayerbot([&](Player* bot)
            {
                currentAvgLevel += bot->GetLevel();
                botsAmount++;
            });
                

            if(currentAvgLevel)
            {
                currentAvgLevel = currentAvgLevel / botsAmount;
            }

            randomAvgLevel = (sPlayerbotAIConfig.randomBotMinLevel + std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)))) / 2;
        }

        currentAllowedBotCount -= currentBots.size();

        int32 neededAddBots = currentAllowedBotCount;

    // BOTPROBE - temporary. Bots below level 20 are almost never online (2.4%)
    // while those at 30-49 are online 96% of the time, and measurement has ruled
    // out guid order, class, race, the logout event and any level filter. The
    // only gate left is the classRaceAllowed quota, which lives in memory only.
    // Counts every row this loop looks at, by level band and by the reason it
    // was skipped, so the answer comes from one startup instead of another guess.
    uint32 botProbe[7][7] = {};
#define BOTPROBE(b, r) do { botProbe[(b)][(r)]++; } while (0)

        currentAllowedBotCount = currentAllowedBotCount*2;      

        CharacterDatabase.AllowAsyncTransactions();
        CharacterDatabase.BeginTransaction();

        bool enoughBotsForCriteria = true;

        for (uint32 noCriteria = 0; noCriteria < 3; noCriteria++)
        {
            int32  classRaceAllowed[MAX_CLASSES][MAX_RACES] = { 0 };

            for (uint32 race = 1; race < MAX_RACES; ++race)
            {
                for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                {
                    if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                    {
                        classRaceAllowed[cls][race] = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                    }
                    else
                    {
                        if (sPlayerbotAIConfig.classRaceProbability[cls][race])
                            classRaceAllowed[cls][race] = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1) * (noCriteria + 1);
                    }
                }
            }

            // The account list used to be walked in the same order on every pass
            // while classRaceAllowed is one global quota - so the first accounts
            // spent it and everything behind them was never reached at all.
            // Measured with the counters below before this fix: accounts 14-135
            // were online ~99% of the time, everything from 138 up sat at 4-6%,
            // and their levels followed, 37 against 11, because only the bots the
            // loop could reach ever got to play. That also starved the LFT queue
            // of low level companions. Shuffling per pass gives every account the
            // same chance of being served first.
            std::vector<uint32> accountOrder = sPlayerbotAIConfig.RandomBotAccountsSnapshot();   // audit A31
            std::shuffle(accountOrder.begin(), accountOrder.end(), *GetRandomGenerator());

            for (std::vector<uint32>::iterator i = accountOrder.begin(); i != accountOrder.end(); i++)
            {
                uint32 accountId = *i;

                std::unique_ptr<QueryResult> result;

                if (noCriteria == 2)
                {
                    result.reset(CharacterDatabase.PQuery("SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u'", accountId));
                }
                else
                {
                    bool needToIncrease = wantedAvgLevel && currentAvgLevel + 1 < wantedAvgLevel;
                    bool needToLower = wantedAvgLevel && currentAvgLevel > wantedAvgLevel + 1;
                    bool rndCanIncrease = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel > currentAvgLevel;
                    bool rndCanLower = !sPlayerbotAIConfig.disableRandomLevels && randomAvgLevel < currentAvgLevel;

                    std::string query = "SELECT guid, level, totaltime, race, class FROM characters WHERE account = '%u' AND level <= %u";
                    std::string wasRand = sPlayerbotAIConfig.instantRandomize ? "totaltime" : "(level > 1)";

                    if (needToIncrease) //We need more higher level bots.
                    {
                        query += " AND (level > %u";
                        if (rndCanIncrease) //Log in higher level bots or bots that will be randomized.
                            query += " OR !" + wasRand;
                        query += ")";

                        result.reset(CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel, (uint32)wantedAvgLevel));
                    }
                    else
                    {
                        if (needToLower && !rndCanLower) //Do not load unrandomized if it'll only increase level.
                            query += " AND " + wasRand;

                        result.reset(CharacterDatabase.PQuery(query.c_str(), accountId, maxLevel));
                    }
                }

                if (!result)
                    continue;

                do
                {
                    Field* fields = result->Fetch();
                    uint32 guid = fields[0].GetUInt32();
                    uint32 level = fields[1].GetUInt32();
                    uint32 totaltime = fields[2].GetUInt32();
                    uint32 race = fields[3].GetUInt32();
                    uint32 cls = fields[4].GetUInt32();

                    uint32 const band = std::min<uint32>(level / 10, 6);
                    BOTPROBE(band, 0);

                    if (GetEventValue(guid, "add"))
                    {
                        BOTPROBE(band, 1);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (GetEventValue(guid, "logout"))
                    {
                        BOTPROBE(band, 2);
                        continue;
                    }

                    if (GetPlayerBot(guid))
                    {
                        BOTPROBE(band, 3);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (std::find(currentBots.begin(), currentBots.end(), guid) != currentBots.end())
                    {
                        BOTPROBE(band, 4);
                        if (!noCriteria)
                            classRaceAllowed[cls][race]--;
                        continue;
                    }

                    if (classRaceAllowed[cls][race] <= 0)
                    {
                        BOTPROBE(band, 5);
                        continue;
                    }

                    BOTPROBE(band, 6);
                    SetEventValue(guid, "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
                    SetEventValue(guid, "logout", 0, 0);
                    currentBots.push_back(guid);

                    if(!noCriteria)
                        classRaceAllowed[cls][race]--;

                    if (wantedAvgLevel)
                    {
                        if (sPlayerbotAIConfig.instantRandomize ? totaltime : level > 1)
                            currentAvgLevel += (float)level / currentBots.size();
                        else
                            currentAvgLevel += (float)level + randomAvgLevel; //Use predicted randomized level. This will be wrong but avarage out correct.
                    }

                    currentAllowedBotCount--;
                    neededAddBots--;

                    if (!currentAllowedBotCount)
                        break;

                } while (result->NextRow());

                if (!currentAllowedBotCount)
                    break;
            }

            if (!currentAllowedBotCount)
                break;

            {
        uint32 gesamt = 0;
        for (uint32 b = 0; b < 7; ++b)
            gesamt += botProbe[b][0];

        if (gesamt)
        {
            sLog.outBasic("BOTPROBE: band | seen | has-add | logout | in-world | in-list | quota | taken");
            for (uint32 b = 0; b < 7; ++b)
            {
                if (!botProbe[b][0])
                    continue;
                sLog.outBasic("BOTPROBE: %2u-%2u | %5u | %7u | %6u | %8u | %7u | %5u | %5u",
                    b * 10, b * 10 + 9, botProbe[b][0], botProbe[b][1], botProbe[b][2],
                    botProbe[b][3], botProbe[b][4], botProbe[b][5], botProbe[b][6]);
            }
        }
    }
#undef BOTPROBE

    if (showLoginWarning && neededAddBots > 0)
            {
                sLog.outError("Not enough accounts to meet selection criteria. A random selection of bots was activated to fill the server.");

                if (sPlayerbotAIConfig.syncLevelWithPlayers)
                    sLog.outError("Only bots between level %d and %d are selected to sync with player level", uint32((currentAvgLevel + 1 < wantedAvgLevel) ? wantedAvgLevel : 1), maxLevel);

                ChatHelper chat(nullptr);

                for (uint32 race = 1; race < MAX_RACES; ++race)
                {
                    for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                    {

                            int32 moreWanted = classRaceAllowed[cls][race];
                            if (moreWanted > 0)
                            {
                                if (sPlayerbotAIConfig.useFixedClassRaceCounts)
                                {
                                    int32 totalWanted = sPlayerbotAIConfig.fixedClassRaceCounts[{cls, race}];
                                    sLog.outError("%d %s %ss needed but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), totalWanted - moreWanted);
                                }
                                else
                                {
                                    int32 totalWanted = ((sPlayerbotAIConfig.classRaceProbability[cls][race] * maxAllowedBotCount / sPlayerbotAIConfig.classRaceProbabilityTotal) + 1);
                                    float percentage = float(sPlayerbotAIConfig.classRaceProbability[cls][race]) * 100.0f / sPlayerbotAIConfig.classRaceProbabilityTotal;
                                    sLog.outError("%d %s %ss needed to get %3.2f%% of total but only %d found.", totalWanted, chat.formatRace(race).c_str(), chat.formatClass(cls).c_str(), percentage, totalWanted - moreWanted);
                                }
                            }
                        
                    }
                }

                showLoginWarning = false;
            }
        }

        CharacterDatabase.CommitTransaction();

        if (currentAllowedBotCount)
            currentAllowedBotCount = std::max(int64(GetEventValue(0, "bot_count")) - int64(currentBots.size()), int64(0));

        if(currentAllowedBotCount && sPlayerbotAIConfig.randomBotAutoCreate && !sPlayerbotAIConfig.useFixedClassRaceCounts)
#ifdef MANGOSBOT_TWO
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 10));
#else
            sLog.outError("Not enough random bot accounts available. Need %d more!!", (uint32)ceil(currentAllowedBotCount / 9));
#endif
      
    }

    return currentBots.size();
}

void RandomPlayerbotMgr::LoadBattleMastersCache()
{
    BattleMastersCache.clear();

    sLog.outString("---------------------------------------");
    sLog.outString("          Loading BattleMasters Cache  ");
    sLog.outString("---------------------------------------");
    sLog.outString();

    auto result = WorldDatabase.Query("SELECT `entry`,`bg_template` FROM `battlemaster_entry`");

    uint32 count = 0;

    if (!result)
    {
        sLog.outString(">> Loaded 0 battlemaster entries - table is empty!");
        sLog.outString();
        return;
    }

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 bgTypeId = fields[1].GetUInt32();

        CreatureInfo const* bmaster = sObjectMgr.GetCreatureTemplate(entry);
        if (!bmaster)
            continue;

#ifdef MANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->FactionAlliance);
#endif
#ifdef CMANGOS
        FactionTemplateEntry const* bmFaction = sFactionTemplateStore.LookupEntry(bmaster->Faction);
#endif
        uint32 bmFactionId = bmFaction->faction;
#ifdef MANGOS
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#ifdef CMANGOS
#ifdef MANGOSBOT_ONE
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry<FactionEntry>(bmFactionId);
#else
        FactionEntry const* bmParentFaction = sFactionStore.LookupEntry(bmFactionId);
#endif
#endif
        uint32 bmParentTeam = bmParentFaction->team;
        Team bmTeam = TEAM_BOTH_ALLOWED;
        if (bmParentTeam == 891)
            bmTeam = ALLIANCE;
        if (bmFactionId == 189)
            bmTeam = ALLIANCE;
        if (bmParentTeam == 892)
            bmTeam = HORDE;
        if (bmFactionId == 66)
            bmTeam = HORDE;

        BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].insert(BattleMastersCache[bmTeam][BattleGroundTypeId(bgTypeId)].end(), entry);
        sLog.outDetail("Cached Battmemaster #%d for BG Type %d (%s)", entry, bgTypeId, bmTeam == ALLIANCE ? "Alliance" : bmTeam == HORDE ? "Horde" : "Neutral");

    } while (result->NextRow());

    sLog.outString(">> Loaded %u battlemaster entries", count);
    sLog.outString();
}

// One key per queue type and bracket (stage b's per-scan bracket sets).
static uint32 BgFillBracketKey(uint32 queueType, uint32 bracket)
{
    return queueType * MAX_BATTLEGROUND_BRACKETS + bracket;
}

// Owner 16.10. (OB-00): a bot the fill queued is alone in its core queue entry (SendBattlemasterJoin, joinAsGroup false).
// A bot in an entry with more members was queued by its group - a real player's group with his bots counts as a whole -
// and belongs to that entry, never to the fill. The core's own reader, a copy under the queue's own m_Lock, on the world
// thread as BattleGroundMgr::Update; only the copy's member count is read.
static bool InGroupQueueEntry(Player* bot, BattleGroundQueueTypeId queueTypeId)
{
    GroupQueueInfo entryInfo;
    return sBattleGroundMgr.m_BattleGroundQueues[queueTypeId].GetPlayerGroupInfoData(bot->GetObjectGuid(), &entryInfo) &&
        entryInfo.Players.size() > 1;
}

// twow-repo#517 stage a (AiPlayerbot.BG.Enable + AiPlayerbot.BG.PlayerFill): persistent-roster bots fill a battleground
// a real player queues for. Runs from the roster branch of UpdateAIInternal, i.e. from PlayerbotWorldScript::OnUpdate
// (WORLDHOOK_ON_UPDATE, end of World::Update): sMapMgr.Update has waited for every continent and instance update by
// then (MapManager::Update: continents.wait() and the instance pool's f.wait()) and the next one starts only in the
// next world tick, so the bots' maps are idle while the manager queues them, accepts their invites and resets them
// after the battleground - the same thread as BattleGroundMgr::Update, which drains the queue schedule.
// Per update only the pending and in-battleground sets are walked (bounded by the bots of the running battlegrounds);
// the real-player scan, and the bounded bot loop only when a real player waits, run once per CheckIntervalSeconds.
// Stage b (AiPlayerbot.BG.SlotRelease, ScanBgSlotRelease) runs in the same pass, after the stage-a scan, and so does the
// bot-only cleanup (PlayerFill and BotOnlyCleanup: WatchBotOnlyBgs per interval, LeaveBotOnlyBg per update).
// Stage c (BgOwnEnabled: announced bot-own battlegrounds) too: the due announcement lines per update (DrainBgAnnounce,
// the only place they are said), the planner once per IntervalMinutes after the scans (PlanBgOwn), and the queue step of
// an announced match once its DelaySeconds ran out (QueueBgOwn, with a scan of its own).
void RandomPlayerbotMgr::CheckBgFill()
{
    // Follow-up D (OB-20 16.10.): the bot-only cleanup only with PlayerFill (bot-only battlegrounds only come from the
    // stage-a fill, startedForFill) and its own switch. Off: neither the watch nor the drain runs, nothing is recorded.
    bool const botOnlyCleanup = sPlayerbotAIConfig.bgEnable && sPlayerbotAIConfig.bgPlayerFill && sPlayerbotAIConfig.bgBotOnlyCleanup;
    // Follow-up D review: switched off while it runs (a config reload), its state goes at once - no stale instance
    // stays for the pair/release scan or the invite check below. A drain cut short that way is left to the core (its
    // PrematureFinish countdown can then end that battleground through EndBattleGround, with rewards for the bots
    // inside).
    if (!botOnlyCleanup)
    {
        bgFillNoRealSince.clear();
        bgFillBotOnlyEnding.clear();
        bgFillBotOnlyWatch.clear();
    }
    UpdateBgFillMembers();
    // Stage b: the watched bot-only battlegrounds per update - the [BGBotOnlyEnd] check and the drain of the ending ones
    // (nothing while none is watched).
    if (botOnlyCleanup)
        LeaveBotOnlyBg();
    // Stage c (BgOwnEnabled): the announcement lines that are due, per update and only here - on the world thread, the
    // ChannelBroadcaster's producer (at most MaxAnnounceDue lines). Off (a config reload): the due lines and an announced
    // match that has not queued yet go at once.
    bool const ownBgs = BgOwnEnabled();
    if (ownBgs)
        DrainBgAnnounce(WorldTimer::getMSTime());
    else
    {
        bgAnnounceDue.clear();
        bgOwnPlan = BgOwnPlan();
    }
    ReportBgFillMinute();

    uint32 const nowMs = WorldTimer::getMSTime();
    // Stage c: an announced match whose DelaySeconds ran out queues in this update, after a fresh scan (its real players
    // and the fill bots already queued are counted), without waiting for the next interval.
    bool const ownQueueDue = ownBgs && bgOwnPlan.queueType && ai::bgfill::DeadlinePassed(nowMs, bgOwnPlan.queueAtMs);
    if (!ownQueueDue && !ai::bgfill::IntervalDue(nowMs, lastBgFillCheckMs, ai::bgfill::CheckIntervalMs(sPlayerbotAIConfig.bgCheckIntervalSeconds)))
        return;
    lastBgFillCheckMs = nowMs ? nowMs : 1;

    WorldBotsClock::time_point const scanStart = WorldBotsClock::now();
    // Stage c: the instances bot-own matches started, before anything counts or watches them (one walk, only while
    // one is known).
    if (!bgOwnInstances.empty())
        PruneBgOwnInstances();
    ScanBgFill(nowMs);
    // Stage b: the bot-only watch first, so the pair/release scan sees the instances that end from now on and sends
    // nobody there.
    if (botOnlyCleanup)
        WatchBotOnlyBgs(nowMs);
    // Stage b after stage a: a player stage a just queued a new match for is left alone in this scan.
    if (sPlayerbotAIConfig.bgSlotRelease)
        ScanBgSlotRelease(nowMs);
    // Stage c after stage a and b: the announced match queues once its delay ran out; else, once per IntervalMinutes,
    // the planner may announce the next one.
    if (ownQueueDue)
        QueueBgOwn(nowMs);
    else if (ownBgs)
        PlanBgOwn(nowMs);
    uint64 const scanUs = usSince(scanStart, WorldBotsClock::now());
    ++bgFillMinute.checks;
    bgFillMinute.scanUsMax = std::max(bgFillMinute.scanUsMax, scanUs);
}

// Every manager update, cheap: only the bots the fill queued (pending) or sent in (in_bg).
void RandomPlayerbotMgr::UpdateBgFillMembers()
{
    for (auto it = bgFillPending.begin(); it != bgFillPending.end();)
    {
        uint32 const guid = it->first;
        BgFillEntry const& entry = it->second;
        Player* bot = GetPlayerBot(guid);
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(entry.queueType);

        // Logged out, or out of that queue (invite expired, queue left): a stale entry, dropped.
        if (!ai || !bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))
        {
            if (ai)
                ai->SetBgFillPending(false);
            it = bgFillPending.erase(it);
            continue;
        }

        // Queued again in a group's entry (owner 16.10.): that entry owns the bot now, it is no fill bot any more. The
        // core lets no group with a queued member join (Group::CanJoinBattleGroundQueue), so this is a guard: the entry
        // is dropped without a leave, which would take the bot out of the group's entry (and the busy/unfit checks below
        // would make it leave). Looked up only for a grouped bot, bounded by the pending set.
        if (bot->GetGroup() && InGroupQueueEntry(bot, queueTypeId))
        {
            if (sPlayerbotAIConfig.bgTrace)
                sLog.outBasic("[BGFill] state=drop bot=%u bg=%u reason=groupentry", guid, entry.bgTypeId);
            ai->SetBgFillPending(false);
            it = bgFillPending.erase(it);
            continue;
        }

        // Teleport running: next update.
        if (!bot->IsInWorld() || bot->IsBeingTeleported())
        {
            ++it;
            continue;
        }

        // OB-10 again: the queue wait can take minutes. Not invited yet: a bot that got parked, grouped (#420, ad-hoc),
        // went into a dungeon, got a quest rescue or went on a forced trip meanwhile is not pulled out of that; it
        // leaves the queue (IsBgFillStillFree). Invited: the real player's match is formed with this bot in it and a
        // decline leaves a hole there, so only hard reasons make it leave (IsBgFillInviteStillFit); an ad-hoc bot group
        // or a forced trip does not. The core can refuse the leave: then the entry stays and the next update tries again.
        bool const invited = bot->IsInvitedForBattleGroundQueueType(queueTypeId);
        char const* leaveReason = nullptr;
        if (!invited && !IsBgFillStillFree(bot, ai))
            leaveReason = "busy";
        else if (invited && !IsBgFillInviteStillFit(bot, ai))
            leaveReason = "unfit";
        // Follow-up D review: the core tops up every battleground with free slots from the whole queue before it forms
        // a new match (BattleGroundQueue::Update over BGFreeSlotQueue, FillPlayersToBG), and each drain leave puts a
        // bot-only battleground back there - so a bot queued for another match can be invited into one being emptied.
        // It declines (port action 0, which also lowers that battleground's invited count) before LeaveBotOnlyBg runs
        // in this update, so the drain sees no open invite and the bot does not refill it.
        else if (invited && IsBotOnlyInvite(bot, entry.bgTypeId))
            leaveReason = "botonly";
        if (leaveReason)
        {
            if (LeaveBgFillQueue(bot, ai, guid, entry, leaveReason))
                it = bgFillPending.erase(it);
            else
                ++it;
            continue;
        }

        // Not invited yet: next update.
        if (!invited)
        {
            ++it;
            continue;
        }

        // A quest rescue asked for before the invite would teleport the bot out of the battleground again
        // (ProcessQuestRescues does not look at battlegrounds): wait until the rescue pass has taken the request (every
        // 10 s) and the bot's AI has cleared the done flag, still within the core's 80 s invite.
        if (ai->IsQuestRescueBusy())
        {
            ++it;
            continue;
        }

        // The core refuses the port in combat, AcceptInvite then returns false and this runs again until the 80 s
        // invite expires.
        if (!BGStatusAction::AcceptInvite(ai, bot, BattleGroundTypeId(entry.bgTypeId)))
        {
            ++it;
            continue;
        }

        // The pending flag stays set while the bot is in the battleground: a WAIT_JOIN status latched before this
        // accept must not run BGStatusAction's port side effects a second time. Cleared on the return below.
        BgFillEntry accepted = entry;
        accepted.instanceId = bot->GetBattleGroundId();
        // Started for the fill: an instance that has not begun yet. A running one the core topped up first from the
        // queue (BattleGroundQueue::FillPlayersToBG) counts against the cap only while it holds other roster bots
        // besides the fill bots (CountBgFillRunning). A stage-b pair or top-up bot goes into a battleground that was
        // already there (WAIT_JOIN too, one the core started for real players): never started for the fill, so it
        // takes no cap and does not put that battleground under the bot-only watch. Stage c: a bot of an announced
        // bot-own match marks a new instance started for the own match instead, never for the fill - the bot-only
        // cleanup never empties it (bots playing by themselves is the owner contract); it counts against the cap all the
        // same (CountBgFillRunning) and against MaxBotBGPerBracket (CountBgOwnPerBand).
        BattleGround const* bg = bot->GetBattleGround();
        accepted.startedForFill = !entry.pairFor && !entry.own && bg && bg->GetStatus() != STATUS_IN_PROGRESS;
        accepted.startedForOwn = entry.own && bg && bg->GetStatus() != STATUS_IN_PROGRESS;
        accepted.acceptMs = WorldTimer::getMSTime();
        if (accepted.startedForOwn)
        {
            // Review c2: the instance stays own for as long as it runs, also once its own bots have left it (a slot
            // release takes the latest joined first) - the bot-only cleanup never drains it (WatchBotOnlyBgs).
            if (accepted.instanceId)
                bgOwnInstances.emplace(std::make_pair(entry.bgTypeId, accepted.instanceId), BgOwnBand(entry.bgTypeId, entry.bracket));
            if (sPlayerbotAIConfig.bgTrace)
                sLog.outBasic("[BGFill] state=own_start bot=%u bg=%u inst=%u", guid, entry.bgTypeId, accepted.instanceId);
        }
        ++bgFillMinute.accepts;
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=accept bot=%u bg=%u inst=%u", guid, entry.bgTypeId, accepted.instanceId);
        bgFillInBg[guid] = accepted;
        it = bgFillPending.erase(it);
    }

    for (auto it = bgFillInBg.begin(); it != bgFillInBg.end();)
    {
        Player* bot = GetPlayerBot(it->first);
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (!ai)
        {
            it = bgFillInBg.erase(it);
            continue;
        }
        // Still in the battleground, or on the way in or out.
        if (bot->InBattleGround() || bot->IsBeingTeleported())
        {
            ++it;
            continue;
        }

        // R6 (OB-10): back on its own map after the battleground. Quest search from the start and the idle watch
        // reset, but no rescue stamp (rescue limiter). The travel target is not restored 1:1: status NONE, the bot
        // picks a new one. The strategies come back through ResetStrategies() on the way out (BGLeaveAction,
        // BGStatusAction TIME_REMOVE, the BGTactics self-heal), which rebuilds the roster bot's AiFactory set.
        ai->OnReturnFromBattleground(uint32(time(nullptr)));
        if (TravelTarget* target = ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get())
            target->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
        ai->SetBgFillPending(false);
        ++bgFillMinute.returns;
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=return bot=%u bg=%u", it->first, it->second.bgTypeId);
        it = bgFillInBg.erase(it);
    }
}

void RandomPlayerbotMgr::ScanBgFill(uint32 nowMs)
{
    bool const trace = sPlayerbotAIConfig.bgTrace;

    // 1. Real players only: the players list also holds bots moved in by MovePlayerBot. Queued for WSG/AB/AV, not
    //    invited yet and not already in a battleground of that type. BR (OB-00 D4) and SV (stage d) are not filled.
    //    A player briefly out of the world (loading screen, far teleport) still counts: his queue slots, team, level
    //    and battleground id live on the Player object (valid until OnPlayerLogout drops him from the list), and
    //    nothing below needs his map - otherwise his waiting fill bots would leave as "abandoned".
    //    Owner 16.10. (OB-00): a real player who queues with a group - his own bots too - has the same priority as a solo
    //    player. The core queues and invites the group as one entry (GroupQueueInfo, BattleGroundQueue::AddGroup), so
    //    the side's real count is that entry's size, every member, real or bot, counted once per entry: a second real
    //    player of the same entry finds himself among the members already counted. The entry's own bracket and team
    //    (the leader's at AddGroup) are used. Read through the core's own reader GetPlayerGroupInfoData (as
    //    BattleGroundHandler and BattleGroundMgr::PlayerLoggedIn): it copies the entry under the queue's own m_Lock, on
    //    the thread of BattleGroundMgr::Update; only the copy's member guids and count are read, never its
    //    PlayerQueueInfo pointers. Bounded by the real players' queue slots and their entries, once per interval.
    uint32 real[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS][2] = {};
    std::set<std::pair<uint32, uint32>> realEntryMembers; // (queue type, member guid low) of the entries counted
    bool realPlayerQueued = false;
    // Stage b (SlotRelease): the waits of the real players seen here are stamped with this scan, the rest pruned below.
    bool const slotRelease = sPlayerbotAIConfig.bgSlotRelease;
    uint32 const scanId = ++bgFillScanId ? bgFillScanId : ++bgFillScanId;
    bgFillNewMatch.clear();
    for (auto const& [playerGuid, player] : players)
    {
        if (!player || !player->InBattleGroundQueue())
            continue;
        if (PlayerbotAI* playerAi = GetBotAI(player))
            if (!playerAi->IsRealPlayer())
                continue;

        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
        {
            BattleGroundQueueTypeId const queueTypeId = player->GetBattleGroundQueueTypeId(slot);
            if (!ai::bgfill::IsFillQueueType(uint32(queueTypeId)))
                continue;
            if (player->IsInvitedForBattleGroundQueueType(queueTypeId))
                continue;
            BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            if (player->InBattleGround() && player->GetBattleGround() && player->GetBattleGround()->GetTypeId() == bgTypeId)
                continue;
            BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            if (!bgTemplate)
                continue;
            // Counted already with the entry of another real player of his group.
            if (realEntryMembers.count(std::make_pair(uint32(queueTypeId), player->GetGUIDLow())))
                continue;
            GroupQueueInfo entryInfo;
            if (!sBattleGroundMgr.m_BattleGroundQueues[queueTypeId].GetPlayerGroupInfoData(player->GetObjectGuid(), &entryInfo))
                continue;
            if (entryInfo.IsInvitedToBGInstanceGUID || entryInfo.BgTypeId != bgTypeId)
                continue;
            // The core queue's bracket of the entry (BattleGroundHandler: the leader's
            // Player::GetBattleGroundBracketIdFromLevel), not BattleGroundMgr::GetBattleGroundBracketIdFromLevel.
            BattleGroundBracketId const bracketId = entryInfo.BracketId;
            if (bracketId == BG_BRACKET_ID_NONE || bracketId >= MAX_BATTLEGROUND_BRACKETS)
                continue;
            uint32 const team = entryInfo.GroupTeam == ALLIANCE ? 0 : 1;
            uint32 const entrySize = uint32(entryInfo.Players.size());
            for (auto const& member : entryInfo.Players)
                realEntryMembers.insert(std::make_pair(uint32(queueTypeId), member.first.GetCounter()));
            // A group larger than a side cannot fit (the core refuses it at the join, Group::CanJoinBattleGroundQueue):
            // no fill for it.
            if (entrySize > bgTemplate->GetMaxPlayersPerTeam())
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=groupsize bg=%u bracket=%u side=%s size=%u max=%u", uint32(bgTypeId),
                        uint32(bracketId), team ? "H" : "A", entrySize, bgTemplate->GetMaxPlayersPerTeam());
                continue;
            }
            real[queueTypeId][bracketId][team] += entrySize;
            realPlayerQueued = true;
            // Stage b: first seen waiting uninvited in this queue - one wait per core queue entry (owner 16.10.: a real
            // player's group with his bots is one unit), under the first real member this scan meets (the players map
            // runs by guid, the dedupe above skips the others of his entry). A new wait takes over the first seen, the
            // release and the reservation of a wait another member of the same entry held before (he logged out): the
            // wait counts from the entry, not from the member (follow-up review: the reservation is keyed by the member
            // too and would otherwise end as "left" while the release stays spent). Bounded by the entry's members.
            if (slotRelease)
            {
                std::pair<uint32, uint32> const waitKey(playerGuid, uint32(queueTypeId));
                BgFillWait& wait = bgFillWait[waitKey];
                if (!wait.firstSeenMs)
                {
                    wait.firstSeenMs = nowMs ? nowMs : 1;
                    for (auto const& member : entryInfo.Players)
                    {
                        auto const earlier = bgFillWait.find(std::make_pair(member.first.GetCounter(), uint32(queueTypeId)));
                        if (earlier == bgFillWait.end() || &earlier->second == &wait || earlier->second.seenScan == scanId ||
                                !earlier->second.firstSeenMs)
                            continue;
                        if (nowMs - earlier->second.firstSeenMs > nowMs - wait.firstSeenMs)
                            wait.firstSeenMs = earlier->second.firstSeenMs;
                        wait.released = wait.released || earlier->second.released;
                        auto const reservation = bgFillReservations.find(earlier->first);
                        if (reservation != bgFillReservations.end() && !bgFillReservations.count(waitKey))
                        {
                            bgFillReservations.emplace(waitKey, reservation->second);
                            bgFillReservations.erase(reservation);
                        }
                    }
                }
                wait.queueType = uint32(queueTypeId);
                wait.bgTypeId = uint32(bgTypeId);
                wait.bracket = uint32(bracketId);
                wait.team = team;
                wait.size = entrySize;
                wait.seenScan = scanId;
            }
        }
    }

    // Stage c: the real players' entries of an announced match's queue and bracket, for its queue step (QueueBgOwn counts
    // them and queues only the rest).
    if (bgOwnPlan.queueType)
    {
        bgOwnPlan.real[0] = real[bgOwnPlan.queueType][bgOwnPlan.bracket][0];
        bgOwnPlan.real[1] = real[bgOwnPlan.queueType][bgOwnPlan.bracket][1];
    }

    // Stage b: a wait not seen in this scan ended (the player got invited, is in the battleground, left the queue or
    // logged out); a new wait starts from zero, with a new release allowed. Bounded by the real players.
    for (auto it = bgFillWait.begin(); it != bgFillWait.end();)
    {
        if (slotRelease && it->second.seenScan == scanId)
            ++it;
        else
            it = bgFillWait.erase(it);
    }

    // AV with Alterac.MinPlayersInQueue above the template minimum: the core waits for that many uninvited entries per
    // side, shuffles them and takes only the template minimum (BattleGroundQueue::Update), so the real player could be
    // left out and a bot-only AV would take the cap. Fail closed: the fill stays out of such an AV bracket, and its
    // waiting fill bots have no room (surplus below). The fill aims for the template minimum everywhere else.
    auto const avQueueMinBlocks = [](BattleGroundTypeId bgTypeId, BattleGround const* bgTemplate)
    {
        return bgTypeId == BATTLEGROUND_AV &&
            ai::bgfill::QueueMinBlocksFill(bgTemplate->GetMinPlayersPerTeam(), sWorld.getConfig(CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE));
    };

    // Fill bots not invited yet leave their queue again, before the forming brackets are counted, when
    // - abandoned: their bracket has no real player waiting any more (he left the queue, or got invited or ported
    //   elsewhere); they would otherwise keep FIFO places ahead of later players and hold the bracket "forming" against
    //   the cap for as long as they stay logged in;
    // - expired: not invited after PendingMaxSeconds;
    // - surplus: their side has more fill bots than the minimum leaves room for next to its real players (left over
    //   from an earlier player). The core takes each side's first entries, so bots queued before a real player could
    //   start the match without him; keeping at most min - real fill bots on the side makes that unlikely, but it is no
    //   guarantee: the entries of logged-out bots still in the core queue are not counted here (the members of a real
    //   player's queued group are: real counts his whole entry).
    // Invited ones are left to the accept (or to the core's invite expiry). A bot the core does not let out keeps its
    // entry. Bounded by the pending set, once per interval; the bots that left are not picked again by this scan.
    std::set<uint32> leftThisScan;
    uint32 kept[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS][2] = {};
    // Stage c (review c2): the bots queued per queue type, bracket and side (fill and own, no pair bots), counted before
    // any leaves - a bot of an announced match whose other side has nobody left to form with (no queued bot, no
    // waiting real player: its bots left, e.g. as "reserve" for a waiting real player) leaves as "ownbroken" instead of
    // holding its FIFO place, the forming count and the level range until PendingMaxSeconds.
    uint32 queuedSide[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS][2] = {};
    for (auto const& [queuedGuid, queued] : bgFillPending)
    {
        if (!queued.pairFor)
            ++queuedSide[queued.queueType][queued.bracket][queued.team];
    }
    for (auto it = bgFillPending.begin(); it != bgFillPending.end();)
    {
        BgFillEntry const& entry = it->second;
        Player* bot = GetPlayerBot(it->first);
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (!ai || bot->IsInvitedForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)))
        {
            ++it;
            continue;
        }
        char const* reason = nullptr;
        if (entry.own)
        {
            // Stage c: a bot of an announced bot-own match waits for its match with or without a real player; only the
            // age bound applies, and it takes no room of a real player's new match (it is counted as pending below).
            // Its match is broken once the other side has neither a queued bot nor a waiting real player.
            uint32 const otherSide = entry.team ? 0 : 1;
            if (ai::bgfill::PendingExpired(nowMs, entry.joinMs, ai::bgfill::PendingMaxSeconds * IN_MILLISECONDS))
                reason = "expired";
            else if (!queuedSide[entry.queueType][entry.bracket][otherSide] && !real[entry.queueType][entry.bracket][otherSide])
                reason = "ownbroken";
        }
        else if (!real[entry.queueType][entry.bracket][0] && !real[entry.queueType][entry.bracket][1])
            reason = "abandoned";
        else if (ai::bgfill::PendingExpired(nowMs, entry.joinMs, ai::bgfill::PendingMaxSeconds * IN_MILLISECONDS))
            reason = "expired";
        else if (entry.pairFor)
        {
            // Stage b pair bot: it stays only while its player still waits uninvited in that queue (the core invites
            // both together); it takes no place in the new-match room below.
            if (!bgFillWait.count(std::make_pair(entry.pairFor, entry.queueType)))
                reason = "unpaired";
        }
        else
        {
            BattleGroundTypeId const bgTypeId = BattleGroundTypeId(entry.bgTypeId);
            BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 const room = bgTemplate && !avQueueMinBlocks(bgTypeId, bgTemplate) ?
                ai::bgfill::NeedPerSide(bgTemplate->GetMinPlayersPerTeam(), real[entry.queueType][entry.bracket][entry.team], 0) : 0;
            uint32& keptSide = kept[entry.queueType][entry.bracket][entry.team];
            if (keptSide >= room)
                reason = "surplus";
            else
                ++keptSide;
        }
        if (!reason)
        {
            ++it;
            continue;
        }
        uint32 const botGuid = it->first;
        if (!LeaveBgFillQueue(bot, ai, botGuid, entry, reason))
        {
            ++it;
            continue;
        }
        leftThisScan.insert(botGuid);
        it = bgFillPending.erase(it);
    }

    // No real player waits: return without touching any other bot - no candidate loop either.
    if (!realPlayerQueued)
        return;

    // 2. Fill bots still in their queue (UpdateBgFillMembers dropped the stale ones) and the brackets still forming.
    uint32 pending[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS][2] = {};
    bool forming[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS] = {};
    uint32 formingBrackets = 0;
    for (auto const& [botGuid, entry] : bgFillPending)
    {
        // Stage b pair bots go into a running battleground (already counted there), not into a new match.
        if (entry.pairFor)
            continue;
        ++pending[entry.queueType][entry.bracket][entry.team];
        if (!forming[entry.queueType][entry.bracket])
        {
            forming[entry.queueType][entry.bracket] = true;
            ++formingBrackets;
        }
    }

    // 3. Need per side and the cap, per bracket with a waiting real player.
    struct Request
    {
        BattleGroundQueueTypeId queueTypeId;
        BattleGroundTypeId bgTypeId;
        BattleGroundBracketId bracketId;
        uint32 team;
        uint32 need;
        std::vector<Player*> candidates;
    };
    std::vector<Request> requests;
    bool runningCounted = false;
    uint32 runningBgs = 0;
    for (uint32 q = BATTLEGROUND_QUEUE_AV; q < MAX_BATTLEGROUND_QUEUE_TYPES; ++q)
    {
        for (uint32 b = BG_BRACKET_ID_FIRST; b < MAX_BATTLEGROUND_BRACKETS; ++b)
        {
            if (!real[q][b][0] && !real[q][b][1])
                continue;
            BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(q);
            BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            BattleGroundBracketId const bracketId = BattleGroundBracketId(b);
            BattleGround* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            if (!bgTemplate)
                continue;

            // R11: the core queue only reacts to events, so a player it skipped (taxi, a member offline) waits for
            // the next one. Same thread as BattleGroundMgr::Update, which swaps the schedule.
            sBattleGroundMgr.ScheduleQueueUpdate(queueTypeId, bgTypeId, bracketId);

            // SlotRelease without PlayerFill: the queue kick above, no new match.
            if (!sPlayerbotAIConfig.bgPlayerFill)
                continue;

            // AV with Alterac.MinPlayersInQueue above the template minimum: no fill (avQueueMinBlocks above).
            if (avQueueMinBlocks(bgTypeId, bgTemplate))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=avqueuemin bg=%u bracket=%u", uint32(bgTypeId), b);
                continue;
            }

            uint32 const minPerTeam = bgTemplate->GetMinPlayersPerTeam();
            uint32 const needA = ai::bgfill::NeedPerSide(minPerTeam, real[q][b][0], pending[q][b][0]);
            uint32 const needH = ai::bgfill::NeedPerSide(minPerTeam, real[q][b][1], pending[q][b][1]);
            if (trace)
                sLog.outBasic("[BGFill] state=queue bg=%u bracket=%u real_a=%u real_h=%u pending_a=%u pending_h=%u need_a=%u need_h=%u",
                    uint32(bgTypeId), b, real[q][b][0], real[q][b][1], pending[q][b][0], pending[q][b][1], needA, needH);
            if (!needA && !needH)
                continue;

            // Stage b: a slot released for a waiting real player is his while it is reserved - no fill bot for that
            // side, and so (no partial side under SlotRelease) no new match in that bracket either.
            if ((needA && BgFillReserved(q, b, 0, nowMs)) || (needH && BgFillReserved(q, b, 1, nowMs)))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=reserved bg=%u bracket=%u side=%s", uint32(bgTypeId), b,
                        needA && needH ? "AH" : needA ? "A" : "H");
                continue;
            }

            // AiPlayerbot.BgBotTeamCap 0 takes a battleground away from bots entirely; that holds here too.
            if (sPlayerbotAIConfig.GetBgBotTeamCap(uint32(bgTypeId)) == 0)
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=teamcap bg=%u bracket=%u side=%s", uint32(bgTypeId), b,
                        needA && needH ? "AH" : needA ? "A" : "H");
                continue;
            }

            if (!runningCounted)
            {
                runningBgs = CountBgFillRunning();
                runningCounted = true;
            }
            uint32 const running = ai::bgfill::RunningForBracket(runningBgs, formingBrackets, forming[q][b]);
            if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=cap running=%u max=%u", running, sPlayerbotAIConfig.bgMaxConcurrent);
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=cap bg=%u bracket=%u side=%s", uint32(bgTypeId), b,
                        needA && needH ? "AH" : needA ? "A" : "H");
                continue;
            }

            // From here on this bracket is forming and counts against the cap of the brackets after it. Under SlotRelease
            // only once it really joins (step 5): a bracket the candidates cannot fill queues nobody, and must not hold
            // the cap against a later bracket that could start.
            if (!forming[q][b] && !sPlayerbotAIConfig.bgSlotRelease)
            {
                forming[q][b] = true;
                ++formingBrackets;
            }
            if (needA)
                requests.push_back(Request{ queueTypeId, bgTypeId, bracketId, 0, needA, {} });
            if (needH)
                requests.push_back(Request{ queueTypeId, bgTypeId, bracketId, 1, needH, {} });
        }
    }

    if (requests.empty())
        return;

    // 4. Candidates: one bounded loop over the bots, from a rotating start so the same bots are not always chosen,
    //    collecting at most CandidateLimit(need) per side.
    uint32 wanted = 0;
    for (Request const& r : requests)
        wanted += ai::bgfill::CandidateLimit(r.need);
    uint32 collected = 0;
    auto const visit = [&](uint32 guid, Player* bot)
    {
        bgFillRotate = guid;
        // Left its queue in this scan (abandoned, expired, surplus): not queued again right away.
        if (leftThisScan.count(guid))
            return;
        if (!IsBgFillCandidate(bot))
            return;
        uint32 const team = bot->GetTeam() == ALLIANCE ? 0 : 1;
        for (Request& r : requests)
        {
            if (r.team != team || r.candidates.size() >= ai::bgfill::CandidateLimit(r.need))
                continue;
            // The core's bracket and join checks (HandleBattlemasterJoinOpcode): level range, not queued for it yet.
            if (bot->GetBattleGroundBracketIdFromLevel(r.bgTypeId) != r.bracketId || !bot->GetBGAccessByLevel(r.bgTypeId) ||
                bot->InBattleGroundQueueForBattleGroundQueueType(r.queueTypeId))
                continue;
            r.candidates.push_back(bot);
            ++collected;
            return;
        }
    };
    PlayerBotMap const& bots = GetAllBots();
    PlayerBotMap::const_iterator const start = bots.upper_bound(bgFillRotate);
    for (auto it = start; it != bots.end() && collected < wanted; ++it)
        visit(it->first, it->second);
    for (auto it = bots.begin(); it != start && collected < wanted; ++it)
        visit(it->first, it->second);

    // Stage b (SlotRelease): a new match is queued whole or not at all. A side the candidates cannot fill would only hold
    // FIFO places without a match; the waiting player is left to the pair option or the slot release after his wait.
    std::set<uint32> incomplete;
    if (sPlayerbotAIConfig.bgSlotRelease)
    {
        for (Request const& r : requests)
            if (r.candidates.size() < r.need)
                incomplete.insert(BgFillBracketKey(uint32(r.queueTypeId), uint32(r.bracketId)));
    }

    // 5. Join on the world thread, as BGJoinAction::JoinQueue does (same packet and battlemaster bypass). No Refresh
    //    for roster bots: no free repair, money or consumables, they keep their own economy.
    for (Request& r : requests)
    {
        uint32 const bracketKey = BgFillBracketKey(uint32(r.queueTypeId), uint32(r.bracketId));
        if (incomplete.count(bracketKey))
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=nobots bg=%u bracket=%u side=%s", uint32(r.bgTypeId), uint32(r.bracketId),
                    r.team ? "H" : "A");
            continue;
        }
        // Stage b (SlotRelease): the cap again, counting only the brackets that really joined in this scan; a whole
        // bracket that passes is forming from here on, for its other side and for the brackets after it.
        if (sPlayerbotAIConfig.bgSlotRelease && !forming[uint32(r.queueTypeId)][uint32(r.bracketId)])
        {
            uint32 const running = ai::bgfill::RunningForBracket(runningBgs, formingBrackets, false);
            if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=cap bg=%u bracket=%u side=%s", uint32(r.bgTypeId), uint32(r.bracketId),
                        r.team ? "H" : "A");
                continue;
            }
            forming[uint32(r.queueTypeId)][uint32(r.bracketId)] = true;
            ++formingBrackets;
        }
        uint32 joined = 0;
        for (Player* bot : r.candidates)
        {
            if (joined >= r.need)
                break;
            PlayerbotAI* ai = GetBotAI(bot);
            BGJoinAction::SendBattlemasterJoin(ai, bot, r.bgTypeId, false);
            // The core refuses silently (its own checks); the next candidate tries.
            if (!bot->InBattleGroundQueueForBattleGroundQueueType(r.queueTypeId))
                continue;
            ai->SetBgFillPending(true);
            BgFillEntry entry;
            entry.queueType = uint32(r.queueTypeId);
            entry.bgTypeId = uint32(r.bgTypeId);
            entry.bracket = uint32(r.bracketId);
            entry.team = r.team;
            entry.joinMs = nowMs;
            bgFillPending[bot->GetGUIDLow()] = entry;
            ++joined;
            ++bgFillMinute.joins;
            if (trace)
                sLog.outBasic("[BGFill] state=join bot=%u bg=%u bracket=%u side=%s", bot->GetGUIDLow(), uint32(r.bgTypeId),
                    uint32(r.bracketId), r.team ? "H" : "A");
        }
        if (trace && joined < r.need)
            sLog.outBasic("[BGFill] state=skip reason=nobots bg=%u bracket=%u side=%s", uint32(r.bgTypeId), uint32(r.bracketId),
                r.team ? "H" : "A");
        // Stage b leaves the players of this bracket to the new match in this scan.
        if (joined)
            bgFillNewMatch.insert(bracketKey);
    }
}

// OB-10 review: a bot on a forced trip (guild founding RosterGuild.FoundTravel #485, guild meeting or errand) stays out;
// pulling it into a battleground would break a trip that can run for an hour. A trip still in PREPARE counts too
// (TravelTarget::IsActive() leaves PREPARE out).
static bool OnForcedTravel(PlayerbotAI* ai)
{
    TravelTarget* target = ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if (!target || !target->IsForced())
        return false;
    TravelStatus const status = target->GetStatus();
    return status != TravelStatus::TRAVEL_STATUS_NONE && status != TravelStatus::TRAVEL_STATUS_EXPIRED;
}

// OB-10 (#420): a group with a real player in it, or a real player's bot. Unlike PlayerbotAI::IsInGroupWithRealPlayer()
// this looks at every online member on any map: the manager runs on the world thread while the maps are idle.
static bool GroupHasRealPlayer(Group* group)
{
    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* member = gref->getSource();
        if (!member)
            continue;
        PlayerbotAI* memberAi = GetBotAI(member);
        if (!memberAi || memberAi->IsRealPlayer() || memberAi->HasRealPlayerMaster())
            return true;
    }
    return false;
}

// A persistent-roster bot that is free to leave for a battleground now.
bool RandomPlayerbotMgr::IsBgFillCandidate(Player* bot) const
{
    if (!bot || !bot->IsInWorld())
        return false;
    uint32 const guid = bot->GetGUIDLow();
    PlayerbotAI* ai = GetBotAI(bot);
    if (!ai || !IsPersistentRosterMember(guid) || !bot->IsAlive())
        return false;
    if (IsParkedBot(guid) || ai->IsParked())
        return false;
    // In no group: covers a group with a real player (#420) and the ad-hoc bot groups. Owner 16.10. (OB-00): a real
    // player's own bots, and every bot of a real player's group, are never free roster bots; queued with him they count
    // as a whole with his entry (ScanBgFill).
    if (bot->GetGroup())
        return false;
    // Not in a dungeon, raid, battleground or arena map.
    if (bot->InBattleGround() || !bot->GetMap() || bot->GetMap()->Instanceable())
        return false;
    if (bot->IsTaxiFlying() || bot->GetTransport() || bot->IsInCombat() || bot->IsBeingTeleportedFar())
        return false;
    // Not in a running quest rescue (asked for, or done and not yet taken by the bot).
    if (ai->IsQuestRescueBusy())
        return false;
    if (ai->HasRealPlayerMaster())
        return false;
    if (OnForcedTravel(ai))
        return false;
    // Deserter (26013): the core's own queue check.
    if (!bot->CanJoinToBattleground())
        return false;
    // Logged in for at least 30 s (the login stamp is a WorldTimer value, HandlePlayerLogin).
    if (WorldTimer::getMSTimeDiffToNow(bot->GetInGameTime()) < ai::bgfill::MinLoggedInSeconds * IN_MILLISECONDS)
        return false;
    if (!bot->HasFreeBattleGroundQueueId())
        return false;
    return !bgFillPending.count(guid) && !bgFillInBg.count(guid);
}

// OB-10 again for a fill bot waiting in its queue, not invited yet: still a free roster bot whose invite may be
// accepted. The bot is in the world (UpdateBgFillMembers checks that first).
bool RandomPlayerbotMgr::IsBgFillStillFree(Player* bot, PlayerbotAI* ai) const
{
    uint32 const guid = bot->GetGUIDLow();
    if (!IsPersistentRosterMember(guid) || IsParkedBot(guid) || ai->IsParked())
        return false;
    if (bot->GetGroup() || ai->HasRealPlayerMaster() || ai->IsQuestRescueBusy())
        return false;
    if (OnForcedTravel(ai))
        return false;
    return !bot->InBattleGround() && !bot->GetMap()->Instanceable();
}

// An invited fill bot: the real player's match is formed with it, so only hard reasons make it decline - no longer a
// persistent-roster bot, a real-player master, in a dungeon, raid or another battleground. Parked counts as hard too:
// ProcessParkedBots would teleport a parked bot from the battleground to its inn spot. A group with a real player (#420)
// counts too. An ad-hoc bot group or a forced trip does not; a quest rescue only delays the accept (UpdateBgFillMembers). The bot is in the world.
bool RandomPlayerbotMgr::IsBgFillInviteStillFit(Player* bot, PlayerbotAI* ai) const
{
    uint32 const guid = bot->GetGUIDLow();
    if (!IsPersistentRosterMember(guid) || IsParkedBot(guid) || ai->IsParked())
        return false;
    if (ai->HasRealPlayerMaster())
        return false;
    // OB-10: an ad-hoc bot group after the invite is fine, a group with a real player (#420) is not; the accept would
    // pull the bot out of a real player's group.
    if (bot->GetGroup() && GroupHasRealPlayer(bot->GetGroup()))
        return false;
    return !bot->InBattleGround() && !bot->GetMap()->Instanceable();
}

// Follow-up D review: the bot is invited into a battleground under the bot-only watch (its grace ran out, it is being
// emptied). Bounded by the watched instances; empty while the cleanup is off (CheckBgFill).
bool RandomPlayerbotMgr::IsBotOnlyInvite(Player* bot, uint32 bgTypeId) const
{
    for (auto const& watch : bgFillBotOnlyWatch)
    {
        if (watch.first.first == bgTypeId && bot->IsInvitedForBattleGroundInstance(watch.first.second))
            return true;
    }
    return false;
}

// A fill bot leaves its queue again (port action 0, the core's own leave; it schedules the queue update itself). The
// core can refuse it (its checks in HandleBattleFieldPortOpcode): a bot still in that queue keeps its flag and entry,
// is not counted as a leave, and the caller tries again later. True when the bot is out of the queue.
bool RandomPlayerbotMgr::LeaveBgFillQueue(Player* bot, PlayerbotAI* ai, uint32 guid, BgFillEntry const& entry, char const* reason)
{
    BGStatusAction::LeaveQueue(bot, BattleGroundTypeId(entry.bgTypeId));
    if (bot->InBattleGroundQueueForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)))
    {
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=leave_failed bot=%u bg=%u reason=%s", guid, entry.bgTypeId, reason);
        return false;
    }
    ai->SetBgFillPending(false);
    ++bgFillMinute.leaves;
    if (sPlayerbotAIConfig.bgTrace)
        sLog.outBasic("[BGFill] state=leave bot=%u bg=%u bracket=%u reason=%s", guid, entry.bgTypeId, entry.bracket, reason);
    return true;
}

// Battlegrounds still running (not ended) that count against AiPlayerbot.BG.MaxConcurrent: the instances fill bots
// were accepted into while they had not begun yet - counted from the accept on, before the bots arrive and show up in
// GetPlayers() - and any other battleground with a persistent-roster bot inside. A running instance the core only
// topped up with fill bots (the fill did not start it) counts only when it holds a roster bot besides those fill bots.
// Once per interval and only when a bracket needs bots: battlegrounds x players (<= 80 each), plus the
// in-battleground set.
uint32 RandomPlayerbotMgr::CountBgFillRunning() const
{
    std::set<std::pair<uint32, uint32>> started;
    std::set<std::pair<uint32, uint32>> toppedUp;
    // Stage c: an instance an announced bot-own match started counts like one started for the fill.
    for (auto const& [botGuid, entry] : bgFillInBg)
        (entry.startedForFill || entry.startedForOwn ? started : toppedUp).emplace(entry.bgTypeId, entry.instanceId);
    // Also once its own bots have left it (bgOwnInstances, review c2).
    for (auto const& [ownKey, ownBand] : bgOwnInstances)
        started.insert(ownKey);

    uint32 running = 0;
    sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
    {
        if (!bg || !bg->GetInstanceID() || bg->GetStatus() == STATUS_WAIT_LEAVE)
            return;
        std::pair<uint32, uint32> const key(uint32(bg->GetTypeID()), bg->GetInstanceID());
        if (started.count(key))
        {
            ++running;
            return;
        }
        bool const toppedUpByFill = toppedUp.count(key) != 0;
        for (auto const& [playerGuid, bgPlayer] : bg->GetPlayers())
        {
            uint32 const playerLow = playerGuid.GetCounter();
            if (!IsPersistentRosterMember(playerLow))
                continue;
            // The topped-up fill bots themselves do not make the instance count.
            if (toppedUpByFill && bgFillInBg.count(playerLow))
                continue;
            ++running;
            return;
        }
    });
    return running;
}

// [BGFill] minute line, at most once per minute (AiPlayerbot.BG.Trace).
void RandomPlayerbotMgr::ReportBgFillMinute()
{
    uint32 const now = uint32(time(nullptr));
    if (!bgFillMinute.last)
        bgFillMinute.last = now;
    if (now - bgFillMinute.last < 60)
        return;
    if (sPlayerbotAIConfig.bgTrace)
        sLog.outBasic("[BGFill] minute checks=%u joins=%u accepts=%u returns=%u pending=%u in_bg=%u scan_us_max=%u leaves=%u",
            bgFillMinute.checks, bgFillMinute.joins, bgFillMinute.accepts, bgFillMinute.returns,
            uint32(bgFillPending.size()), uint32(bgFillInBg.size()), uint32(bgFillMinute.scanUsMax), bgFillMinute.leaves);
    bgFillMinute = BgFillMinute();
    bgFillMinute.last = now;
}

// Stage b: a slot of that queue type, bracket and side is reserved for a waiting real player (until it runs out).
bool RandomPlayerbotMgr::BgFillReserved(uint32 queueType, uint32 bracket, uint32 team, uint32 nowMs) const
{
    for (auto const& [playerKey, res] : bgFillReservations)
    {
        if (res.queueType == queueType && res.bracket == bracket && res.team == team &&
                !ai::bgfill::DeadlinePassed(nowMs, res.untilMs))
            return true;
    }
    return false;
}

// Stage b: a WSG flag carrier never leaves - the flag aura, or the battleground's own flag picker guid.
static bool CarriesBgFlag(Player* bot, BattleGround* bg)
{
    if (bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG))
        return true;
    if (bg->GetTypeID() != BATTLEGROUND_WS)
        return false;
    BattleGroundWS const* ws = static_cast<BattleGroundWS const*>(bg);
    ObjectGuid const guid = bot->GetObjectGuid();
    return ws->GetAllianceFlagPickerGuid() == guid || ws->GetHordeFlagPickerGuid() == guid;
}

// Stage b: a manager-owned fill bot leaves its battleground - for a waiting real player (slot release) or at the end of
// a bot-only match. The core leave with castDeserter = false (OB-00 deserter decision, OB-20), directly on the world
// thread while the maps are idle (the leave opcode's combat refusal does not apply; the victim choice prefers bots out
// of combat), then BGLeaveAction's after-battleground state. The bot stays in bgFillInBg, so the stage-a return runs for
// it as for every return (OnReturnFromBattleground, travel target NONE, flag cleared): it is no leave of its own and is
// not counted as one. True when the bot is out of the battleground.
bool RandomPlayerbotMgr::ReleaseBgFillBot(Player* bot, PlayerbotAI* ai)
{
    uint32 const instanceId = bot->GetBattleGroundId();
    bot->LeaveBattleground(true, false);
    if (bot->InBattleGround() && bot->GetBattleGroundId() == instanceId)
        return false;
    BGStatusAction::ApplyLeaveState(ai, bot);
    return true;
}

// Owner 16.10. (OB-00): a bot of a real player's group is never released, even a fill bot of this manager. Inside the
// battleground GetGroup() is the battleground raid (it holds the real players of that side too), so the bot's own group
// outside is its original group there; a real player's own bot (master) counts too.
static bool InRealPlayerGroup(Player* bot, PlayerbotAI* ai)
{
    if (ai->HasRealPlayerMaster())
        return true;
    Group* group = bot->GetGroup();
    if (group && group->isBGGroup())
        group = bot->GetOriginalGroup();
    return group && GroupHasRealPlayer(group);
}

// Stage b (AiPlayerbot.BG.SlotRelease), once per interval after ScanBgFill and WatchBotOnlyBgs: a real player whose core
// queue entry has waited SlotReleaseWaitSeconds uninvited in the WSG/AB/AV queue gets into a running battleground of his
// type and bracket that holds fill bots (not one whose bot-only end runs). Owner 16.10. (OB-00): the unit is his queue
// entry - with his own bots in his group he has the same priority as a solo player; the group (k members) joins whole
// and the pair option and the slot release apply to the whole entry. Pair option first (OB-00 D2): both sides have k free
// slots, k free roster bots of the other faction queue for that instance (all or none; more while his side is ahead,
// PairCount) and the core (InvitationType 1) invites them together. With his side short of k free slots and the other
// side short, so that releases alone would not get him invited, one bot of the other faction queues as a top-up: the
// core invites it alone and the release follows at a later interval. Else, and also when fewer free bots than needed
// exist for the pair option, a slot release: up to k fill bots of his side leave in one decision (only while the side
// stays at or above the minimum, G2/OB-20; never a bot of a real player's group) and the slots are reserved for the
// entry. Only entries the core would select count: no member in another battleground, on a taxi or logging out, not
// possibly in the premade queue, not larger than a side. Bounded: the waits (one per entry) with their members and
// reservations, one ApplyAllBattleGrounds walk, the pending and in-battleground sets, one rotating bot loop only when a
// pair bot is needed (it stops as soon as every pair has its bots), and per battleground at most one release decision
// per scan.
void RandomPlayerbotMgr::ScanBgSlotRelease(uint32 nowMs)
{
    bool const trace = sPlayerbotAIConfig.bgTrace;
    uint32 const waitMs = ai::bgfill::SlotReleaseWaitMs(sPlayerbotAIConfig.bgSlotReleaseWaitSeconds);

    // 1. Reservations end: the player is invited or in the battleground (success), out of the queue or logged out, or
    //    the reservation ran out (fallback: nothing else is done; his side stays at or above the minimum and the core
    //    queue may top it up).
    for (auto it = bgFillReservations.begin(); it != bgFillReservations.end();)
    {
        uint32 const playerGuid = it->first.first;
        BgFillReservation const& res = it->second;
        BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(res.queueType);
        Player* player = GetPlayer(playerGuid);
        char const* reason = nullptr;
        if (player && (player->IsInvitedForBattleGroundQueueType(queueTypeId) ||
                (player->InBattleGround() && player->GetBattleGroundTypeId() == BattleGroundTypeId(res.bgTypeId))))
            reason = "invited";
        else if (!player || !player->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))
            reason = "left";
        else if (ai::bgfill::DeadlinePassed(nowMs, res.untilMs))
            reason = "expired";
        if (!reason)
        {
            ++it;
            continue;
        }
        if (trace)
            sLog.outBasic("[BGFill] state=reserve_end for=%u bg=%u inst=%u reason=%s", playerGuid, res.bgTypeId, res.instanceId, reason);
        it = bgFillReservations.erase(it);
    }

    // 2. Entries due: waited long enough (the same unsigned age check as the pending expiry), no new match queued for
    //    their bracket in this scan (stage a: the core invites them there), no reservation running for them, and one
    //    the core would select for its free slots. Each core entry once (dueMembers), with its size k, team and
    //    desired instance read from the core's own copy.
    std::vector<std::pair<uint32, BgFillWait*>> due;
    std::set<uint32> dueMembers;   // member guids of the entries already due in this scan
    uint32 const premadeWaitMs = sWorld.getConfig(CONFIG_UINT32_BATTLEGROUND_PREMADE_GROUP_WAIT_FOR_MATCH);
    uint32 const premadeMinSize = sWorld.getConfig(CONFIG_UINT32_BATTLEGROUND_PREMADE_QUEUE_GROUP_MIN_SIZE);
    for (auto& [waitKey, wait] : bgFillWait)
    {
        if (!ai::bgfill::PendingExpired(nowMs, wait.firstSeenMs, waitMs))
            continue;
        if (bgFillNewMatch.count(BgFillBracketKey(wait.queueType, wait.bracket)))
            continue;
        if (bgFillReservations.count(waitKey))
            continue;
        Player* player = GetPlayer(waitKey.first);
        if (!player)
            continue;
        GroupQueueInfo ginfo;
        if (!sBattleGroundMgr.m_BattleGroundQueues[wait.queueType].GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
            continue;
        if (ginfo.IsInvitedToBGInstanceGUID || ginfo.Players.empty())
            continue;
        // One unit per core entry: a second wait of the same entry (another real member) is not due again.
        if (dueMembers.count(waitKey.first))
            continue;
        uint32 const size = uint32(ginfo.Players.size());
        BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(BattleGroundTypeId(wait.bgTypeId));
        if (!bgTemplate)
            continue;
        // A group larger than a side cannot fit (ScanBgFill starts no wait for it; checked again here).
        if (size > bgTemplate->GetMaxPlayersPerTeam())
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=groupsize for=%u bg=%u bracket=%u size=%u max=%u", waitKey.first,
                    wait.bgTypeId, wait.bracket, size, bgTemplate->GetMaxPlayersPerTeam());
            continue;
        }
        // A group in the core's premade queue (BattleGround.PremadeGroupWaitForMatch, PremadeQueue.MinGroupSize) is not
        // seen by FillPlayersToBG, which takes the normal queues only. The join decides it by the group's member count
        // (the entry may hold fewer), and CheckPremadeMatch moves only the head of each premade queue per update to the
        // normal queue (after the wait for a match, or when it is smaller than the minimum); neither is visible here
        // (follow-up review). Fail closed: with the premade queue on, an entry or group of that size stays with the
        // core queue.
        Group* const playerGroup = player->GetGroup();
        uint32 const groupMembers = playerGroup ? playerGroup->GetMembersCount() : 0;
        if (premadeWaitMs && (size >= premadeMinSize || groupMembers >= premadeMinSize))
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=premade for=%u bg=%u bracket=%u size=%u", waitKey.first, wait.bgTypeId,
                    wait.bracket, size);
            continue;
        }
        // The core leaves the whole entry out of its selection while any member is in another battleground, on a taxi,
        // disconnected or logging out (BattleGroundQueue::SelectionPool::AddGroup, the same checks); a pair or a release
        // now would be spent for nothing, and the release could not be repeated in this wait. It stays tracked and is
        // due again once all are free. Bounded by the entry's members (at most a side, above).
        bool memberBusy = false;
        for (auto const& member : ginfo.Players)
        {
            Player* memberPlayer = sObjectAccessor.FindPlayer(member.first);
            if (!memberPlayer)
                continue;
            if (memberPlayer->InBattleGround() || memberPlayer->IsTaxiFlying() ||
                    !memberPlayer->GetSession()->IsConnected() || memberPlayer->GetSession()->isLogingOut())
            {
                memberBusy = true;
                break;
            }
        }
        if (memberBusy)
            continue;
        for (auto const& member : ginfo.Players)
            dueMembers.insert(member.first.GetCounter());
        wait.size = size;
        wait.team = ginfo.GroupTeam == ALLIANCE ? 0 : 1;
        // Queued for one instance (the client's instance choice, BattleGroundHandler): the core selects him only for
        // that one (SelectionPool::AddGroup checks DesiredInstanceId against GetClientInstanceID), so only that one is
        // a target.
        wait.desiredInstanceId = ginfo.DesiredInstanceId;
        due.emplace_back(waitKey.first, &wait);
    }
    if (due.empty())
        return;

    // 3. Running battlegrounds holding manager-owned fill bots (the stage-a in-battleground set), WAIT_JOIN or
    //    IN_PROGRESS (not ending, and not a bot-only one under the watch - being emptied, or paused while a real player
    //    is there; follow-up D review): one walk. Free slots are the core's (invited counts); the minimum check below
    //    uses the players inside (what PrematureFinish counts).
    struct Target
    {
        uint32 bgTypeId = 0;
        uint32 instanceId = 0;
        uint32 clientInstanceId = 0;   // the instance number the client sees (DesiredInstanceId)
        uint32 bracket = 0;
        uint32 maxPerTeam = 0;
        uint32 invited[2] = {};
        uint32 fill[2] = {};
    };
    std::map<std::pair<uint32, uint32>, std::array<uint32, 2>> fillBots;
    for (auto const& [botGuid, entry] : bgFillInBg)
    {
        if (!entry.instanceId || bgFillBotOnlyWatch.count(std::make_pair(entry.bgTypeId, entry.instanceId)))
            continue;
        ++fillBots[std::make_pair(entry.bgTypeId, entry.instanceId)][entry.team];
    }
    std::vector<Target> targets;
    if (!fillBots.empty())
    {
        sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
        {
            if (!bg || !bg->GetInstanceID())
                return;
            if (bg->GetStatus() != STATUS_WAIT_JOIN && bg->GetStatus() != STATUS_IN_PROGRESS)
                return;
            auto const found = fillBots.find(std::make_pair(uint32(bg->GetTypeID()), bg->GetInstanceID()));
            if (found == fillBots.end())
                return;
            Target target;
            target.bgTypeId = uint32(bg->GetTypeID());
            target.instanceId = bg->GetInstanceID();
            target.clientInstanceId = bg->GetClientInstanceID();
            target.bracket = uint32(bg->GetBracketId());
            target.maxPerTeam = bg->GetMaxPlayersPerTeam();
            target.invited[0] = bg->GetInvitedCount(ALLIANCE);
            target.invited[1] = bg->GetInvitedCount(HORDE);
            target.fill[0] = found->second[0];
            target.fill[1] = found->second[1];
            targets.push_back(target);
        });
    }

    // Pair bots already waiting for an entry: one pair (or top-up) at a time per entry and queue.
    std::set<std::pair<uint32, uint32>> pairPending;
    for (auto const& [botGuid, entry] : bgFillPending)
    {
        if (entry.pairFor)
            pairPending.emplace(entry.pairFor, entry.queueType);
    }

    // 4. Per due entry: the target (most fill bots on his side, then the lowest instance id), then the pair option (or
    //    a top-up), else the slot release (step 7), all for the entry's size k.
    struct PairNeed
    {
        uint32 playerGuid = 0;
        BgFillWait* wait = nullptr;
        Target const* target = nullptr;
        uint32 team = 0;          // the pair bots' side: the other faction
        uint32 count = 1;         // bots wanted: PairCount for a pair (all or none), 1 for a top-up
        bool topUp = false;       // his side is short of k free slots: one bot goes in alone, the release follows later
        std::vector<Player*> bots;
    };
    struct ReleaseNeed
    {
        uint32 playerGuid = 0;
        BgFillWait* wait = nullptr;
        Target const* target = nullptr;
    };
    std::vector<PairNeed> pairs;
    std::vector<ReleaseNeed> releases;
    uint32 open = 0;
    for (auto const& [playerGuid, wait] : due)
    {
        uint32 const side = wait->team;
        uint32 const other = side ? 0 : 1;
        uint32 const size = wait->size;
        Target const* target = nullptr;
        for (Target const& t : targets)
        {
            if (t.bgTypeId != wait->bgTypeId || t.bracket != wait->bracket)
                continue;
            if (wait->desiredInstanceId && wait->desiredInstanceId != t.clientInstanceId)
                continue;
            if (!target || ai::bgfill::PreferTarget(t.fill[side], t.instanceId, target->fill[side], target->instanceId))
                target = &t;
        }
        if (!target)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=notarget for=%u bg=%u bracket=%u", playerGuid, wait->bgTypeId, wait->bracket);
            continue;
        }

        // Pair option first (OB-00 D2), for the whole entry. A top-up when his side lacks k free slots and the other side
        // is short: releases alone would not get him invited (ReleaseInvitesPlayer), one bot of the other faction evens
        // the counts first.
        uint32 const differential = target->bgTypeId == uint32(BATTLEGROUND_AV) ? ai::bgfill::DifferentialAV : 0;
        bool const pairOption = ai::bgfill::PairPossible(target->invited[0], target->invited[1], target->maxPerTeam, size);
        bool const topUp = !pairOption &&
            ai::bgfill::TopUpNeeded(target->invited[side], target->invited[other], target->maxPerTeam, differential, size);
        if (pairOption || topUp)
        {
            char const* skip = nullptr;
            if (pairPending.count(std::make_pair(playerGuid, wait->queueType)))
                skip = "pairpending";
            else if (BgFillReserved(wait->queueType, wait->bracket, other, nowMs))
                skip = "reserved";
            if (skip)
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=%s for=%u bg=%u inst=%u bracket=%u", skip, playerGuid,
                        target->bgTypeId, target->instanceId, wait->bracket);
                continue;
            }
            PairNeed need;
            need.playerGuid = playerGuid;
            need.wait = wait;
            need.target = target;
            need.team = other;
            need.count = topUp ? 1 : ai::bgfill::PairCount(target->invited[side], target->invited[other], differential, size);
            need.topUp = topUp;
            open += need.count;
            pairs.push_back(need);
            continue;
        }

        releases.push_back(ReleaseNeed{ playerGuid, wait, target });
    }

    // 5. Pair bots: one rotating loop for every pair need - free roster bots of the other faction in the entry's
    //    bracket (the stage-a candidate rules and the core's join checks); it stops once every pair has its bots (at
    //    once without a pair need).
    auto const visit = [&](uint32 guid, Player* bot)
    {
        bgFillRotate = guid;
        if (!IsBgFillCandidate(bot))
            return;
        uint32 const team = bot->GetTeam() == ALLIANCE ? 0 : 1;
        for (PairNeed& need : pairs)
        {
            if (need.bots.size() >= need.count || need.team != team)
                continue;
            BattleGroundTypeId const bgTypeId = BattleGroundTypeId(need.wait->bgTypeId);
            if (bot->GetBattleGroundBracketIdFromLevel(bgTypeId) != BattleGroundBracketId(need.wait->bracket) ||
                    !bot->GetBGAccessByLevel(bgTypeId) ||
                    bot->InBattleGroundQueueForBattleGroundQueueType(BattleGroundQueueTypeId(need.wait->queueType)))
                continue;
            need.bots.push_back(bot);
            --open;
            return;
        }
    };
    PlayerBotMap const& bots = GetAllBots();
    PlayerBotMap::const_iterator const start = bots.upper_bound(bgFillRotate);
    for (auto it = start; it != bots.end() && open; ++it)
        visit(it->first, it->second);
    for (auto it = bots.begin(); it != start && open; ++it)
        visit(it->first, it->second);

    // 6. The pair bots join with the stage-a join helper (no Refresh) for the target instance (follow-up review: the core
    //    selects them only there, SelectionPool::AddGroup checks DesiredInstanceId, so another battleground of the
    //    bracket cannot take them without him) and are pending with the entry's real player guid. With InvitationType 1
    //    the core invites the entry and the bots together (a top-up bot alone); one not invited within the pending
    //    expiry leaves as in stage a (ScanBgFill), and so does one whose entry no longer waits ("unpaired"). All or
    //    nothing (owner 16.10.): with fewer free bots than the pair needs there is no pair option after all - none of
    //    them queues and the slot release decides for the entry (a top-up has no release to fall back to - the release
    //    would not get him in); a join the core refuses makes the bots already queued for that pair leave again.
    for (PairNeed const& need : pairs)
    {
        if (need.bots.size() < need.count)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=nobots for=%u bg=%u inst=%u bracket=%u side=%s have=%u need=%u",
                    need.playerGuid, need.target->bgTypeId, need.target->instanceId, need.wait->bracket, need.team ? "H" : "A",
                    uint32(need.bots.size()), need.count);
            if (!need.topUp)
                releases.push_back(ReleaseNeed{ need.playerGuid, need.wait, need.target });
            continue;
        }
        BattleGroundTypeId const bgTypeId = BattleGroundTypeId(need.wait->bgTypeId);
        BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(need.wait->queueType);
        std::vector<uint32> joined;
        for (Player* bot : need.bots)
        {
            PlayerbotAI* ai = GetBotAI(bot);
            BGJoinAction::SendBattlemasterJoin(ai, bot, bgTypeId, false, need.target->clientInstanceId);
            if (!bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=refused for=%u bot=%u bg=%u", need.playerGuid, bot->GetGUIDLow(),
                        need.wait->bgTypeId);
                break;
            }
            ai->SetBgFillPending(true);
            BgFillEntry entry;
            entry.queueType = need.wait->queueType;
            entry.bgTypeId = need.wait->bgTypeId;
            entry.bracket = need.wait->bracket;
            entry.team = need.team;
            entry.joinMs = nowMs;
            entry.pairFor = need.playerGuid;
            bgFillPending[bot->GetGUIDLow()] = entry;
            joined.push_back(bot->GetGUIDLow());
            ++bgFillMinute.joins;
        }
        if (joined.size() < need.count)
        {
            // One the core does not let out stays pending for its entry, as any pair bot (unpaired, expired).
            for (uint32 botGuid : joined)
            {
                auto const pending = bgFillPending.find(botGuid);
                Player* bot = GetPlayerBot(botGuid);
                PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
                if (pending != bgFillPending.end() && ai && LeaveBgFillQueue(bot, ai, botGuid, pending->second, "pairshort"))
                    bgFillPending.erase(pending);
            }
            continue;
        }
        for (uint32 botGuid : joined)
        {
            if (trace && need.topUp)
                sLog.outBasic("[BGFill] state=topup bot=%u for=%u bg=%u inst=%u bracket=%u", botGuid, need.playerGuid,
                    need.wait->bgTypeId, need.target->instanceId, need.wait->bracket);
            else if (trace)
                sLog.outBasic("[BGFill] state=pair bot=%u for=%u bg=%u inst=%u bracket=%u size=%u", botGuid, need.playerGuid,
                    need.wait->bgTypeId, need.target->instanceId, need.wait->bracket, need.count);
        }
    }

    // 7. Slot release only without a pair option, at most once per wait and once per battleground per scan: one decision
    //    of up to k releases for the entry.
    std::set<std::pair<uint32, uint32>> releasedThisScan;   // fresh counts next scan
    for (auto const& [playerGuid, wait, target] : releases)
    {
        uint32 const side = wait->team;
        std::pair<uint32, uint32> const targetKey(target->bgTypeId, target->instanceId);
        char const* skip = nullptr;
        if (wait->released)
            skip = "released";
        else if (releasedThisScan.count(targetKey))
            skip = "busy";
        if (skip)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=%s for=%u bg=%u inst=%u bracket=%u", skip, playerGuid,
                    target->bgTypeId, target->instanceId, wait->bracket);
            continue;
        }

        // Victim candidates: manager-owned fill bots of his side inside that instance (not on the way in or out), never a
        // bot of a real player's group (owner 16.10.).
        BattleGround* bg = nullptr;
        std::vector<ai::bgfill::ReleaseCand> cands;
        for (auto const& [botGuid, entry] : bgFillInBg)
        {
            if (entry.bgTypeId != target->bgTypeId || entry.instanceId != target->instanceId || entry.team != side)
                continue;
            Player* bot = GetPlayerBot(botGuid);
            if (!bot || !GetBotAI(bot) || !bot->IsInWorld() || bot->IsBeingTeleported())
                continue;
            if (!bot->InBattleGround() || bot->GetBattleGroundId() != entry.instanceId)
                continue;
            BattleGround* botBg = bot->GetBattleGround();
            if (!botBg)
                continue;
            bg = botBg;
            if (InRealPlayerGroup(bot, GetBotAI(bot)))
                continue;
            ai::bgfill::ReleaseCand cand;
            cand.guid = botGuid;
            cand.joinMs = entry.acceptMs;
            cand.carriesFlag = CarriesBgFlag(bot, botBg);
            cand.inCombat = bot->IsInCombat();
            cand.alive = bot->IsAlive();
            cands.push_back(cand);
        }

        // Live counts from the battleground itself. Only as many releases as the core needs to invite the entry alone
        // (InvitationType 1 keeps the free slots balanced, FillPlayersToBG; ReleaseCount for size k), none when no number
        // of releases does - they would only shrink his side. And only from a side above the minimum that stays at or
        // above it after all of them while the invite is open, so no PrematureFinish (G2, OB-20).
        Team const sideTeam = side ? HORDE : ALLIANCE;
        Team const otherTeam = side ? ALLIANCE : HORDE;
        uint32 releaseCount = 0;
        if (bg)
            releaseCount = ai::bgfill::ReleaseCount(bg->GetInvitedCount(sideTeam), bg->GetInvitedCount(otherTeam),
                bg->GetMaxPlayersPerTeam(), bg->GetTypeID() == BATTLEGROUND_AV ? ai::bgfill::DifferentialAV : 0, wait->size);
        if (!bg)
            skip = "novictim";
        else if (!releaseCount)
            skip = "unbalanced";
        else if (!ai::bgfill::ReleaseAllowed(bg->GetPlayersCountByTeam(sideTeam), bg->GetMinPlayersPerTeam(), releaseCount))
            skip = "min";
        if (skip)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=%s for=%u bg=%u inst=%u bracket=%u size=%u", skip, playerGuid,
                    target->bgTypeId, target->instanceId, wait->bracket, wait->size);
            continue;
        }

        // Fill bots of his side still waiting in that queue could take the freed slots ahead of him (FIFO): plain fill
        // bots leave the queue (under SlotRelease stage a queues only whole matches, so these are left over); another
        // player's pair bot, or a leave the core refuses, puts the release off to the next interval. Invited ones are
        // already in the invited count.
        bool pendingAhead = false;
        bool ownBroken = false;
        for (auto it = bgFillPending.begin(); it != bgFillPending.end();)
        {
            BgFillEntry const& entry = it->second;
            if (entry.queueType != wait->queueType || entry.bracket != wait->bracket || entry.team != side)
            {
                ++it;
                continue;
            }
            Player* bot = GetPlayerBot(it->first);
            PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
            if (ai && bot->IsInvitedForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)))
            {
                ++it;
                continue;
            }
            bool const ownEntry = entry.own;
            if (!ai || entry.pairFor || !LeaveBgFillQueue(bot, ai, it->first, entry, "reserve"))
            {
                pendingAhead = true;
                ++it;
                continue;
            }
            ownBroken = ownBroken || ownEntry;
            it = bgFillPending.erase(it);
        }
        // Stage c (review c2): a bot of an announced bot-own match that left here for the waiting real player breaks that
        // match (the real player keeps his priority), so its other side's bots not invited yet leave the queue now too
        // ("ownbroken") instead of holding FIFO places, the forming count and the level range until PendingMaxSeconds.
        // A leave the core refuses keeps its entry (it expires as any fill bot). Bounded by the pending set.
        if (ownBroken)
        {
            for (auto it = bgFillPending.begin(); it != bgFillPending.end();)
            {
                BgFillEntry const& entry = it->second;
                if (!entry.own || entry.queueType != wait->queueType || entry.bracket != wait->bracket || entry.team == side)
                {
                    ++it;
                    continue;
                }
                Player* bot = GetPlayerBot(it->first);
                PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
                if (!ai || bot->IsInvitedForBattleGroundQueueType(BattleGroundQueueTypeId(entry.queueType)) ||
                        !LeaveBgFillQueue(bot, ai, it->first, entry, "ownbroken"))
                {
                    ++it;
                    continue;
                }
                it = bgFillPending.erase(it);
            }
        }
        if (pendingAhead)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=pending for=%u bg=%u inst=%u bracket=%u", playerGuid,
                    target->bgTypeId, target->instanceId, wait->bracket);
            continue;
        }

        // The victims, one by one from the same candidates: never a flag carrier or a dead bot; out of combat first, then
        // the latest joined, then the lowest guid (a chosen one drops out with guid 0). Fewer victims than releases: none
        // leaves - the entry would not get in - and the next interval tries again.
        std::vector<uint32> victims;
        while (victims.size() < releaseCount)
        {
            uint32 const chosen = ai::bgfill::ChooseReleaseVictim(cands, nowMs);
            if (!chosen)
                break;
            victims.push_back(chosen);
            for (ai::bgfill::ReleaseCand& cand : cands)
            {
                if (cand.guid == chosen)
                    cand.guid = 0;
            }
        }
        if (victims.size() < releaseCount)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=novictim for=%u bg=%u inst=%u bracket=%u have=%u need=%u", playerGuid,
                    target->bgTypeId, target->instanceId, wait->bracket, uint32(victims.size()), releaseCount);
            continue;
        }

        // Each leave keeps his side above the minimum before it (live count), so the side ends at or above it.
        uint32 released = 0;
        for (uint32 victimGuid : victims)
        {
            Player* victim = GetPlayerBot(victimGuid);
            PlayerbotAI* victimAi = victim ? GetBotAI(victim) : nullptr;
            if (!victimAi || !ai::bgfill::ReleaseAllowed(bg->GetPlayersCountByTeam(sideTeam), bg->GetMinPlayersPerTeam()))
                break;
            uint32 const countBefore = bg->GetPlayersCountByTeam(sideTeam);
            if (!ReleaseBgFillBot(victim, victimAi))
            {
                if (trace)
                    sLog.outBasic("[BGFill] state=skip reason=leavefailed for=%u bg=%u inst=%u bracket=%u", playerGuid,
                        target->bgTypeId, target->instanceId, wait->bracket);
                break;
            }
            uint32 const countAfter = bg->GetPlayersCountByTeam(sideTeam);
            ++released;

            // The measurement point (OB-20): always written, one line per released bot, for the entry's real player.
            sLog.outBasic("[BGSlotRelease] bg=%u,%u bot=%u for=%u side=%s count_before=%u count_after=%u", bg->GetMapId(),
                bg->GetInstanceID(), victimGuid, playerGuid, side ? "H" : "A", countBefore, countAfter);
        }
        if (!released)
            continue;
        releasedThisScan.insert(targetKey);
        // Cut short (a leave failed, or the live minimum stopped it): the entry gets in only with all releaseCount slots,
        // so the decision is not spent and nothing is reserved - the next interval decides again on the live counts,
        // which no longer hold the bots that left (follow-up review).
        if (released < releaseCount)
        {
            if (trace)
                sLog.outBasic("[BGFill] state=skip reason=partial for=%u bg=%u inst=%u bracket=%u released=%u need=%u", playerGuid,
                    target->bgTypeId, target->instanceId, wait->bracket, released, releaseCount);
            continue;
        }
        wait->released = true;

        // The freed slots are the entry's until it is invited, leaves the queue or the reservation runs out - one
        // reservation per entry, with the bots released for it; meanwhile no fill bot is queued for that side and bracket
        // and no second release decision is made for it.
        BgFillReservation res;
        res.queueType = wait->queueType;
        res.bgTypeId = wait->bgTypeId;
        res.bracket = wait->bracket;
        res.team = side;
        res.instanceId = target->instanceId;
        res.untilMs = nowMs + waitMs;
        res.size = wait->size;
        res.released = released;
        bgFillReservations[std::make_pair(playerGuid, wait->queueType)] = res;
        if (trace)
            sLog.outBasic("[BGFill] state=reserve for=%u bg=%u inst=%u side=%s size=%u released=%u wait_s=%u", playerGuid,
                res.bgTypeId, res.instanceId, side ? "H" : "A", res.size, res.released, waitMs / IN_MILLISECONDS);
    }
}

// Stage b (Enable && PlayerFill && BotOnlyCleanup, follow-up D with OB-20): a battleground started for the fill (stage a,
// startedForFill) that has had no real player inside or invited for BotOnlyGraceSeconds after reaching IN_PROGRESS
// ends - every manager bot in it leaves, of both sides, without Deserter (LeaveBotOnlyBg), until it holds nobody and no
// open invite, and the core deletes it. It must not end through EndBattleGround, which rewards whoever is inside (marks
// and experience even without a winner), here only bots (OB-20). A real player with an open invite counts as present (up
// to 80 s). A real player who only waits uninvited does not (follow-up review: nothing may ever bring him into that
// battleground - no pair or release path, or one that keeps failing - while it holds the cap against the new match he
// needs); the leave keeps the core from inviting him instead. An instance whose grace ran out stays watched
// (bgFillBotOnlyWatch) until the core has deleted it, also once its fill bots are out and only other manager bots are
// left; a real player present then only pauses its drain, without a new grace. Once per interval; bounded by the
// started and watched instances, one ApplyAllBattleGrounds walk and the real players.
void RandomPlayerbotMgr::WatchBotOnlyBgs(uint32 nowMs)
{
    std::set<std::pair<uint32, uint32>> started;
    std::map<std::pair<uint32, uint32>, uint32> fillIn;   // every manager-owned fill bot of those instances
    for (auto const& [botGuid, entry] : bgFillInBg)
    {
        if (!entry.instanceId)
            continue;
        std::pair<uint32, uint32> const key(entry.bgTypeId, entry.instanceId);
        ++fillIn[key];
        if (entry.startedForFill)
            started.insert(key);
    }
    // Still watched after its last fill bot left: other manager bots can be inside, and it ends only once empty.
    for (auto const& watch : bgFillBotOnlyWatch)
        started.insert(watch.first);
    // Stage c: an instance an announced bot-own match started is never emptied - bots playing by themselves is the owner
    // contract - also when fill bots of a real player's match went in with it, and also once its own bots have left it
    // (bgOwnInstances, kept from the accept until the instance ends; review c2). Nothing is put back after this.
    for (auto const& [key, ownBand] : bgOwnInstances)
        started.erase(key);
    for (auto it = bgFillNoRealSince.begin(); it != bgFillNoRealSince.end();)
    {
        if (started.count(it->first))
            ++it;
        else
            it = bgFillNoRealSince.erase(it);
    }
    for (auto it = bgFillBotOnlyEnding.begin(); it != bgFillBotOnlyEnding.end();)
    {
        if (started.count(*it))
            ++it;
        else
            it = bgFillBotOnlyEnding.erase(it);
    }
    if (started.empty())
        return;

    std::set<std::pair<uint32, uint32>> inProgress;
    sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
    {
        if (!bg || !bg->GetInstanceID() || bg->GetStatus() != STATUS_IN_PROGRESS)
            return;
        std::pair<uint32, uint32> const key(uint32(bg->GetTypeID()), bg->GetInstanceID());
        if (started.count(key))
            inProgress.insert(key);
    });

    // Real players only (the players list also holds bots moved in by MovePlayerBot): inside, or invited.
    std::set<std::pair<uint32, uint32>> realPresent;
    for (auto const& [playerGuid, player] : players)
    {
        if (!player)
            continue;
        if (PlayerbotAI* playerAi = GetBotAI(player))
            if (!playerAi->IsRealPlayer())
                continue;
        for (std::pair<uint32, uint32> const& key : started)
        {
            if ((player->InBattleGround() && player->GetBattleGroundId() == key.second) ||
                    player->IsInvitedForBattleGroundInstance(key.second))
                realPresent.insert(key);
        }
    }

    uint32 const graceMs = ai::bgfill::BotOnlyGraceMs(sPlayerbotAIConfig.bgBotOnlyGraceSeconds);
    for (std::pair<uint32, uint32> const& key : started)
    {
        if (!inProgress.count(key) || realPresent.count(key))
        {
            bgFillBotOnlyEnding.erase(key);
            // Follow-up D review: once its grace ran out (watched) the drain may have begun, the battleground is below
            // the minimum and the core's PrematureFinish countdown runs on regardless of a grace here. A real player
            // inside or invited then only pauses the drain: the watch and the grace are kept, so the first interval
            // without him marks it again at once (a lapsed invite, a short visit) and a countdown end still gets its
            // [BGBotOnlyEnd] line. Only one still inside at its end drops the watch (LeaveBotOnlyBg, no bot-only end).
            // Before the grace ran out he starts it again. One that only left IN_PROGRESS stays watched until
            // LeaveBotOnlyBg has looked at its end and the core has deleted it.
            if (!bgFillBotOnlyWatch.count(key))
                bgFillNoRealSince.erase(key);
            continue;
        }
        uint32 const since = bgFillNoRealSince.emplace(key, nowMs ? nowMs : 1).first->second;
        if (bgFillBotOnlyEnding.count(key) || !ai::bgfill::PendingExpired(nowMs, since, graceMs))
            continue;
        bgFillBotOnlyEnding.insert(key);
        bgFillBotOnlyWatch.emplace(key, false);
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=botonly_end bg=%u inst=%u bots=%u", key.first, key.second, fillIn[key]);
    }
}

// Stage b, every manager update while an instance is watched (its grace ran out): one ApplyAllBattleGrounds walk finds
// the watched battlegrounds. One the core no longer holds is pruned, with its grace and ending mark. One that reached
// WAIT_LEAVE anyway - an EndBattleGround ran (a win, the WSG/AB time limit, PrematureFinish while the end waited) - gets
// one [BGBotOnlyEnd] line, always written (bounded by the battleground ends), unless someone inside is not a manager bot
// (then it was no bot-only end).
// Then the drain: at most BotOnlyLeavesPerUpdate manager bots of the ending IN_PROGRESS battlegrounds leave per update,
// with castDeserter = false (ReleaseBgFillBot); the stage-a return runs for the fill bots among them. Every manager bot
// inside counts, of both sides, so the battleground ends up empty: without an open invite BattleGround::Update deletes
// it at its next update - its empty check returns before the PrematureFinish countdown - and EndBattleGround never runs.
// The side of each leave: BotOnlyDrainSide, the larger side first (no side order lets the core end it at once).
// Each leave frees a slot, and the core may invite a waiting player into it at its next queue update (the next world
// tick, RemovePlayerAtLeave schedules it) - long before the next WatchBotOnlyBgs. So a bot leaves only from a side where
// that cannot happen (follow-up review, BotOnlyLeaveAllowed): no real player's entry of its side waits uninvited for that
// battleground type and bracket, or the core would not invite the smallest of them even after the leave (CoreInvitesEntry
// on the live invited counts minus that bot; a later leave of the other side only makes that less likely). With WSG/AB
// (differential 0) a player waiting on one side is never invited by the drain. When no bot can leave that way (players
// waiting on both sides, or AV near the end), the end pauses - the ending mark is dropped, the grace keeps running and
// the watch marks it again at its next interval - but only for one more BotOnlyGraceSeconds after the end was due;
// then the bots leave regardless, so a battleground nobody joins does not hold the cap for good.
// Guards before every leave: an open invite (more invited than inside) or anyone inside who is not a bot of this
// manager pauses the end - the ending mark is dropped, the watch and the grace stay (follow-up D review: the core's
// countdown does not wait for a new grace), and the watch marks it again at its first interval without a real player
// present. A fill or pair bot queued for another match and invited there declines first (UpdateBgFillMembers,
// IsBotOnlyInvite), so its invite does not pause the end. The battleground pointers hold for this update: the core
// deletes a battleground only in its own map update (BattleGround::Update from BattleGroundMap::Update), and the maps
// are idle here (CheckBgFill). Bounded: the walk, the players of the watched battlegrounds, and one walk over the real
// players' queue entries only while an end runs.
void RandomPlayerbotMgr::LeaveBotOnlyBg()
{
    if (bgFillBotOnlyWatch.empty())
        return;
    std::map<std::pair<uint32, uint32>, BattleGround const*> watched;
    sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
    {
        if (!bg || !bg->GetInstanceID())
            return;
        std::pair<uint32, uint32> const key(uint32(bg->GetTypeID()), bg->GetInstanceID());
        if (bgFillBotOnlyWatch.count(key))
            watched.emplace(key, bg);
    });
    for (auto it = bgFillBotOnlyWatch.begin(); it != bgFillBotOnlyWatch.end();)
    {
        auto const found = watched.find(it->first);
        if (found == watched.end())
        {
            // Deleted by the core (emptied, or after its WAIT_LEAVE): nothing of it is kept.
            bgFillNoRealSince.erase(it->first);
            bgFillBotOnlyEnding.erase(it->first);
            it = bgFillBotOnlyWatch.erase(it);
            continue;
        }
        BattleGround const* bg = found->second;
        if (it->second || bg->GetStatus() != STATUS_WAIT_LEAVE)
        {
            ++it;
            continue;
        }
        uint32 bots = 0;
        bool others = false;
        for (auto const& member : bg->GetPlayers())
        {
            if (GetPlayerBot(member.first.GetCounter()))
                ++bots;
            else
                others = true;
        }
        if (others)
        {
            it = bgFillBotOnlyWatch.erase(it);
            continue;
        }
        BattleGroundWinner const winner = bg->GetWinner();
        sLog.outBasic("[BGBotOnlyEnd] bg=%u,%u winner=%s bots=%u", bg->GetMapId(), bg->GetInstanceID(),
            winner == WINNER_ALLIANCE ? "A" : winner == WINNER_HORDE ? "H" : "none", bots);
        it->second = true;
        ++it;
    }
    if (bgFillBotOnlyEnding.empty())
        return;

    // Once per update, before any leave (not per bot).
    std::map<std::pair<uint32, uint32>, std::array<uint32, 2>> waiting;
    CollectBgFillWaiting(waiting);
    uint32 const nowMs = WorldTimer::getMSTime();
    uint32 const graceMs = ai::bgfill::BotOnlyGraceMs(sPlayerbotAIConfig.bgBotOnlyGraceSeconds);
    std::set<std::pair<uint32, uint32>> held;   // ending instances whose next leave would let the core invite a waiting player
    std::vector<std::pair<uint32, uint32>> const ending(bgFillBotOnlyEnding.begin(), bgFillBotOnlyEnding.end());
    uint32 leaves = 0;
    for (std::pair<uint32, uint32> const& key : ending)
    {
        if (leaves >= ai::bgfill::BotOnlyLeavesPerUpdate)
            break;
        auto const found = watched.find(key);
        if (found == watched.end() || found->second->GetStatus() != STATUS_IN_PROGRESS)
            continue;
        BattleGround const* bg = found->second;
        auto const since = bgFillNoRealSince.find(key);
        bool const pauseOver = since != bgFillNoRealSince.end() && ai::bgfill::PendingExpired(nowMs, since->second, 2 * graceMs);
        // The smallest waiting entry per side of that type and the battleground's bracket: the core invites it after a
        // leave exactly when CoreInvitesEntry holds for the counts minus that bot (a larger entry or several need more).
        auto const waits = waiting.find(std::make_pair(key.first, uint32(bg->GetBracketId())));
        uint32 const differential = bg->GetTypeID() == BATTLEGROUND_AV ? ai::bgfill::DifferentialAV : 0;
        while (leaves < ai::bgfill::BotOnlyLeavesPerUpdate)
        {
            char const* stop = nullptr;
            std::array<uint32, 2> inside = { 0, 0 };          // manager bots inside per side
            std::array<Player*, 2> leaver = { nullptr, nullptr };
            if (bg->GetInvitedCount(ALLIANCE) + bg->GetInvitedCount(HORDE) > bg->GetPlayersSize())
                stop = "invite";
            else
            {
                for (auto const& member : bg->GetPlayers())
                {
                    Player* bot = GetPlayerBot(member.first.GetCounter());
                    if (!bot)
                    {
                        stop = "player";
                        break;
                    }
                    // OB-10 16.10.: a bot of a real player's group (its master, or its original group holds a real
                    // player) never leaves; it keeps the battleground from emptying, so the end stops as for a player.
                    PlayerbotAI* const botAi = GetBotAI(bot);
                    if (botAi && InRealPlayerGroup(bot, botAi))
                    {
                        stop = "realgroup";
                        break;
                    }
                    uint32 const side = member.second.PlayerTeam == HORDE ? 1 : 0;
                    ++inside[side];
                    if (!leaver[side] && GetBotAI(bot) && bot->IsInWorld() && !bot->IsBeingTeleported() &&
                            bot->InBattleGround() && bot->GetBattleGroundId() == key.second)
                        leaver[side] = bot;
                }
            }
            if (stop)
            {
                bgFillBotOnlyEnding.erase(key);
                if (sPlayerbotAIConfig.bgTrace)
                    sLog.outBasic("[BGFill] state=botonly_stop bg=%u inst=%u reason=%s", key.first, key.second, stop);
                break;
            }
            // Empty: the core deletes it at its next update (no open invite, checked above).
            if (!inside[0] && !inside[1])
                break;
            std::array<bool, 2> mayLeave = { false, false };
            bool blocked = false;
            for (uint32 side = 0; side < 2; ++side)
            {
                Team const sideTeam = side ? HORDE : ALLIANCE;
                Team const otherTeam = side ? ALLIANCE : HORDE;
                uint32 const smallest = waits != waiting.end() ? waits->second[side] : 0;
                bool const allowed = ai::bgfill::BotOnlyLeaveAllowed(bg->GetInvitedCount(sideTeam), bg->GetInvitedCount(otherTeam),
                    bg->GetMaxPlayersPerTeam(), differential, smallest, pauseOver);
                mayLeave[side] = allowed && leaver[side];
                blocked = blocked || (!allowed && leaver[side]);
            }
            uint32 const side = ai::bgfill::BotOnlyDrainSide(inside[0], inside[1], mayLeave[0], mayLeave[1]);
            if (side == ai::bgfill::NoDrainSide)
            {
                if (blocked)
                    held.insert(key);
                break;
            }
            Player* bot = leaver[side];
            // The core refused the leave: the next update tries again.
            if (!ReleaseBgFillBot(bot, GetBotAI(bot)))
                break;
            ++leaves;
        }
    }
    // No bot of these could leave without the core inviting a waiting player: they pause until the next watch interval.
    for (std::pair<uint32, uint32> const& key : held)
    {
        bgFillBotOnlyEnding.erase(key);
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=botonly_stop bg=%u inst=%u reason=waiting", key.first, key.second);
    }
}

// Stage b: the real players' core queue entries waiting uninvited in a WSG/AB/AV queue, per (bg type, the entry's
// bracket): the smallest entry size per side (0 = none waits). Read live through the core's own reader, as ScanBgFill
// (follow-up review: the waits of the last scan missed a player who queued since, and a member's own level bracket can
// differ from the entry's, which is the leader's): GetPlayerGroupInfoData copies the entry under the queue's own lock;
// only its size, team and bracket are read. Bounded by the real players' queue slots; only while a bot-only end runs,
// once per update.
void RandomPlayerbotMgr::CollectBgFillWaiting(std::map<std::pair<uint32, uint32>, std::array<uint32, 2>>& waiting) const
{
    for (auto const& [playerGuid, player] : players)
    {
        if (!player || !player->InBattleGroundQueue())
            continue;
        if (PlayerbotAI* playerAi = GetBotAI(player))
            if (!playerAi->IsRealPlayer())
                continue;
        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
        {
            BattleGroundQueueTypeId const queueTypeId = player->GetBattleGroundQueueTypeId(slot);
            if (!ai::bgfill::IsFillQueueType(uint32(queueTypeId)) || player->IsInvitedForBattleGroundQueueType(queueTypeId))
                continue;
            BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            if (player->InBattleGround() && player->GetBattleGround() && player->GetBattleGround()->GetTypeId() == bgTypeId)
                continue;
            GroupQueueInfo entryInfo;
            if (!sBattleGroundMgr.m_BattleGroundQueues[queueTypeId].GetPlayerGroupInfoData(player->GetObjectGuid(), &entryInfo))
                continue;
            if (entryInfo.IsInvitedToBGInstanceGUID || entryInfo.BgTypeId != bgTypeId || entryInfo.Players.empty())
                continue;
            if (entryInfo.BracketId == BG_BRACKET_ID_NONE || entryInfo.BracketId >= MAX_BATTLEGROUND_BRACKETS)
                continue;
            uint32 const size = uint32(entryInfo.Players.size());
            uint32& smallest = waiting[std::make_pair(uint32(bgTypeId), uint32(entryInfo.BracketId))][entryInfo.GroupTeam == ALLIANCE ? 0 : 1];
            if (!smallest || size < smallest)
                smallest = size;
        }
    }
}

// twow-repo#517 stage c (owner contract: bots play battlegrounds by themselves only when a start is announced, by 2-5 of
// the otherwise silent bots in world chat): the full gate. Everything off - no work at all; MaxConcurrent 0 also blocks
// (fail closed). MaxPerHour 0 blocks every start in the planner (no line, no bot-own battleground).
bool RandomPlayerbotMgr::BgOwnEnabled() const
{
    return sPlayerbotAIConfig.bgEnable && sPlayerbotAIConfig.bgAnnounce && sPlayerbotAIConfig.bgAnnounceIntervalMinutes > 0 &&
        sPlayerbotAIConfig.bgMaxConcurrent > 0;
}

// Stage c: the level range of a battleground's bracket (ai::bgfill::LevelBand): owner C0 counts bot-own battlegrounds
// per level range over all types, and the core's bracket index differs per type for the same levels.
uint32 RandomPlayerbotMgr::BgOwnBand(uint32 bgTypeId, uint32 bracket) const
{
    BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(BattleGroundTypeId(bgTypeId));
    if (!bgTemplate)
        return 0;
    return ai::bgfill::LevelBand(bgTemplate->GetMinLevel(), bgTemplate->GetMaxLevel(), bracket);
}

// Owner Q2 (17.10.): the level range of a battleground's bracket for the announcement text (ai::bgfill::AnnounceLevelRange)
// - the core's own bracket levels (Player::Get{Min,Max}LevelForBattleGroundBracketId, the levels BattleGroundQueue::Update
// forms that bracket's match with), the top capped at the template's maximum level. Empty (no range) without a template.
std::string RandomPlayerbotMgr::BgAnnounceLevelRange(uint32 bgTypeId, uint32 bracket) const
{
    BattleGroundTypeId const typeId = BattleGroundTypeId(bgTypeId);
    BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(typeId);
    if (!bgTemplate || bracket >= MAX_BATTLEGROUND_BRACKETS)
        return std::string();
    BattleGroundBracketId const bracketId = BattleGroundBracketId(bracket);
    return ai::bgfill::AnnounceLevelRange(Player::GetMinLevelForBattleGroundBracketId(bracketId, typeId),
        Player::GetMaxLevelForBattleGroundBracketId(bracketId, typeId), bgTemplate->GetMaxLevel());
}

// Stage c (owner C0, MaxBotBGPerBracket): bot-own battlegrounds per level range, any type - running (an instance a
// bot-own match started and not ended, counted from the first accept on, as CountBgFillRunning does, and also once its
// own bots have left it: bgOwnInstances), forming (own bots queued, per queue type and bracket) and the announced match
// not queued yet. While a match is accepting (some of its bots in, some still queued), or while bots of it are left over
// in the queue, it counts twice - on purpose: that fails closed (with MaxBotBGPerBracket 2 or more the next start waits an
// interval longer), and a second match forming in that queue and bracket could not be told apart from left-over bots.
// Bounded: the own instances, the pending set and one ApplyAllBattleGrounds walk only while an own instance is known.
void RandomPlayerbotMgr::CountBgOwnPerBand(std::map<uint32, uint32>& perBand) const
{
    if (!bgOwnInstances.empty())
    {
        sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
        {
            if (!bg || !bg->GetInstanceID() || bg->GetStatus() == STATUS_WAIT_LEAVE)
                return;
            auto const found = bgOwnInstances.find(std::make_pair(uint32(bg->GetTypeID()), bg->GetInstanceID()));
            if (found != bgOwnInstances.end())
                ++perBand[found->second];
        });
    }
    std::set<std::pair<uint32, uint32>> forming;   // (queue type, bracket) with own bots queued
    for (auto const& [botGuid, entry] : bgFillPending)
    {
        if (entry.own)
            forming.emplace(entry.queueType, entry.bracket);
    }
    for (std::pair<uint32, uint32> const& key : forming)
        ++perBand[BgOwnBand(uint32(sServerFacade.BgTemplateId(BattleGroundQueueTypeId(key.first))), key.second)];
    if (bgOwnPlan.queueType)
        ++perBand[bgOwnPlan.band];
}

// Stage c (C4): an announcer must be in the world, alive, not in combat, not on a taxi or being teleported, without a
// real-player master, in no group (owner D2: a real player's group bots are never announced; the same rule as the
// stage-a candidate filter - a bot a real player invites during the stagger says nothing), and not a parked bot outside
// a capital city. The planner takes announcers only from its free candidates (never parked); this is checked again when
// the line is due. OB-10 review: nor a bot inside a battleground or an instance (dungeon, raid) - a call for another
// battleground from there would not read as organic.
bool RandomPlayerbotMgr::IsBgAnnouncerFit(Player* bot) const
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsTaxiFlying() || bot->IsBeingTeleported())
        return false;
    if (bot->InBattleGround() || bot->GetMap()->Instanceable())
        return false;
    if (bot->GetGroup())
        return false;
    PlayerbotAI* ai = GetBotAI(bot);
    if (!ai || ai->HasRealPlayerMaster())
        return false;
    if ((IsParkedBot(bot->GetGUIDLow()) || ai->IsParked()) && !ai->IsInCapitalCity())
        return false;
    return true;
}

// Stage c (review c2), once per interval from CheckBgFill while an own instance is known: one ApplyAllBattleGrounds walk
// drops the own instances the core no longer holds or that ended (WAIT_LEAVE). Bounded by MaxConcurrent.
void RandomPlayerbotMgr::PruneBgOwnInstances()
{
    std::set<std::pair<uint32, uint32>> running;
    sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
    {
        if (!bg || !bg->GetInstanceID() || bg->GetStatus() == STATUS_WAIT_LEAVE)
            return;
        std::pair<uint32, uint32> const key(uint32(bg->GetTypeID()), bg->GetInstanceID());
        if (bgOwnInstances.count(key))
            running.insert(key);
    });
    for (auto it = bgOwnInstances.begin(); it != bgOwnInstances.end();)
    {
        if (running.count(it->first))
            ++it;
        else
            it = bgOwnInstances.erase(it);
    }
}

// Stage c (review c2): the core tops up every running battleground of a type and bracket with free slots from the whole
// queue before it forms a new match (BattleGroundQueue::Update over BGFreeSlotQueue, FillPlayersToBG), so bots queued for
// an announced match there would go into that match instead - somebody else's, up to its maximum, which stage a avoids.
// The (bg type, bracket) of every such battleground (begun, not ended, a free slot), from one ApplyAllBattleGrounds walk.
void RandomPlayerbotMgr::CollectBgFreeSlotBrackets(std::set<std::pair<uint32, uint32>>& freeSlots) const
{
    sBattleGroundMgr.ApplyAllBattleGrounds([&](BattleGround const* bg)
    {
        if (!bg || !bg->GetInstanceID() || bg->GetStatus() <= STATUS_WAIT_QUEUE || bg->GetStatus() >= STATUS_WAIT_LEAVE ||
                !bg->HasFreeSlots())
            return;
        freeSlots.emplace(uint32(bg->GetTypeID()), uint32(bg->GetBracketId()));
    });
}

// Stage c (C4): a faction's pool for that battleground - its generic lines, then that battleground's own lines; a line in
// both counts once (review c2: never the same line twice in one announcement).
void RandomPlayerbotMgr::BuildBgAnnouncePool(uint32 queueType, std::array<std::vector<std::string>, 2>& pool) const
{
    for (uint32 team = 0; team < 2; ++team)
    {
        pool[team] = sPlayerbotAIConfig.bgAnnounceText[team][0];
        if (queueType >= 4)
            continue;
        for (std::string const& line : sPlayerbotAIConfig.bgAnnounceText[team][queueType])
        {
            if (std::find(pool[team].begin(), pool[team].end(), line) == pool[team].end())
                pool[team].push_back(line);
        }
    }
}

// Stage c (C4): the server message - one random line of each faction's pool, only to the manager's real players of that
// faction whose level is in the bracket (bounded by the real players; no walk over the sessions, no new text row in the database).
// Only the factions in sides (ai::bgfill::AnnounceSide bits): both from the planner, the unannounced ones from the
// mode-2 fallback (review c4). The caller checked that their pools have lines and MaxPerHour has room for one line each.
// Each faction's text goes through ai::bgfill::ExpandAnnounceText (owner Q2: the level range); a text that cannot go out
// (never with the load filter) sends nothing to any faction - all of them or none, fail closed.
// Returns the faction texts sent (counted against MaxPerHour).
uint32 RandomPlayerbotMgr::SendBgAnnounceServer(uint32 queueType, uint32 bgTypeId, uint32 bracket,
    std::array<std::vector<std::string>, 2> const& pool, uint32 sides)
{
    char const* const bgName = ai::bgfill::BgFullName(queueType);
    std::string const levelRange = BgAnnounceLevelRange(bgTypeId, bracket);
    BattleGroundTypeId const typeId = BattleGroundTypeId(bgTypeId);
    std::array<std::string, 2> texts;
    for (uint32 team = 0; team < 2; ++team)
    {
        if (!(sides & ai::bgfill::AnnounceSide(team)) || pool[team].empty())
            continue;
        texts[team] = ai::bgfill::ExpandAnnounceText(pool[team][urand(0, uint32(pool[team].size()) - 1)], bgName, levelRange,
            sPlayerbotAIConfig.bgAnnounceShowLevelRange);
        if (texts[team].empty())
            return 0;
    }
    uint32 lines = 0;
    for (uint32 team = 0; team < 2; ++team)
    {
        if (texts[team].empty())
            continue;
        std::string const& text = texts[team];
        Team const sideTeam = team ? HORDE : ALLIANCE;
        for (auto const& [playerGuid, player] : players)
        {
            if (!player || !player->GetSession() || player->GetTeam() != sideTeam)
                continue;
            if (PlayerbotAI* playerAi = GetBotAI(player))
                if (!playerAi->IsRealPlayer())
                    continue;
            if (!player->GetBGAccessByLevel(typeId) || uint32(player->GetBattleGroundBracketIdFromLevel(typeId)) != bracket)
                continue;
            ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
        }
        ++lines;
    }
    return lines;
}

// Stage c (C2/C3/C4), from CheckBgFill once per IntervalMinutes (one try per interval, a start or not) while no announced
// match waits, after the scans of that interval:
// - MaxPerHour first (no line, no bot-own battleground: fail closed), then the global cap (running bot battlegrounds and
//   forming brackets below MaxConcurrent) and per type and bracket MaxBotBGPerBracket per level range (owner C0), the
//   battleground team cap 0, the AV queue minimum, a slot reserved for a waiting real player (stage b) and a running
//   battleground of that type and bracket with a free slot (the core would top it up with the bots first);
// - C2: one bounded loop over the bots from a rotating start collects free roster bots (the stage-a candidate filter: the
//   forced trip, a real player's group, parked, dead, quest rescue, far teleport, ...) per type, bracket and side, at most
//   OwnCandidateLimit each, stopping once every open list is full and at most after OwnCandidateVisitMax bots - no AI
//   values, no DB bot list;
// - C3: WSG, AB or AV (BR excluded, SV after stage d) with a bracket where both sides have the template minimum plus their
//   share of the announcers (OwnCandidatesNeeded), the type started longest ago first (PickOwnType), its bracket with the
//   most candidates;
// - C4: 2/3/5 announcers from those candidates, alternating sides and at least one of each, out of their
//   BotCooldownMinutes, each with a distinct line of its faction's pool (generic plus that battleground's lines, each
//   line once), into the due-queue staggered 2-15 s (drained by DrainBgAnnounce); server mode (or the fallback without an
//   announcer of each side): one system message per faction text, only to the manager's real players of that faction
//   whose level is in the bracket (SendBgAnnounceServer). The [BGAnnounce] line is always written, once
//   per announcement, bounded by MaxPerHour.
// The plan keeps the candidates (announcers first); QueueBgOwn queues them DelaySeconds later.
void RandomPlayerbotMgr::PlanBgOwn(uint32 nowMs)
{
    bool const trace = sPlayerbotAIConfig.bgTrace;
    if (bgOwnPlan.queueType)
        return;
    if (!ai::bgfill::PlannerDue(nowMs, lastBgOwnPlanMs, ai::bgfill::AnnounceIntervalMs(sPlayerbotAIConfig.bgAnnounceIntervalMinutes)))
        return;
    lastBgOwnPlanMs = nowMs ? nowMs : 1;

    // MaxPerHour: the hour window, then room for one line per faction (0 = none at all; below 2 counts as 0 at load).
    if (ai::bgfill::AnnounceHourOver(nowMs, bgAnnounceHourStart))
    {
        bgAnnounceHourStart = nowMs ? nowMs : 1;
        bgAnnounceHourLines = 0;
    }
    if (ai::bgfill::AnnounceLinesAllowed(bgAnnounceHourLines, sPlayerbotAIConfig.bgAnnounceMaxPerHour, ai::bgfill::AnnounceMinLines) <
            ai::bgfill::AnnounceMinLines)
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=announcecap lines=%u max=%u", bgAnnounceHourLines, sPlayerbotAIConfig.bgAnnounceMaxPerHour);
        return;
    }

    // The global cap: running bot battlegrounds (own ones included) and brackets forming (fill and own bots queued).
    std::set<uint32> formingKeys;
    for (auto const& [botGuid, entry] : bgFillPending)
    {
        if (!entry.pairFor)
            formingKeys.insert(BgFillBracketKey(entry.queueType, entry.bracket));
    }
    uint32 const running = CountBgFillRunning() + uint32(formingKeys.size());
    if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=cap running=%u max=%u own=1", running, sPlayerbotAIConfig.bgMaxConcurrent);
        return;
    }

    // Per type and bracket: may a bot-own match form there? Not where a running battleground of that type and bracket has
    // a free slot: the core would put the bots into it first (review c2, CollectBgFreeSlotBrackets).
    std::map<uint32, uint32> ownPerBand;
    CountBgOwnPerBand(ownPerBand);
    std::set<std::pair<uint32, uint32>> freeSlots;
    CollectBgFreeSlotBrackets(freeSlots);
    uint32 freeSlotSkips = 0;
    uint32 const ownTypes[3] = { uint32(BATTLEGROUND_QUEUE_WS), uint32(BATTLEGROUND_QUEUE_AB), uint32(BATTLEGROUND_QUEUE_AV) };
    uint32 minPerTeam[MAX_BATTLEGROUND_QUEUE_TYPES] = {};
    bool open[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS] = {};
    uint32 openSlots = 0;
    for (uint32 q : ownTypes)
    {
        BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(BattleGroundQueueTypeId(q));
        BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
        if (!bgTemplate || !ai::bgfill::AnnouncerCount(q) || !bgTemplate->GetMinPlayersPerTeam())
            continue;
        // AiPlayerbot.BgBotTeamCap 0 takes a battleground away from bots entirely; the AV queue minimum above the template
        // minimum would make the core wait for more entries than the match takes (as in stage a).
        if (sPlayerbotAIConfig.GetBgBotTeamCap(uint32(bgTypeId)) == 0)
            continue;
        if (bgTypeId == BATTLEGROUND_AV &&
                ai::bgfill::QueueMinBlocksFill(bgTemplate->GetMinPlayersPerTeam(), sWorld.getConfig(CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE)))
            continue;
        minPerTeam[q] = bgTemplate->GetMinPlayersPerTeam();
        for (uint32 b = BG_BRACKET_ID_FIRST; b < MAX_BATTLEGROUND_BRACKETS; ++b)
        {
            if (!ai::bgfill::OwnBandAllows(ownPerBand[BgOwnBand(uint32(bgTypeId), b)], sPlayerbotAIConfig.bgMaxBotBGPerBracket))
                continue;
            if (BgFillReserved(q, b, 0, nowMs) || BgFillReserved(q, b, 1, nowMs))
                continue;
            if (freeSlots.count(std::make_pair(uint32(bgTypeId), b)))
            {
                ++freeSlotSkips;
                continue;
            }
            open[q][b] = true;
            ++openSlots;
        }
    }
    if (!openSlots)
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=bracketcap own=1 freeslots=%u", freeSlotSkips);
        return;
    }

    // C2: the free roster bots per type, bracket and side - one bounded loop from a rotating start.
    std::array<std::vector<uint32>, 2> cands[MAX_BATTLEGROUND_QUEUE_TYPES][MAX_BATTLEGROUND_BRACKETS];
    uint32 visited = 0;
    // The loop stops once every open list (type, bracket, side) is full (C2: stop at the need), else at
    // OwnCandidateVisitMax.
    uint32 listsWithRoom = openSlots * 2;
    auto const visit = [&](uint32 guid, Player* bot)
    {
        bgOwnRotate = guid;
        ++visited;
        if (!IsBgFillCandidate(bot))
            return;
        uint32 const team = bot->GetTeam() == ALLIANCE ? 0 : 1;
        for (uint32 q : ownTypes)
        {
            if (!minPerTeam[q])
                continue;
            BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(BattleGroundQueueTypeId(q));
            // The core's bracket and join checks (HandleBattlemasterJoinOpcode): level range, not queued for it yet.
            if (!bot->GetBGAccessByLevel(bgTypeId) || bot->InBattleGroundQueueForBattleGroundQueueType(BattleGroundQueueTypeId(q)))
                continue;
            BattleGroundBracketId const bracketId = bot->GetBattleGroundBracketIdFromLevel(bgTypeId);
            if (bracketId == BG_BRACKET_ID_NONE || bracketId >= MAX_BATTLEGROUND_BRACKETS || !open[q][bracketId])
                continue;
            std::vector<uint32>& list = cands[q][bracketId][team];
            uint32 const limit = ai::bgfill::OwnCandidateLimit(minPerTeam[q], q);
            if (list.size() < limit)
            {
                list.push_back(guid);
                if (list.size() == limit && listsWithRoom)
                    --listsWithRoom;
            }
        }
    };
    PlayerBotMap const& bots = GetAllBots();
    PlayerBotMap::const_iterator const start = bots.upper_bound(bgOwnRotate);
    for (auto it = start; it != bots.end() && visited < ai::bgfill::OwnCandidateVisitMax && listsWithRoom; ++it)
        visit(it->first, it->second);
    for (auto it = bots.begin(); it != start && visited < ai::bgfill::OwnCandidateVisitMax && listsWithRoom; ++it)
        visit(it->first, it->second);

    // C3: the types with a bracket where both sides reach the template minimum plus their share of the announcers
    // (OwnCandidatesNeeded; that bracket: the most candidates, then the lowest), the type started longest ago first.
    std::vector<uint32> possible;
    uint32 bestBracket[MAX_BATTLEGROUND_QUEUE_TYPES] = {};
    for (uint32 q : ownTypes)
    {
        uint32 best = MAX_BATTLEGROUND_BRACKETS;
        size_t bestHave = 0;
        for (uint32 b = BG_BRACKET_ID_FIRST; b < MAX_BATTLEGROUND_BRACKETS; ++b)
        {
            if (!open[q][b])
                continue;
            size_t const have = std::min(cands[q][b][0].size(), cands[q][b][1].size());
            if (have < ai::bgfill::OwnCandidatesNeeded(minPerTeam[q], q))
                continue;
            if (best == MAX_BATTLEGROUND_BRACKETS || have > bestHave)
            {
                best = b;
                bestHave = have;
            }
        }
        if (best == MAX_BATTLEGROUND_BRACKETS)
            continue;
        possible.push_back(q);
        bestBracket[q] = best;
    }
    uint32 const queueType = ai::bgfill::PickOwnType(bgOwnTypes, possible);
    if (!queueType)
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=nobots own=1 visited=%u", visited);
        return;
    }
    uint32 const bracket = bestBracket[queueType];
    BattleGroundTypeId const bgTypeId = sServerFacade.BgTemplateId(BattleGroundQueueTypeId(queueType));
    std::array<std::vector<uint32>, 2>& chosen = cands[queueType][bracket];

    // C4: the faction pools for this battleground (generic plus its own lines, each line once) and the announcers. Both
    // factions or none (review c2): a faction without a line skips the announcement (the config falls back to the
    // built-in pool, so this is a guard).
    std::array<std::vector<std::string>, 2> pool;
    BuildBgAnnouncePool(queueType, pool);
    if (pool[0].empty() || pool[1].empty())
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=nopool bg=%u bracket=%u", uint32(bgTypeId), bracket);
        return;
    }
    uint32 const cooldownMs = ai::bgfill::AnnounceCooldownMs(sPlayerbotAIConfig.bgAnnounceBotCooldownMinutes);
    for (auto it = bgAnnounceCooldown.begin(); it != bgAnnounceCooldown.end();)
    {
        if (ai::bgfill::PendingExpired(nowMs, it->second, cooldownMs))
            it = bgAnnounceCooldown.erase(it);
        else
            ++it;
    }
    uint32 const mode = ai::bgfill::AnnounceMode(sPlayerbotAIConfig.bgAnnounceMode);
    uint32 const wanted = ai::bgfill::AnnouncerCount(queueType);
    std::vector<std::pair<uint32, uint32>> announcers;   // (guid, team)
    std::array<uint32, 2> perTeam = { 0, 0 };
    if (mode != ai::bgfill::AnnounceModeServer)
    {
        uint32 const firstTeam = urand(0, 1);
        std::array<size_t, 2> next = { 0, 0 };
        for (uint32 i = 0; i < wanted * 2 && announcers.size() < wanted; ++i)
        {
            uint32 const team = ai::bgfill::AnnouncerTeam(i, firstTeam);
            // The same line never twice in one announcement: no more announcers of a side than its pool has lines.
            if (perTeam[team] >= pool[team].size())
                continue;
            while (next[team] < chosen[team].size())
            {
                uint32 const guid = chosen[team][next[team]++];
                if (bgAnnounceCooldown.count(guid) || !IsBgAnnouncerFit(GetPlayerBot(guid)))
                    continue;
                announcers.emplace_back(guid, team);
                ++perTeam[team];
                break;
            }
        }
        // Per faction (owner D3, review c2): chat needs an announcer of each side, else it counts as no announcer
        // (mode 2: the server message; mode 0: no announcement).
        if (!perTeam[0] || !perTeam[1])
        {
            announcers.clear();
            perTeam = { 0, 0 };
        }
    }
    bool const server = ai::bgfill::AnnounceUsesServer(mode, uint32(announcers.size()));
    uint32 const allowed = ai::bgfill::AnnounceLinesAllowed(bgAnnounceHourLines, sPlayerbotAIConfig.bgAnnounceMaxPerHour,
        server ? 2 : uint32(announcers.size()));
    // A bot-own battleground only with an announcement, to both factions or to none: the server message needs room for
    // two lines, chat for one announcer of each side.
    if (allowed < ai::bgfill::AnnounceMinLines)
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=%s bg=%u bracket=%u", (allowed || server) ? "announcecap" : "noannouncer",
                uint32(bgTypeId), bracket);
        return;
    }
    uint32 lines = 0;
    if (server)
    {
        announcers.clear();
        lines = SendBgAnnounceServer(queueType, uint32(bgTypeId), bracket, pool, ai::bgfill::AnnounceSidesBoth);
    }
    else
    {
        // The server helper names its own range; the chat lines use this one (owner Q2).
        char const* const bgName = ai::bgfill::BgFullName(queueType);
        std::string const levelRange = BgAnnounceLevelRange(uint32(bgTypeId), bracket);
        // Cut to MaxPerHour: the announcers alternate sides from the first one on, so the first two are one of each.
        if (announcers.size() > allowed)
            announcers.resize(allowed);
        perTeam = { 0, 0 };
        for (auto const& [guid, team] : announcers)
            ++perTeam[team];
        std::array<uint32, 2> said = { 0, 0 };
        std::array<std::vector<uint32>, 2> pick;
        for (uint32 team = 0; team < 2; ++team)
        {
            std::vector<uint32> rolls;
            for (uint32 i = 0; i < perTeam[team]; ++i)
                rolls.push_back(urand(0, 0x7FFFFFFF));
            pick[team] = ai::bgfill::PickLineIndexes(uint32(pool[team].size()), perTeam[team], rolls);
        }
        for (auto const& [guid, team] : announcers)
        {
            if (said[team] >= pick[team].size() || bgAnnounceDue.size() >= ai::bgfill::MaxAnnounceDue)
                continue;
            BgAnnounceDue due;
            due.guid = guid;
            due.team = team;
            due.dueMs = nowMs + urand(ai::bgfill::AnnounceStaggerMinMs, ai::bgfill::AnnounceStaggerMaxMs);
            due.text = ai::bgfill::ExpandAnnounceText(pool[team][pick[team][said[team]++]], bgName, levelRange,
                sPlayerbotAIConfig.bgAnnounceShowLevelRange);
            // A text that cannot go out (never with the load filter) is not said and not counted (fail closed).
            if (due.text.empty())
                continue;
            bgAnnounceDue.push_back(due);
            bgAnnounceCooldown[guid] = nowMs ? nowMs : 1;
            ++lines;
        }
    }
    if (!lines)
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=noannouncer bg=%u bracket=%u", uint32(bgTypeId), bracket);
        return;
    }
    bgAnnounceHourLines += lines;
    // The measurement point of every announcement, always written (bounded by MaxPerHour). Chat lines are the planned
    // ones; a faction none of whose lines is said gets the mode-2 fallback line (DrainBgAnnounce) or a second line with
    // lines=0 at the queue step (QueueBgOwn, reason=unannounced).
    sLog.outBasic("[BGAnnounce] bg=%u bracket=%u bots=%u mode=%s lines=%u", uint32(bgTypeId), bracket, uint32(announcers.size()),
        server ? "server" : "chat", lines);

    // The plan: queue the minimum per side DelaySeconds from now, from these candidates (the announcers first, so the
    // bots that announced play).
    bgOwnPlan = BgOwnPlan();
    bgOwnPlan.queueType = queueType;
    bgOwnPlan.bgTypeId = uint32(bgTypeId);
    bgOwnPlan.bracket = bracket;
    bgOwnPlan.band = BgOwnBand(uint32(bgTypeId), bracket);
    bgOwnPlan.queueAtMs = nowMs + ai::bgfill::AnnounceDelayMs(sPlayerbotAIConfig.bgAnnounceDelaySeconds);
    bgOwnPlan.server = { server, server };
    for (uint32 team = 0; team < 2; ++team)
    {
        std::vector<uint32>& list = bgOwnPlan.candidates[team];
        for (auto const& [guid, side] : announcers)
        {
            if (side == team)
                list.push_back(guid);
        }
        for (uint32 guid : chosen[team])
        {
            if (std::find(list.begin(), list.end(), guid) == list.end())
                list.push_back(guid);
        }
    }
    bgOwnTypes.push_back(queueType);
    if (bgOwnTypes.size() > ai::bgfill::OwnTypeHistory)
        bgOwnTypes.erase(bgOwnTypes.begin());
    if (trace)
        sLog.outBasic("[BGFill] state=announce bg=%u bracket=%u band=%u cand_a=%u cand_h=%u queue_in_s=%u", uint32(bgTypeId), bracket,
            bgOwnPlan.band, uint32(chosen[0].size()), uint32(chosen[1].size()),
            ai::bgfill::AnnounceDelayMs(sPlayerbotAIConfig.bgAnnounceDelaySeconds) / IN_MILLISECONDS);
}

// Stage c (C3), once the announced match's DelaySeconds ran out (CheckBgFill runs a fresh scan first): nobody queues
// unless the start was really announced to each faction (a chat line of it said or the server message to it; else
// reason=unannounced). Then both sides queue up to the template minimum through the stage-a join helper - the real
// players' entries of that queue and bracket and the fill bots already queued for it count, so a real player who queued
// for the announced type gets the stage-a fill and only the rest is queued here. The caps again (MaxConcurrent with this bracket counted once,
// MaxBotBGPerBracket), no queue while a slot there is reserved for a waiting real player or a running battleground of
// that type and bracket has a free slot (reason=freeslots). The plan's bots go through the stage-a candidate filter and
// the core's join checks again: if a side cannot reach its need any more, nobody queues (reason=nobots; the
// announcement stays without a match, MaxPerHour bounds that). A join the core refuses takes back the joins of this
// match. The queued bots are fill bots marked own (bgFillPending): not invited within PendingMaxSeconds they leave as in
// stage a; accepted into a new instance it is started for the own match (startedForOwn, never startedForFill).
void RandomPlayerbotMgr::QueueBgOwn(uint32 nowMs)
{
    bool const trace = sPlayerbotAIConfig.bgTrace;
    BgOwnPlan const plan = bgOwnPlan;
    bgOwnPlan = BgOwnPlan();
    BattleGroundQueueTypeId const queueTypeId = BattleGroundQueueTypeId(plan.queueType);
    BattleGroundTypeId const bgTypeId = BattleGroundTypeId(plan.bgTypeId);
    BattleGroundBracketId const bracketId = BattleGroundBracketId(plan.bracket);
    BattleGround const* bgTemplate = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
    if (!bgTemplate || !ai::bgfill::IsFillQueueType(plan.queueType) || plan.bracket >= MAX_BATTLEGROUND_BRACKETS)
        return;

    // Owner contract: bots play a battleground by themselves only if its start was announced - to each faction (owner D3,
    // review c4). A faction none of whose chat lines was really said (its announcers got unfit before their lines were
    // due, DrainBgAnnounce) and that no server message reached: nobody queues (review c2). The [BGAnnounce] line with
    // lines=0 names the unannounced side (once per plan: the plan is used once; bounded by MaxPerHour).
    uint32 const unannounced = ai::bgfill::AnnounceSidesMissing(plan.said[0], plan.said[1], plan.server[0], plan.server[1]);
    if (unannounced)
    {
        sLog.outBasic("[BGAnnounce] bg=%u bracket=%u bots=0 mode=chat lines=0 reason=unannounced side=%s", plan.bgTypeId, plan.bracket,
            ai::bgfill::AnnounceSidesName(unannounced));
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=unannounced own=1 bg=%u bracket=%u said_a=%u said_h=%u", plan.bgTypeId, plan.bracket,
                plan.said[0], plan.said[1]);
        return;
    }

    // Bots already queued for that queue and bracket per side (fill and own, no pair bots) and the brackets forming.
    uint32 pendingSide[2] = { 0, 0 };
    std::set<uint32> formingKeys;
    uint32 const planKey = BgFillBracketKey(plan.queueType, plan.bracket);
    for (auto const& [botGuid, entry] : bgFillPending)
    {
        if (entry.pairFor)
            continue;
        uint32 const key = BgFillBracketKey(entry.queueType, entry.bracket);
        formingKeys.insert(key);
        if (key == planKey)
            ++pendingSide[entry.team];
    }
    char const* skip = nullptr;
    uint32 const running = ai::bgfill::RunningForBracket(CountBgFillRunning(), uint32(formingKeys.size()), formingKeys.count(planKey) != 0);
    std::map<uint32, uint32> ownPerBand;
    CountBgOwnPerBand(ownPerBand);
    std::set<std::pair<uint32, uint32>> freeSlots;
    CollectBgFreeSlotBrackets(freeSlots);
    if (!ai::bgfill::CapAllows(running, sPlayerbotAIConfig.bgMaxConcurrent))
        skip = "cap";
    else if (!ai::bgfill::OwnBandAllows(ownPerBand[plan.band], sPlayerbotAIConfig.bgMaxBotBGPerBracket))
        skip = "bracketcap";
    else if (BgFillReserved(plan.queueType, plan.bracket, 0, nowMs) || BgFillReserved(plan.queueType, plan.bracket, 1, nowMs))
        skip = "reserved";
    // A battleground of that type and bracket with a free slot started meanwhile: the core would top it up with these
    // bots instead of forming the announced match (review c2).
    else if (freeSlots.count(std::make_pair(plan.bgTypeId, plan.bracket)))
        skip = "freeslots";
    if (skip)
    {
        // The call was answered by a running battleground with a free slot (usually the stage-a match of a real player
        // who answered it): no bot-own match forms. Always written like an unannounced plan, so the [BGAnnounce] lines
        // show it (review c4); once per plan, the plan is used once.
        if (std::string(skip) == "freeslots")
            sLog.outBasic("[BGAnnounce] bg=%u bracket=%u bots=0 mode=%s lines=0 reason=freeslots", plan.bgTypeId, plan.bracket,
                plan.server[0] && plan.server[1] ? "server" : "chat");
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=%s own=1 bg=%u bracket=%u", skip, plan.bgTypeId, plan.bracket);
        return;
    }

    uint32 const minPerTeam = bgTemplate->GetMinPlayersPerTeam();
    uint32 const need[2] = { ai::bgfill::NeedPerSide(minPerTeam, plan.real[0], pendingSide[0]),
        ai::bgfill::NeedPerSide(minPerTeam, plan.real[1], pendingSide[1]) };
    if (trace)
        sLog.outBasic("[BGFill] state=own_queue bg=%u bracket=%u real_a=%u real_h=%u pending_a=%u pending_h=%u need_a=%u need_h=%u",
            plan.bgTypeId, plan.bracket, plan.real[0], plan.real[1], pendingSide[0], pendingSide[1], need[0], need[1]);
    if (!need[0] && !need[1])
        return;

    // The plan's bots again: still free, still in the bracket, not queued for it. A side short of its need: nobody.
    std::array<std::vector<Player*>, 2> joiners;
    for (uint32 team = 0; team < 2; ++team)
    {
        for (uint32 guid : plan.candidates[team])
        {
            if (joiners[team].size() >= need[team])
                break;
            Player* bot = GetPlayerBot(guid);
            if (!bot || !IsBgFillCandidate(bot) || (bot->GetTeam() == ALLIANCE ? 0u : 1u) != team)
                continue;
            if (!bot->GetBGAccessByLevel(bgTypeId) || bot->GetBattleGroundBracketIdFromLevel(bgTypeId) != bracketId ||
                    bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))
                continue;
            joiners[team].push_back(bot);
        }
    }
    if (joiners[0].size() < need[0] || joiners[1].size() < need[1])
    {
        if (trace)
            sLog.outBasic("[BGFill] state=skip reason=nobots own=1 bg=%u bracket=%u have_a=%u have_h=%u need_a=%u need_h=%u", plan.bgTypeId,
                plan.bracket, uint32(joiners[0].size()), uint32(joiners[1].size()), need[0], need[1]);
        return;
    }

    // Join on the world thread as stage a does (no Refresh: roster bots keep their own economy).
    std::vector<uint32> joined;
    bool refused = false;
    for (uint32 team = 0; team < 2 && !refused; ++team)
    {
        for (Player* bot : joiners[team])
        {
            PlayerbotAI* ai = GetBotAI(bot);
            BGJoinAction::SendBattlemasterJoin(ai, bot, bgTypeId, false);
            if (!bot->InBattleGroundQueueForBattleGroundQueueType(queueTypeId))
            {
                refused = true;
                break;
            }
            ai->SetBgFillPending(true);
            BgFillEntry entry;
            entry.queueType = plan.queueType;
            entry.bgTypeId = plan.bgTypeId;
            entry.bracket = plan.bracket;
            entry.team = team;
            entry.joinMs = nowMs;
            entry.own = true;
            bgFillPending[bot->GetGUIDLow()] = entry;
            joined.push_back(bot->GetGUIDLow());
            ++bgFillMinute.joins;
            if (trace)
                sLog.outBasic("[BGFill] state=own_queue bot=%u bg=%u bracket=%u side=%s", bot->GetGUIDLow(), plan.bgTypeId, plan.bracket,
                    team ? "H" : "A");
        }
    }
    if (!refused)
        return;
    // The core refused a join (its own checks): the match would start short, so the bots of this match leave again; one
    // the core does not let out stays pending and leaves as any fill bot (expired).
    for (uint32 botGuid : joined)
    {
        auto const pending = bgFillPending.find(botGuid);
        Player* bot = GetPlayerBot(botGuid);
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (pending != bgFillPending.end() && ai && LeaveBgFillQueue(bot, ai, botGuid, pending->second, "ownshort"))
            bgFillPending.erase(pending);
    }
    if (trace)
        sLog.outBasic("[BGFill] state=skip reason=refused own=1 bg=%u bracket=%u", plan.bgTypeId, plan.bracket);
}

// Stage c (C4), every manager update from CheckBgFill - the world thread, and the only place an announcement line is
// said: each due line (at most MaxAnnounceDue) is taken off the queue; its bot is resolved and checked again
// (IsBgAnnouncerFit), joins the World channel if needed (GetOrCreateChannel and Join on the world thread, as the join
// opcode does) and its line goes out through Channel::AsyncSay - the ChannelBroadcaster queue, whose producer the world
// thread already is (the opcode path), so no new producer thread. Never the synchronous channel say or the bot's own
// world-chat helpers from here. A bot that no longer qualifies says nothing. Each line said counts for its faction in the
// announced match (bgOwnPlan.said per faction; one plan at a time, its lines are due 2-15 s after it, before its queue
// join, so the counts are final then). Once the last due line is gone, mode 2 sends the server message to each faction
// none of whose lines was said (once per plan, within MaxPerHour, review c2/c4); otherwise QueueBgOwn queues nobody for
// a match not announced to both factions.
// World membership (OB-10 review): a free random bot is already on World from its login (PlayerbotHolder::
// JoinChatChannels, IsFreeBot), and every announcer is a free bot, so the Join below is normally a no-op and the
// announcer stays a member exactly as before. Bots answer channel lines only through the existing chat-reply path
// (PlayerbotAI chat handling, its reply chances and chat cooldown), the same as for any World line today; stage c adds no
// reply path and no membership a free bot would not have anyway.
void RandomPlayerbotMgr::DrainBgAnnounce(uint32 nowMs)
{
    if (bgAnnounceDue.empty())
        return;
    for (auto it = bgAnnounceDue.begin(); it != bgAnnounceDue.end();)
    {
        if (!ai::bgfill::DeadlinePassed(nowMs, it->dueMs))
        {
            ++it;
            continue;
        }
        BgAnnounceDue const due = *it;
        it = bgAnnounceDue.erase(it);
        Player* bot = GetPlayerBot(due.guid);
        ChannelMgr* cMgr = bot && IsBgAnnouncerFit(bot) ? channelMgr(bot->GetTeam()) : nullptr;
        Channel* world = cMgr ? cMgr->GetOrCreateChannel("World") : nullptr;
        if (!world)
        {
            if (sPlayerbotAIConfig.bgTrace)
                sLog.outBasic("[BGFill] state=announce bot=%u said=0 reason=%s", due.guid, cMgr ? "nochannel" : "unfit");
            continue;
        }
        PlayerbotAI* const botAi = GetBotAI(bot);
        if (!botAi || !botAi->IsOnChannel(world))
            world->Join(bot->GetObjectGuid(), "");
        world->AsyncSay(bot->GetObjectGuid(), due.text.c_str(), LANG_UNIVERSAL);
        if (bgOwnPlan.queueType)
            ++bgOwnPlan.said[due.team ? 1 : 0];
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=announce bot=%u said=1 text=%s", due.guid, due.text.c_str());
    }
    if (!bgAnnounceDue.empty() || !bgOwnPlan.queueType || bgOwnPlan.fallbackTried ||
            ai::bgfill::AnnounceMode(sPlayerbotAIConfig.bgAnnounceMode) != ai::bgfill::AnnounceModeFallback)
        return;
    // Mode 2 fallback at due time, tried once per plan (review c4: per faction): the server message to each faction none
    // of whose chat lines was said - all of those factions or none, within MaxPerHour (one line each).
    uint32 const missing = ai::bgfill::AnnounceSidesMissing(bgOwnPlan.said[0], bgOwnPlan.said[1], bgOwnPlan.server[0], bgOwnPlan.server[1]);
    if (!missing)
        return;
    bgOwnPlan.fallbackTried = true;
    uint32 const wanted = ai::bgfill::AnnounceSideCount(missing);
    std::array<std::vector<std::string>, 2> pool;
    BuildBgAnnouncePool(bgOwnPlan.queueType, pool);
    if (((missing & ai::bgfill::AnnounceSideAlliance) && pool[0].empty()) || ((missing & ai::bgfill::AnnounceSideHorde) && pool[1].empty()) ||
            ai::bgfill::AnnounceLinesAllowed(bgAnnounceHourLines, sPlayerbotAIConfig.bgAnnounceMaxPerHour, wanted) < wanted)
    {
        if (sPlayerbotAIConfig.bgTrace)
            sLog.outBasic("[BGFill] state=skip reason=announcecap fallback=1 bg=%u bracket=%u side=%s", bgOwnPlan.bgTypeId, bgOwnPlan.bracket,
                ai::bgfill::AnnounceSidesName(missing));
        return;
    }
    uint32 const lines = SendBgAnnounceServer(bgOwnPlan.queueType, bgOwnPlan.bgTypeId, bgOwnPlan.bracket, pool, missing);
    bgAnnounceHourLines += lines;
    if (lines == wanted)
    {
        for (uint32 team = 0; team < 2; ++team)
        {
            if (missing & ai::bgfill::AnnounceSide(team))
                bgOwnPlan.server[team] = true;
        }
    }
    sLog.outBasic("[BGAnnounce] bg=%u bracket=%u bots=0 mode=server lines=%u side=%s", bgOwnPlan.bgTypeId, bgOwnPlan.bracket, lines,
        ai::bgfill::AnnounceSidesName(missing));
}

// twow-repo#517 stage c (owner decision C0, AiPlayerbot.BG.BotOnlyHonorRate, only with BG.Enable and a rate that really
// reduces): a bot in a battleground without a real player inside gets honor x rate - bonus and kill honor; its rank points
// follow (HonorMgr::Add credits them from the scaled value). Marks, the kill count, experience and reputation stay
// normal; with a real player inside everybody gets the normal honor, and real players always do. Called from
// PlayerbotPlayerScript::OnHonorGain, i.e. HonorMgr::Add on the bot's map thread, which is its battleground's map. It
// stays map-local (review c2): the battleground comes from the map itself (BattleGroundMap::GetBG, no BattleGroundMgr
// lookup), and one pass over this map's own player list (at most 2 x 40) checks that every player on it is a bot of that
// battleground; with as many of them as the battleground has players (GetPlayersSize), the two sets are the same, so no
// participant is elsewhere (an offline or teleporting real player stays in the battleground's list). No manager state,
// no lock, no global lookup. Fail closed: anybody else on the map, a count mismatch, or the bot not in that battleground
// keeps the normal honor.
void RandomPlayerbotMgr::ScaleBotOnlyHonor(Player* player, float& cp, uint8 type)
{
    if (!sPlayerbotAIConfig.bgEnable || !ai::bgfill::HonorRateActive(sPlayerbotAIConfig.bgBotOnlyHonorRate))
        return;
    if (!player || type == DISHONORABLE || cp <= 0.0f)
        return;
    PlayerbotAI* ai = GetBotAI(player);
    if (!ai || ai->IsRealPlayer() || !player->InBattleGround())
        return;
    Map* map = player->FindMap();
    if (!map || !map->IsBattleGround())
        return;
    BattleGround* bg = static_cast<BattleGroundMap*>(map)->GetBG();
    if (!bg || bg->GetInstanceID() != player->GetBattleGroundId())
        return;
    // A real player among the battleground's players, or anybody on the map who is not one of them: normal honor.
    BattleGround::BattleGroundPlayerMap const& participants = bg->GetPlayers();
    uint32 onMap = 0;
    for (auto const& ref : map->GetPlayers())
    {
        Player* member = ref.getSource();
        if (!member || !participants.count(member->GetObjectGuid()))
            return;
        PlayerbotAI* memberAi = GetBotAI(member);
        if (!memberAi || memberAi->IsRealPlayer())
            return;
        ++onMap;
    }
    if (onMap != bg->GetPlayersSize())
        return;
    float const before = cp;
    cp = ai::bgfill::ScaleBotOnlyHonor(cp, sPlayerbotAIConfig.bgBotOnlyHonorRate);
    if (sPlayerbotAIConfig.bgTrace)
        sLog.outBasic("[BGFill] state=honor_rate player=%u cp_before=%.2f cp_after=%.2f", player->GetGUIDLow(), before, cp);
}

void RandomPlayerbotMgr::CheckBgQueue()
{
    if (!BgCheckTimer)
        BgCheckTimer = time(nullptr);

    if (time(nullptr) < (BgCheckTimer + 30))
    {
        return;
    }
    else
    {
        BgCheckTimer = time(nullptr);
    }

    sLog.outDetail("Checking BG Queue...");

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BgPlayers[j][i][0] = 0;
            BgPlayers[j][i][1] = 0;
            BgBots[j][i][0] = 0;
            BgBots[j][i][1] = 0;
            ArenaBots[j][i][0][0] = 0;
            ArenaBots[j][i][0][1] = 0;
            ArenaBots[j][i][1][0] = 0;
            ArenaBots[j][i][1][1] = 0;
            NeedBots[j][i][0] = false;
            NeedBots[j][i][1] = false;
        }
    }

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        if (!player->InBattleGroundQueue())
            continue;

        if (player->InBattleGround() && player->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            continue;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = player->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = player->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, player->GetLevel());
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, player->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;

                if (bgQueue.GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                {
                    if (ginfo.isRated)
                    {
                        for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                        {
                            uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                            ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                            if (!arenateam)
                                continue;
                            if (arenateam->GetType() != arenaType)
                                continue;

                            Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                        }
                    }
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (player->InArena())
                {
                    if (player->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
         */
#endif
#ifdef MANGOSBOT_ONE
            if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, playerId = player->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *player = RandomPlayerbotMgr::instance().GetPlayer(playerId);

                        if (!player)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(player->GetObjectGuid(), &ginfo))
                        {
                            if (ginfo.isRated)
                            {
                                for (uint32 arena_slot = 0; arena_slot < MAX_ARENA_SLOT; ++arena_slot)
                                {
                                    uint32 arena_team_id = player->GetArenaTeamId(arena_slot);
                                    ArenaTeam* arenateam = sObjectMgr.GetArenaTeamById(arena_team_id);
                                    if (!arenateam)
                                        continue;
                                    if (arenateam->GetType() != arenaType)
                                        continue;

                                    sRandomPlayerbotMgr.Rating[queueTypeId][bracketId][1] = arenateam->GetRating();
                                }
                            }
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (player->InArena())
                        {
                            if (player->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }
                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            if (GetBotAI(player))
                BgBots[queueTypeId][bracketId][TeamId]++;
            else
                BgPlayers[queueTypeId][bracketId][TeamId]++;

            if (!player->IsInvitedForBattleGroundQueueType(queueTypeId) && (!player->InBattleGround() || player->GetBattleGround()->GetTypeId() != sServerFacade.BgTemplateId(queueTypeId)))
            {
#ifndef MANGOSBOT_ZERO
                if (ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId))
                {
                    NeedBots[queueTypeId][bracketId][TeamId] = true;
                }
                else
                {
                    NeedBots[queueTypeId][bracketId][0] = true;
                    NeedBots[queueTypeId][bracketId][1] = true;
                }
#else
                NeedBots[queueTypeId][bracketId][0] = true;
                NeedBots[queueTypeId][bracketId][1] = true;
#endif
            }
        }
    }

    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (!bot->InBattleGroundQueue())
            return;

        if (!IsFreeBot(bot))
            return;

        if (bot->InBattleGround() && bot->GetBattleGround() && bot->GetBattleGround()->GetStatus() == STATUS_WAIT_LEAVE)
            return;

        for (int i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattleGroundQueueTypeId queueTypeId = bot->GetBattleGroundQueueTypeId(i);
            if (queueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;

            uint32 TeamId = bot->GetTeam() == ALLIANCE ? 0 : 1;

            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);

#ifndef MANGOSBOT_TWO
            BattleGroundBracketId bracketId = sBattleGroundMgr.GetBattleGroundBracketIdFromLevel(bgTypeId, bot->GetLevel());;
#endif
#ifdef MANGOSBOT_TWO
            BattleGround* bg = sBattleGroundMgr.GetBattleGroundTemplate(bgTypeId);
            uint32 mapId = bg->GetMapId();
            PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
            if (!pvpDiff)
                continue;

            BattleGroundBracketId bracketId = pvpDiff->GetBracketId();
#endif
#ifdef MANGOSBOT_TWO
            /* to fix
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                BattleGroundQueue& bgQueue = sServerFacade.bgQueue(queueTypeId);
                GroupQueueInfo ginfo;
                uint32 tempT = TeamId;
                if (bgQueue.GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                {
                    TeamId = ginfo.isRated ? 1 : 0;
                }
                if (bot->InArena())
                {
                    if (bot->GetBattleGround()->IsRated())
                        TeamId = 1;
                    else
                        TeamId = 0;
                }
                ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;
            }
        */
#endif
#ifdef MANGOSBOT_ONE
            ArenaType arenaType = sServerFacade.BgArenaType(queueTypeId);
            if (arenaType != ARENA_TYPE_NONE)
            {
                sWorld.GetBGQueue().GetMessager().AddMessage([queueTypeId, botId = bot->GetObjectGuid(), arenaType = arenaType, bracketId = bracketId, tempT = TeamId](BattleGroundQueue* bgQueue)
                    {
                        uint32 TeamId;
                        GroupQueueInfo ginfo;

                        BattleGroundQueueItem* queueItem = &bgQueue->GetBattleGroundQueue(queueTypeId);
                        Player *bot = RandomPlayerbotMgr::instance().GetPlayer(botId);
                        if (!bot)
                            return;

                        if (queueItem->GetPlayerGroupInfoData(bot->GetObjectGuid(), &ginfo))
                        {
                            TeamId = ginfo.isRated ? 1 : 0;
                        }
                        if (bot->InArena())
                        {
                            if (bot->GetBattleGround()->IsRated()/* && (ginfo.isRated && ginfo.arenaTeamId && ginfo.arenaTeamRating && ginfo.opponentsTeamRating)*/)
                                TeamId = 1;
                            else
                                TeamId = 0;
                        }

                        

                        sRandomPlayerbotMgr.ArenaBots[queueTypeId][bracketId][TeamId][tempT]++;

                    }
                );
            }
#endif
            BgBots[queueTypeId][bracketId][TeamId]++;
        }
    });

    for (int i = BG_BRACKET_ID_FIRST; i < MAX_BATTLEGROUND_BRACKETS; ++i)
    {
        for (int j = BATTLEGROUND_QUEUE_AV; j < MAX_BATTLEGROUND_QUEUE_TYPES; ++j)
        {
            BattleGroundQueueTypeId queueTypeId = BattleGroundQueueTypeId(j);

            if ((BgPlayers[j][i][0] + BgBots[j][i][0] + BgPlayers[j][i][1] + BgBots[j][i][1]) == 0)
                continue;

#ifndef MANGOSBOT_ZERO
            if (ArenaType type = sServerFacade.BgArenaType(queueTypeId))
            {
                sLog.outDetail("ARENA:%s %s: Plr (Skirmish:%d, Rated:%d) Bot (Skirmish:%d, Rated:%d) Total (Skirmish:%d Rated:%d)",
                    type == ARENA_TYPE_2v2 ? "2v2" : type == ARENA_TYPE_3v3 ? "3v3" : "5v5",
                    i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                    BgPlayers[j][i][0],
                    BgPlayers[j][i][1],
                    BgBots[j][i][0],
                    BgBots[j][i][1],
                    BgPlayers[j][i][0] + BgBots[j][i][0],
                    BgPlayers[j][i][1] + BgBots[j][i][1]
                );
                continue;
            }
#endif
            BattleGroundTypeId bgTypeId = sServerFacade.BgTemplateId(queueTypeId);
            std::string _bgType;
            switch (bgTypeId)
            {
            case BATTLEGROUND_AV:
                _bgType = "AV";
                break;
            case BATTLEGROUND_WS:
                _bgType = "WSG";
                break;
            case BATTLEGROUND_AB:
                _bgType = "AB";
                break;
#ifndef MANGOSBOT_ZERO
            case BATTLEGROUND_EY:
                _bgType = "EotS";
                break;
#endif
#ifdef MANGOSBOT_TWO
            case BATTLEGROUND_RB:
                _bgType = "Random";
                break;
            case BATTLEGROUND_SA:
                _bgType = "SotA";
                break;
            case BATTLEGROUND_IC:
                _bgType = "IoC";
                break;
#endif
            default:
                _bgType = "Other";
                break;
            }
            sLog.outDetail("BG:%s %s: Plr (%d:%d) Bot (%d:%d) Total (A:%d H:%d)",
                _bgType.c_str(),
                i == 0 ? "10-19" : i == 1 ? "20-29" : i == 2 ? "30-39" : i == 3 ? "40-49" : i == 4 ? "50-59" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 6) ? "60" : (i == 5 && MAX_BATTLEGROUND_BRACKETS == 7) ? "60-69" : i == 6 ? (i == 6 && MAX_BATTLEGROUND_BRACKETS == 16) ? "70-79" : "70" : "80",
                BgPlayers[j][i][0],
                BgPlayers[j][i][1],
                BgBots[j][i][0],
                BgBots[j][i][1],
                BgPlayers[j][i][0] + BgBots[j][i][0],
                BgPlayers[j][i][1] + BgBots[j][i][1]
            );
        }
    }

    sLog.outDetail("BG Queue check finished");
    return;
}

void RandomPlayerbotMgr::CheckLfgQueue()
{
    if (!LfgCheckTimer || time(NULL) > (LfgCheckTimer + 30))
        LfgCheckTimer = time(NULL);

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
        sLog.outDetail("Checking LFG Queue...");
    }

    // Clear LFG list
    LfgDungeons[HORDE].clear();
    LfgDungeons[ALLIANCE].clear();

    for (auto i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld())
            continue;

        bool isLFG = false;

#ifdef MANGOSBOT_ZERO
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group)
        {
            if (sWorld.GetLFGQueue().IsGroupInQueue(group->GetId()))
            {
                isLFG = true;
                LFGGroupQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetGroupQueueInfo(&lfgInfo, group->GetId());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
        else
        {
            if (sWorld.GetLFGQueue().IsPlayerInQueue(player->GetObjectGuid()))
            {
                isLFG = true;
                LFGPlayerQueueInfo lfgInfo;
                sWorld.GetLFGQueue().GetPlayerQueueInfo(&lfgInfo, player->GetObjectGuid());
                uint32 lfgType = (zoneId << 16) | lfgInfo.areaId;
                LfgDungeons[player->GetTeam()].push_back(lfgType);
            }
        }
#endif

#ifdef MANGOSBOT_ONE
        /* todo: Fix with new system
        WorldSafeLocsEntry const* ClosestGrave = player->GetMap()->GetGraveyardManager().GetClosestGraveYard(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetMapId(), player->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = player->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(player->GetObjectGuid()))
            {
                if (player->GetSession()->m_lfgInfo.queued && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                    isLFG = true;
                }
            }
        }
        else if (!group)
        {
            for (int i = 0; i < MAX_LOOKING_FOR_GROUP_SLOT; ++i)
                if (!player->m_lookingForGroup.group[i].empty() && player->GetSession()->LookingForGroup_auto_join && player->m_lookingForGroup.group[i].isAuto())
                {
                    isLFG = true;
                    uint32 lfgType = (zoneId << 16) | ((0 << 8) | uint8(player->m_lookingForGroup.group[i].entry));
                    LfgDungeons[player->GetTeam()].push_back(lfgType);
                }

            if (!player->m_lookingForGroup.more.empty() && player->GetSession()->LookingForGroup_auto_add && player->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(player->m_lookingForGroup.more.entry));
                LfgDungeons[player->GetTeam()].push_back(lfgType);
                isLFG = true;
            }
        }
        */
#endif

#ifdef MANGOSBOT_TWO
        Group* group = player->GetGroup();
        if (group)
        {
            if (group->IsLFGGroup())
            {
                isLFG = true;
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(group->GetObjectGuid());
                if (lfgData.GetState() != LFG_STATE_NONE && lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
        else
        {
            if (player->GetLfgData().GetState() != LFG_STATE_NONE)
            {
                LFGQueueData& lfgData = sWorld.GetLFGQueue().GetQueueData(player->GetObjectGuid());
                isLFG = true;
                if (lfgData.GetState() < LFG_STATE_DUNGEON)
                {
                    LfgDungeonSet dList = lfgData.GetDungeons();
                    for (auto dungeon : dList)
                    {
                        LfgDungeons[player->GetTeam()].push_back(dungeon);
                    }
                }
            }
        }
#endif
    }

#ifdef MANGOSBOT_ONE
    /* todo: Fix with new system
    ForEachPlayerbot([&](Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        if (LfgDungeons[bot->GetTeam()].empty())
            return;

        WorldSafeLocsEntry const* ClosestGrave = bot->GetMap()->GetGraveyardManager().GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
        uint32 zoneId = ClosestGrave ? ClosestGrave->ID : 0;

        Group* group = bot->GetGroup();
        if (group && !group->IsFull())
        {
            if (group->IsLeader(bot->GetObjectGuid()))
            {
                if (bot->GetSession()->m_lfgInfo.queued && bot->GetSession()->m_lfgInfo.autofill)
                {
                    uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                    LfgDungeons[bot->GetTeam()].push_back(lfgType);
                }
            }
        }
        else if (!group)
        {
            if (!bot->m_lookingForGroup.more.empty() && bot->GetSession()->LookingForGroup_auto_add && bot->m_lookingForGroup.more.isAuto())
            {
                uint32 lfgType = (zoneId << 16) | ((1 << 8) | uint8(bot->m_lookingForGroup.more.entry));
                LfgDungeons[bot->GetTeam()].push_back(lfgType);
            }
        }
    });
    */
#endif

    if (sPlayerbotAIConfig.logRandomBotJoinLfg)
    {
       if (LfgDungeons[ALLIANCE].size() || LfgDungeons[HORDE].size())
            sLog.outDetail("LFG Queue check finished. There are real players in queue.");
       else
           sLog.outDetail("LFG Queue check finished. No real players in queue.");
    }
    return;
}

void RandomPlayerbotMgr::AddOfflineGroupBots()
{
    if (!OfflineGroupBotsTimer || time(NULL) > (OfflineGroupBotsTimer + 5))
        OfflineGroupBotsTimer = time(NULL);

    uint32 totalCounter = 0;
    for (const auto& i : players)
    {
        Player* player = i.second;

        if (!player || !player->IsInWorld() || !player->GetGroup())
            continue;

        Group* group = player->GetGroup();
        if (group && group->IsLeader(player->GetObjectGuid()))
        {
            std::vector<uint32> botsToAdd;
            Group::MemberSlotList const& slots = group->GetMemberSlots();
            for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
            {
                ObjectGuid member = i->guid;
                if (member == player->GetObjectGuid())
                    continue;

                if (!IsFreeBot(member.GetCounter()))
                    continue;

                if (sObjectMgr.GetPlayer(member))
                    continue;

                if (GetPlayerBot(member))
                    continue;

                botsToAdd.push_back(member.GetCounter());
            }

            if (botsToAdd.empty())
                return;

            uint32 maxToAdd = urand(1, 5);
            uint32 counter = 0;
            for (auto& guid : botsToAdd)
            {
                if (counter >= maxToAdd)
                    break;

                if (sPlayerbotAIConfig.IsFreeAltBot(guid))
                {
                    for (auto& bot : sPlayerbotAIConfig.freeAltBots)
                    {
                        if (bot.second == guid)
                        {
                            Player* player = GetPlayerBot(bot.second);
                            if (!player)
                            {
                                AddPlayerBot(bot.second, bot.first);
                            }
                        }
                    }
                }
                else
                    AddRandomBot(guid);

                counter++;
                totalCounter++;
            }
        }
    }

    if (totalCounter)
        sLog.outDetail("Added %u offline bots from groups", totalCounter);
}

Item* RandomPlayerbotMgr::CreateTempItem(uint32 item, uint32 count, Player const* player, uint32 randomPropertyId)
{
    if (count < 1)
        return nullptr;                                        // don't create item at zero count

    if (ItemPrototype const* pProto = sObjectMgr.GetItemPrototype(item))
    {
        if (count > pProto->GetMaxStackSize())
            count = pProto->GetMaxStackSize();

        MANGOS_ASSERT(count != 0 && "pProto->Stackable == 0 but checked at loading already");

        Item* pItem = NewItemOrBag(pProto);
        if (pItem->Create(0, item, player ? player->GetObjectGuid() : ObjectGuid()))
        {
            pItem->SetCount(count);
            if (int32 randId = randomPropertyId ? randomPropertyId : Item::GenerateItemRandomPropertyId(item))
                pItem->SetItemRandomProperties(randId);

            return pItem;
        }
        delete pItem;
    }
    return nullptr;
}

InventoryResult RandomPlayerbotMgr::CanEquipUnseenItem(Player* player, uint8 slot, uint16& dest, uint32 item)
{
    dest = 0;
    Item* pItem = RandomPlayerbotMgr::CreateTempItem(item, 1, player);

    if (pItem)
    {
        InventoryResult result = player->CanEquipItem(slot, dest, pItem, true, false);

        pItem->RemoveFromUpdateQueueOf(player);

        if (!player->GetItemUpdateQueue().empty() && !player->GetItemUpdateQueue().back()) //Prevent queue overflow.
            player->GetItemUpdateQueue().pop_back();

        delete pItem;
        return result;
    }

    return EQUIP_ERR_ITEM_NOT_FOUND;
}

void RandomPlayerbotMgr::SaveCurTime()
{
    if (!EventTimeSyncTimer || time(NULL) > (EventTimeSyncTimer + 60))
        EventTimeSyncTimer = time(NULL);

    SetValue(uint32(0), "current_time", uint32(time(nullptr)));
}

void RandomPlayerbotMgr::SyncEventTimers()
{
    uint32 oldTime = GetValue(uint32(0), "current_time");
    if (oldTime)
    {
        uint32 curTime = time(nullptr);
        uint32 timeDiff = curTime - oldTime;
        CharacterDatabase.PExecute(PlayerbotDatabaseContract::EventStoreSql(
            "UPDATE ", " SET time = time + %u WHERE owner = 0 AND bot <> 0").c_str(), timeDiff);
    }
}

void RandomPlayerbotMgr::CheckPlayers()
{
    if (!PlayersCheckTimer || time(NULL) > (PlayersCheckTimer + 60))
        PlayersCheckTimer = time(NULL);

    sLog.outDetail("Checking Players...");

    uint32 newPlayersLevel = 0;

    for (auto i : players)
    {
        Player* player = i.second;

        if (player->IsGameMaster())
            continue;

        //if (player->GetSession()->GetSecurity() > SEC_PLAYER)
        //    continue;

        if (player->GetLevel() > newPlayersLevel)
            newPlayersLevel = player->GetLevel();
    }

    if(playersLevel!= newPlayersLevel)
        sLog.outDetail("Max player level is %d, max bot level changed from %d to %d", newPlayersLevel, playersLevel, newPlayersLevel);
    else
        sLog.outDetail("Max player level is %d, max bot level set to %d", newPlayersLevel, newPlayersLevel);

    playersLevel = newPlayersLevel;

    return;
}

void RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time)
{
    SetEventValue(bot, "randomize", 1, time);
}

// Resolve the configured names once. Deferred rather than done in
// PlayerbotAIConfig::Initialize because that runs before the character database
// is usable, and a name is what a person can reasonably be asked to write in a
// config file.
void RandomPlayerbotMgr::ResolvePinnedBots()
{
    m_pinnedBotsResolved = true;

    for (const std::string& name : sPlayerbotAIConfig.pinnedBotNames)
    {
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);

        auto result = CharacterDatabase.PQuery("SELECT guid FROM characters WHERE name = '%s'", escaped.c_str());
        if (!result)
        {
            sLog.outError("PinnedBots: no character named '%s'", name.c_str());
            continue;
        }

        uint32 guid = result->Fetch()[0].GetUInt32();
        m_pinnedBots.insert(guid);
        sLog.outString("PinnedBots: '%s' (guid %u) will stay online and will not be relocated", name.c_str(), guid);
    }
}

bool RandomPlayerbotMgr::IsPinnedBot(uint32 guidLow)
{
    if (!m_pinnedBotsResolved)
        ResolvePinnedBots();

    return m_pinnedBots.find(guidLow) != m_pinnedBots.end();
}

// AddRandomBots only tops the population up to MaxRandomBots and stops there, so
// which characters get in is decided once and never revisited. A pinned bot that
// missed the cut would simply never appear, which is why this runs alongside it.
void RandomPlayerbotMgr::EnsurePinnedBotsOnline()
{
    if (!m_pinnedBotsResolved)
        ResolvePinnedBots();

    for (uint32 guid : m_pinnedBots)
    {
        if (!GetPlayerBot(guid))
            AddRandomBot(guid);
    }
}

void RandomPlayerbotMgr::ScheduleTeleport(uint32 bot, uint32 time)
{
    if (!time)
        time = 60 + urand(sPlayerbotAIConfig.randomBotTeleportMinInterval, sPlayerbotAIConfig.randomBotTeleportMaxInterval);
    SetEventValue(bot, "teleport", 1, time);
}

void RandomPlayerbotMgr::ScheduleChangeStrategy(uint32 bot, uint32 time)
{
    if (!time)
        time = urand(sPlayerbotAIConfig.minRandomBotChangeStrategyTime, sPlayerbotAIConfig.maxRandomBotChangeStrategyTime);
    SetEventValue(bot, "change_strategy", 1, time);
}

bool RandomPlayerbotMgr::ValidatePersistentRosterLogin(uint32 bot, uint32& accountId, std::string& diagnostic) const
{
    accountId = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, bot));
    std::string accountName;
    bool accountExists = accountId && sAccountMgr.GetName(accountId, accountName);
    std::transform(accountName.begin(), accountName.end(), accountName.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    bool rndbotStock = accountExists && accountName.compare(0, 6, "RNDBOT") == 0 &&
        sPlayerbotAIConfig.IsInRandomAccountList(accountId);
    bool loginRecordExists = false;
    bool accountActive = false;
    uint32 accountLockFlags = 0;
    if (accountExists)
    {
        std::unique_ptr<QueryResult> loginRecord(LoginDatabase.PQuery(
            "SELECT `locked`, `active` FROM `account` WHERE `id`=%u", accountId));
        if (loginRecord && loginRecord->GetRowCount() == 1)
        {
            loginRecordExists = true;
            Field* fields = loginRecord->Fetch();
            accountLockFlags = fields[0].GetUInt32();
            accountActive = fields[1].GetBool();
        }
    }
    diagnostic = ai::roster::EvaluateRosterAccountAdmission(accountId != 0, accountExists, rndbotStock,
        accountExists && sAccountMgr.IsAccountBanned(accountId), loginRecordExists, accountActive,
        accountLockFlags, accountExists && sWorld.FindSession(accountId));
    if (!diagnostic.empty())
        return false;
    return true;
}

bool RandomPlayerbotMgr::PersistentRosterRetryReady(uint32 bot) const
{
    auto it = persistentRosterRetryAfter.find(bot);
    return it == persistentRosterRetryAfter.end() || std::chrono::steady_clock::now() >= it->second;
}

void RandomPlayerbotMgr::SchedulePersistentRosterRetry(uint32 bot, std::string const& diagnostic)
{
    uint32& attempt = persistentRosterRetryAttempts[bot];
    attempt = std::min<uint32>(attempt + 1, 6);
    uint32 delaySeconds = std::min<uint32>(1u << attempt, 60u);
    persistentRosterRetryAfter[bot] = std::chrono::steady_clock::now() + std::chrono::seconds(delaySeconds);
    if (persistentRoster)
        persistentRoster->RecordUnavailable(bot, diagnostic + ":RETRY_IN_" + std::to_string(delaySeconds) + "S");
    sLog.outError("[PersistentRoster] DEGRADED guid=%u code=%s retry_in=%us; no replacement selected",
        bot, diagnostic.c_str(), delaySeconds);
}

void RandomPlayerbotMgr::ClearPersistentRosterRetry(uint32 bot)
{
    persistentRosterRetryAttempts.erase(bot);
    persistentRosterRetryAfter.erase(bot);
}

bool RandomPlayerbotMgr::AddRandomBot(uint32 bot)
{
    SC_LOG("AddRandomBot entry guid=%u", bot);
    if (sPlayerbotAIConfig.persistentActiveRosterEnabled && !IsPersistentRosterMember(bot))
    {
        sLog.outError("[PersistentRoster] rejected non-roster AddRandomBot guid=%u", bot);
        return false;
    }
    Player* player = GetPlayerBot(bot);
    if (player)
    {
        SC_LOG("AddRandomBot guid=%u already online — returning true", bot);
        return true;
    }

    if (IsPersistentRosterMember(bot))
    {
        persistentRoster->RecordPending(bot);
        if (!PersistentRosterRetryReady(bot))
            return false;

        uint32 validatedAccountId = 0;
        std::string diagnostic;
        if (!ValidatePersistentRosterLogin(bot, validatedAccountId, diagnostic))
        {
            SchedulePersistentRosterRetry(bot, diagnostic);
            return false;
        }
        persistentRoster->RecordAvailable(bot);
        if (GetEventValue(bot, "login"))
            return true;
        if (!AddPlayerBot(bot, 0))
        {
            SchedulePersistentRosterRetry(bot, "LOGIN_QUEUE_REJECTED");
            return false;
        }
        SetEventValue(bot, "login", 1, -1);
        return true;
    }

    uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, bot));
    SC_LOG("AddRandomBot guid=%u accountId=%u — checking IsInRandomAccountList", bot, accountId);

    if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        SC_LOG("AddRandomBot guid=%u — NOT in random account list, FAIL", bot);
        sLog.outError("Bot #%d login fail: Not random bot!", bot);
        return false;
    }

    uint32 loginEv = GetEventValue(bot, "login");
    SC_LOG("AddRandomBot guid=%u — IsInRandomAccountList OK, login event=%u", bot, loginEv);

    // stale-event recovery: if login=1 is set but
    // we already proved (line ~2124, GetPlayerBot returned null) that no
    // actual session exists for this bot, the marker is stale — typically
    // because the previous mangosd crashed mid-tick before the bot's
    // logout cleanup could clear the event. Without this fix, the bot is
    // permanently un-summonable until someone manually `DELETE FROM
    // ai_playerbot_random_bots` and bounces mangosd to clear the in-memory
    // eventCache. We force-reset the event in both DB and cache, then
    // proceed with a fresh login.
    if (loginEv)
    {
        SC_LOG("AddRandomBot guid=%u — login=1 but no live session; treating as stale, force-resetting event", bot);
        SetEventValue(bot, "login", 0, 0);  // updates DB row + eventCache in one call
        loginEv = 0;
    }

    if (!loginEv)
    {
        SC_LOG("AddRandomBot guid=%u — calling AddPlayerBot", bot);
        AddPlayerBot(bot, 0);
        SC_LOG("AddRandomBot guid=%u — AddPlayerBot returned, setting event values", bot);
        SetEventValue(bot, "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
        SetEventValue(bot, "logout", 0, 0);
        SetEventValue(bot, "login", 1, -1);
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);
        currentBots.push_back(bot);
        SC_LOG("AddRandomBot guid=%u — DONE, added to currentBots", bot);
        sLog.outDetail("Random bot added #%d", bot);
    }
    else
    {
        SC_LOG("AddRandomBot guid=%u — login event already set, SKIPPING actual login", bot);
    }

    return true;
}

void RandomPlayerbotMgr::MovePlayerBot(uint32 guid, PlayerbotHolder* newHolder)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    players[guid] = this->GetPlayerBot(guid);
    PlayerbotHolder::MovePlayerBot(guid, newHolder);
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    // A test-harness login is not this manager's bot AT ALL: not just exempt
    // from the timed logout (older guard below), but from Randomize /
    // ResetStrategies / RandomTeleport too. Live (tr-20260822-215152-1): the
    // rotation refresh rebuilt a dc run member's strategies every ~90s and
    // RandomTeleportForLevel ported it out of the Deadmines run onto a
    // Westfall grind spot mid-run.
    if (IsExternallyManaged(bot))
        return false;

    Player* player = GetPlayerBot(bot);
    if (player && sPlayerbotAIConfig.IsFreeAltBot(player))
    {
        return false;
    }

    PlayerbotAI* ai = player ? GetBotAI(player) : NULL;

    bool persistentRosterBot = IsPersistentRosterMember(bot);
    ai::roster::RuntimeBehaviorPolicy runtimePolicy =
        ai::roster::EvaluateRuntimeBehaviorPolicy(persistentRosterBot, player && player->GetGroup());
    if (persistentRosterBot)
    {
        if (!player)
            return AddRandomBot(bot);
        if (!player->GetSession())
        {
            persistentRoster->RecordPending(bot);
            SchedulePersistentRosterRetry(bot, "PLAYER_SESSION_MISSING");
            return false;
        }
        if (!player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut())
            return false;
        if (GetEventValue(bot, "login"))
            SetEventValue(bot, "login", 0, 0);
        ClearPersistentRosterRetry(bot);
        persistentRoster->RecordOnline(bot);
    }

    // twow-repo#541/#551 (HERE:1600, 33 [Park] state=lost): with random bots (PersistentActiveRoster 0)
    // the timed logout below logged parked bots out when their lease ran out. A parked bot stays; the
    // roster bookkeeping above still runs.
    if (IsParkedBot(bot))
        return false;

    bool botsAllowedInWorld =!sPlayerbotAIConfig.randomBotLoginWithPlayer || (!players.empty() && sWorld.GetActiveSessionCount() > 0);

    bool isValid = true;

    if (runtimePolicy.leaseLogout && sPlayerbotAIConfig.randomBotTimedLogout && !GetEventValue(bot, "add") && !sPlayerbotAIConfig.asyncBotLogin) // RandomBotInWorldTime is expired.
        isValid = false;
    else if(runtimePolicy.populationRotation && !botsAllowedInWorld)           // Logout if all players logged out
        isValid = false;

    //Log out bot
    if (!isValid)
    {
        // An externally-managed login (dc test harness) is not ours to reap -
        // its owner logs it out in its own teardown.
        if (IsExternallyManaged(bot))
            return false;

        if (botsAllowedInWorld && player && player->GetGroup())
        {
            SetEventValue(bot, "add", 1, 120);                                 // Delay logout for 2 minutes while in group.
            return false;
        }

        if (!player || !player->IsInWorld())
            sLog.outDetail("Bot #%d: log out", bot);
        else
            sLog.outDetail("Bot #%d %s:%d <%s>: log out", bot, IsAlliance(player->getRace()) ? "A" : "H", player->GetLevel(), player->GetName());

        currentBots.remove(bot);
        SetEventValue(bot, "add", 0, 0);

        if (!player)
        {
            return false;
        }    

        LogoutPlayerBot(bot);

        if (sPlayerbotAIConfig.randomBotTimedOffline)
        {
            uint32 logout = GetEventValue(bot, "logout");

            if (!logout)
                SetEventValue(bot, "logout", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
        }

        return false;
    }

    //Log in bot (Added in AddRandomBots)
    if (!player)
    {
        if (!botsAllowedInWorld)
            return false;

        if (GetEventValue(bot, "login"))
            return true;

        AddPlayerBot(bot, 0);

        SetEventValue(bot, "login", 1, -1); // This will be reset to 0 on server startup. Check RandomPlayerbotMgr constructor

        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);

        return true;
    }

    if (!player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut()) //Skip bots that are in limbo.
        return false;

    if(GetEventValue(bot, "login"))
        SetEventValue(bot, "login", 0, 0); //Bot is no longer loggin in.

    uint32 update = GetEventValue(bot, "update");
    //Update the bot
    if (!update)
    {
        //Clean up expired values
        if (ai && !ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            ai->GetAiObjectContext()->ClearExpiredValues();

        //Randomize/teleport bot
        // Note: ProcessBot() itself skips Randomize() (which reassigns level/spec) when
        // disableRandomLevels is set, but still runs ChangeStrategy()/RandomTeleportForLevel()
        // for idle bots - so this must not be skipped wholesale for disableRandomLevels=1.
        if (player->GetGroup() || player->IsTaxiFlying())
            return false;

        bool update = true;
        if (ai)
        {
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                update = false;

            if (player->GetGroup() && ai->GetGroupMaster() && (!GetBotAI(ai->GetGroupMaster()) || GetBotAI(ai->GetGroupMaster())->IsRealPlayer()))
                update = false;

            if (ai->HasPlayerNearby())
                update = false;
        }
        if (update)
            ProcessBot(player);

        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime * 5);
        SetEventValue(bot, "update", 1, randomTime);
        return true;
    }

    return false;
}

bool RandomPlayerbotMgr::ProcessBot(Player* player)
{
    // twow-repo#541/#551: no randomize, strategy change or teleport for a parked bot.
    if (player && IsParkedBot(player->GetGUIDLow()))
        return false;
    // Same harness guard as the guid overload - this one is the entry point
    // for callers that already hold the player.
    if (player && IsExternallyManaged(player->GetGUIDLow()))
        return false;

    if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->GetSession()->isLogingOut())
        return false;

    uint32 bot = player->GetGUIDLow();
    bool persistentRosterBot = IsPersistentRosterMember(bot);
    ai::roster::RuntimeBehaviorPolicy runtimePolicy =
        ai::roster::EvaluateRuntimeBehaviorPolicy(persistentRosterBot, player->GetGroup() != nullptr);

    if (player->InBattleGround())
        return false;

    if (player->InBattleGroundQueue())
        return false;

    // only teleport idle bots
    bool idleBot = false;
    TravelTarget* target = GetBotAI(player)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
    if (target)
    {
        if (target->GetTravelState() == TravelState::TRAVEL_STATE_IDLE)
            idleBot = true;
    }
    else
        idleBot = true;

    if (idleBot && runtimePolicy.randomizeProgression)
    {
        uint32 randomize = GetEventValue(bot, "randomize");
        // Randomize() reassigns level/spec - never call it when levels are pinned
        // (DisableRandomLevels=1). Falls through to the changeStrategy/teleport checks
        // below so idle bots still get RandomTeleportForLevel()'s zone relocation.
        if (!sPlayerbotAIConfig.disableRandomLevels && !randomize)
        {
            bool randomiser = true;
            if (player->GetGuildId())
            {
                Guild* guild = sGuildMgr.GetGuildById(player->GetGuildId());
                uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guild->GetLeaderGuid());
                if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
                {
                    int32 rank = guild->GetRank(player->GetObjectGuid());
                    randomiser = rank < 4 ? false : true;
                }
            }

            if (randomiser)
            {
                Randomize(player);
                return true;
            }
        }

        // Both branches below teleport the bot - to an inn or to a grind spot -
        // which for a pinned bot means being pulled out of the quest chain it is
        // being watched through. Nothing else about it differs from any other
        // random bot.
        if (IsPinnedBot(bot))
            return false;

        uint32 changeStrategy = GetEventValue(bot, "change_strategy");
        if (!changeStrategy)
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s>", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                ChangeStrategy(player);
                ScheduleChangeStrategy(bot);
            }
            else
            {
                sLog.outDetail("Changing strategy for bot #%d %s:%d <%s> is supposed to happen, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }

        uint32 teleport = GetEventValue(bot, "teleport");
        if (!teleport && players.size())
        {
            if (sPlayerbotAIConfig.enableRandomTeleports)
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
                RandomTeleportForLevel(player, true);
                ScheduleTeleport(bot);
            }
            else
            {
                sLog.outDetail("Bot #%d %s:%d <%s>: supposed to be sent to grind, but enableRandomTeleports = false", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
            }
            return true;
        }
    }

    return false;
}

void RandomPlayerbotMgr::Revive(Player* player)
{
    uint32 bot = player->GetGUIDLow();

    //sLog.outString("Bot %d revived", bot);
    SetEventValue(bot, "dead", 0, 0);
    SetEventValue(bot, "revive", 0, 0);

    if (sServerFacade.GetDeathState(player) == CORPSE)
    {
        RandomTeleport(player);
    }
    else
    {
        RandomTeleportForLevel(player, false);
    }
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation> &locs, bool hearth, bool activeOnly)
{
    if (bot->IsBeingTeleported())
        return;

    if (bot->InBattleGround())
        return;

    if (bot->InBattleGroundQueue())
        return;

	if (bot->GetLevel() < 5)
		return;

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        return;

    if (bot->IsTaxiFlying() && GetBotAI(bot)->HasPlayerNearby())
        return;

    if (locs.empty())
    {
        sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
        return;
    }

    std::vector<WorldPosition> tlocs;

    for (auto& loc : locs)
    {
        tlocs.push_back(WorldPosition(loc));
    }

    //Do not teleport to maps disabled in config
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [](const WorldPosition& l) {std::vector<uint32>::iterator i = find(sPlayerbotAIConfig.randomBotMaps.begin(), sPlayerbotAIConfig.randomBotMaps.end(), l.getMapId()); return i == sPlayerbotAIConfig.randomBotMaps.end(); }), tlocs.end());

    // Do not drop bots into the enemy faction's home territory. The teleport
    // cache (ai_playerbot_tele_cache) only knows level, map and coordinates -
    // no faction - so an Alliance bot was as likely to land in Durotar as in
    // Elwynn. There is nothing for it to do there and the city guards kill it
    // on sight, over and over. Contested zones stay open.
    // Skipped if it would leave too little to choose from, so a level bracket
    // that only exists in enemy territory does not strand its bots.
    {
        std::vector<WorldPosition> friendly;
        friendly.reserve(tlocs.size());
        for (WorldPosition const& loc : tlocs)
            if (!loc.isEnemyHomeZoneFor(bot->GetTeam()))
                friendly.push_back(loc);

        if (friendly.size() >= tlocs.size() / 4)
            tlocs = friendly;
    }

    //Random shuffle based on distance. Closer distances are more likely (but not exclusively) to be at the begin of the list.
    tlocs = WorldPosition(bot).GetNextPoint(tlocs, 0);

    //5% + 0.1% per level chance node on different map in selection.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.position.mapid != bot->GetMapId() && urand(1, 100) > 0.5 * bot->GetLevel(); }), tlocs.end());

    //Continent is about 20.000 large
    //Bot will travel 0-5000 units + 75-150 units per level.
    //tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](WorldLocation const& l) {return l.position.mapid == bot->GetMapId() && sServerFacade.GetDistance2d(bot, l.coord_x, l.coord_y) > urand(0, 5000) + bot->GetLevel() * 15 * urand(5, 10); }), tlocs.end());

    // teleport to active areas only
    if (sPlayerbotAIConfig.randomBotTeleportNearPlayer && activeOnly)
    {
        tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [this](const WorldPosition& l)
        {
            uint32 mapId = l.getMapId();
            Map* tMap = sMapMgr.FindMap(mapId, 0);
            if (tMap && tMap->IsContinent() && tMap->HasActiveZones())
            {
                uint32 zoneId = sTerrainMgr.GetZoneId(mapId, l.coord_x, l.coord_y, l.coord_z);
                if (tMap->HasActiveZone(zoneId))
                {
                    if (sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount > 0 && sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius > 0.0f)
                    {
                        uint32 botsNearTeleportPoint = 0;
                        ForEachPlayerbot([&](Player* otherBot)
                        {
                            // Only check the bots that are on the same zone
                            if (otherBot && !otherBot->IsBeingTeleported() && zoneId == otherBot->GetZoneId())
                            {
                                if (l.fDist(WorldPosition(otherBot)) <= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmountRadius)
                                {
                                    botsNearTeleportPoint++;
                                }
                            }
                        });

                        return botsNearTeleportPoint >= sPlayerbotAIConfig.randomBotTeleportNearPlayerMaxAmount;
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            return true;
        }),
        tlocs.end());

        /*if (!tlocs.empty())
        {
            tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](const WorldPosition& l)
            {
                uint32 mapId = l.getMapId();
                Map* tMap = sMapMgr.FindMap(mapId, 0);
                if (!tMap || !tMap->IsContinent())
                        return true;

                if (!tMap->HasActiveAreas())
                    return true;

                AreaTableEntry const* area = l.getArea();
                if (area)
                {
                    if (!tMap->HasActiveZone(area->zone ? area->zone : area->ID))
                        return true;
                }
            }), tlocs.end());
        }*/
    }

    // filter starter zones
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(), [bot](const WorldPosition& l)
    {
        uint32 mapId = l.getMapId();
        uint32 zoneId, areaId;
        sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, mapId, l.coord_x, l.coord_y, l.coord_z);
        AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
        if (zoneId && zoneId != areaId)
        {
            AreaTableEntry const* zone = GetAreaEntryByAreaID(zoneId);
            if (!zone)
                return true;

            bool isEnemyZone = false;
            switch (zone->team)
            {
            case AREATEAM_ALLY:
                isEnemyZone = bot->GetTeam() != ALLIANCE;
                break;
            case AREATEAM_HORDE:
                isEnemyZone = bot->GetTeam() != HORDE;
                break;
            default:
                isEnemyZone = false;
                break;
            }
            if (isEnemyZone && (bot->GetLevel() < 21 || (zone->flags & AREA_FLAG_CAPITAL)))
                return true;

            // filter other races zones
            if (bot->GetLevel() < 30)
            {
                if ((zoneId == 12 || zoneId == 40) && bot->getRace() != RACE_HUMAN && bot->getRace() != RACE_HIGH_ELF)
                    return true;
                if ((zoneId == 1 || zoneId == 38) && bot->getRace() != RACE_DWARF && bot->getRace() != RACE_GNOME)
                    return true;
                if ((zoneId == 85 || zoneId == 130) && bot->getRace() != RACE_UNDEAD)
                    return true;
                if ((zoneId == 141 || zoneId == 148) && bot->getRace() != RACE_NIGHTELF)
                    return true;
                if ((zoneId == 14 || zoneId == 17) && !(bot->getRace() == RACE_ORC || bot->getRace() == RACE_TROLL || bot->getRace() == RACE_GOBLIN))
                    return true;
                if ((zoneId == 215) && bot->getRace() != RACE_TAUREN)
                    return true;
                // redridge / duskwood
                if ((zoneId == 44 || zoneId == 10) && bot->GetTeam() != ALLIANCE)
                    return true;
#ifndef MANGOSBOT_ZERO
                if ((zoneId == 3524 || zoneId == 3525) && bot->getRace() != RACE_DRAENEI)
                    return true;
                if ((zoneId == 3430 || zoneId == 3433) && bot->getRace() != RACE_BLOODELF)
                    return true;
#endif
            }
        }

        // Never send bots to custom player-only starting zones (no MMAP support).
        // twow-repo#575 (PATH-01 E3): Alah'Thalas 2040 is a zone of its own and is filtered too; with
        // AiPlayerbot.CustomRaceStartZones the zones are teleport targets for their own race only.
        if (ai::start_zone::TeleportSkipsZone(zoneId, bot->getRace(), sPlayerbotAIConfig.customRaceStartZones))
            return true;

        if (!area)
            return true;

        bool isEnemyZone = false;
        switch (area->team)
        {
        case AREATEAM_ALLY:
            isEnemyZone = bot->GetTeam() != ALLIANCE;
            break;
        case AREATEAM_HORDE:
            isEnemyZone = bot->GetTeam() != HORDE;
            break;
        default:
            isEnemyZone = false;
            break;
        }
        return isEnemyZone && bot->GetLevel() < 21;

    }), tlocs.end());

    if (tlocs.empty())
    {
        if (activeOnly)
        {
            if (hearth)
                return RandomTeleportForRpg(bot, false);
            else
                return RandomTeleportForLevel(bot, false);
        }

        sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());

        return;
    }

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleportByLocations");

    int index = 0;

    for (int i = 0; i < tlocs.size(); i++)
    {
        for (int attemtps = 0; attemtps < 3; ++attemtps)
        {
            WorldLocation loc = tlocs[i];

#ifdef MANGOSBOT_ONE
            // Teleport to Dark Portal area if event is in progress
            if (sWorldState.GetExpansion() == EXPANSION_NONE && bot->GetLevel() > 54 && urand(0, 100) > 20)
            {
                if (urand(0, 1))
                    loc = WorldLocation(uint32(0), -11772.43f, -3272.84f, -17.9f, 3.32447f);
                else
                    loc = WorldLocation(uint32(0), -11741.70f, -3130.3f, -11.7936f, 3.32447f);
            }
#endif

            float x = loc.coord_x + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float y = loc.coord_y + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2 : 0);
            float z = loc.coord_z;

            Map* map = sMapMgr.FindMap(loc.mapid, 0);
            if (!map)
                continue;

            uint32 areaId = sTerrainMgr.GetAreaId(loc.mapid, x, y, z);
            AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
            if (!area)
                continue;

#ifndef MANGOSBOT_ZERO
            // Do not teleport to outland before portal opening (allow new races zones)
            if (sWorldState.GetExpansion() == EXPANSION_NONE && (loc.mapid == 571 || (loc.mapid == 530 && area->team != 2 && area->team != 4)))
                continue;
#endif

#ifdef MANGOSBOT_TWO
            float ground = map->GetHeight(bot->GetPhaseMask(), x, y, z + 0.5f);
#else
            float ground = map->GetHeight(x, y, z + 0.5f);
#endif
            if (ground <= INVALID_HEIGHT)
                continue;

            z = 0.05f + ground;
            // area_name is a char* (aliased via a union with Name in Map.h), not a
            // locale array - area_name[0] was dereferencing it to a single char and
            // handing that raw value to %s as if it were a pointer, segfaulting the
            // instant the format machinery tried to read a string from it. It can
            // also be null (e.g. custom zones with incomplete DBC data), same
            // nullability WorldPosition::getAreaName() already guards against.
            sLog.outDetail("Random teleporting bot %s to %s %f,%f,%f (%u/%zu locations)",
                bot->GetName(), area->area_name ? area->area_name : "", x, y, z, attemtps, tlocs.size());

            if (bot->IsTaxiFlying())
                bot->GetMotionMaster()->MovementExpired();

            // A teleport into enemy territory is survivable - the bot leaves
            // again. Binding its home there is not: it hearths back for the
            // rest of its life and the city guards kill it every time. The
            // filter above may hand out an enemy location when too little else
            // is left, so refuse the bind separately from the teleport.
            //
            // Found 2026-08-10: 108 High Elves were bound to Undercity and
            // Orgrimmar this way and averaged 1179 deaths each, against 494
            // for correctly bound bots.
            bool const hostileHome = WorldPosition(loc).isEnemyHomeZoneFor(bot->GetTeam());

            if (hearth && !hostileHome)
                bot->SetHomebindToLocation(loc, area->ID);

            bot->GetMotionMaster()->Clear();
            bot->TeleportTo(loc.mapid, x, y, z, 0);
            bot->SendHeartBeat();
            GetBotAI(bot)->Reset(true);

            if (bot->GetGroup())
            {
                for (GroupReference* gref = bot->GetGroup()->GetFirstMember(); gref; gref = gref->next())
                {
                    Player* member = gref->getSource();
                    PlayerbotAI* ai = GetBotAI(bot);
                    // twow-repo#595 P4 B1 (AiPlayerbot.Shortcut.GroupLoopFix, critic X6): 1 = each member's own AI; a
                    // real member (no bot AI, or a real session) is skipped and not moved. 0 = the leader's AI as before.
                    // The leader itself is never a member to skip (it is excluded from the move by bot != member anyway).
                    if (sPlayerbotAIConfig.shortcutGroupLoopFix && bot != member)
                    {
                        ai = GetBotAI(member);
                        if (!::ai::shortcut::GroupLoopMovesMember(true, ai != nullptr, ai && ai->IsRealPlayer()))
                        {
                            sLog.outBasic("[Shortcut] mech=group_loop state=skip_real leader=%u member=%u",
                                bot->GetGUIDLow(), member->GetGUIDLow());
                            continue;
                        }
                    }
                    if (ai && bot != member)
                    {
                        if (member->IsTaxiFlying())
                            member->GetMotionMaster()->MovementExpired();
                        if (hearth && !hostileHome)
                            member->SetHomebindToLocation(loc, area->ID);

                        member->GetMotionMaster()->Clear();
                        member->TeleportTo(loc.mapid, x, y, z, 0);
                        member->SendHeartBeat();
                        // twow-repo#595 P4 B1: null guard in both paths - with switch 0 a real member (no bot AI) is
                        // still moved as before, but no longer crashes the world thread on a null AI.
                        if (PlayerbotAI* memberAi = GetBotAI(member))
                            memberAi->Reset(true);
                    }

                }
            }
            return;
        }
    }

    sLog.outError("Cannot teleport bot %s - no locations available", bot->GetName());
}

std::vector<std::pair<uint32, uint32>> RandomPlayerbotMgr::RpgLocationsNear(WorldLocation pos, const std::map<uint32, std::map<uint32, std::vector<std::string>>>& areaNames, uint32 radius)
{
    std::vector<std::pair<uint32, uint32>> results;
    float minDist = FLT_MAX;
    WorldPosition areaPos(pos);
    std::string hasZone = "-", wantZone = areaPos.getAreaName(true, true);

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            uint32 i = 0;
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                std::string currentZone = areaNames.at(level).at(r)[i];
                i++;

                if (currentZone != wantZone && hasZone == wantZone) //If we already have the right id but this location isn't in the right id. Skip it.
                    continue;

                if (currentZone == wantZone && hasZone != wantZone) //If this is the first spot with a good area id use this now.
                    minDist = FLT_MAX;

                float dist = WorldPosition(pos).fDist(p);

                if (dist > radius || dist > minDist)
                    continue;

                if (dist < minDist)
                    results.clear();

                results.push_back(std::make_pair(r, level));

                hasZone = currentZone;

                minDist = dist;
            }
        }
    }

    return results;
}

void RandomPlayerbotMgr::PrepareTeleportCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    auto results = CharacterDatabase.PQuery("SELECT `map_id`, `x`, `y`, `z`, `level` FROM `ai_playerbot_tele_cache`");
    if (results)
    {
        sLog.outString("Loading random teleport caches for %d levels...", maxLevel);
        do
        {
            Field* fields = results->Fetch();
            uint16 mapId = fields[0].GetUInt16();
            float x = fields[1].GetFloat();
            float y = fields[2].GetFloat();
            float z = fields[3].GetFloat();
            uint16 level = fields[4].GetUInt16();
            WorldLocation loc(mapId, x, y, z, 0);
            locsPerLevelCache[level].push_back(loc);
        } while (results->NextRow());
    }
    else
    {
        sLog.outString("Preparing random teleport caches for %d levels...", maxLevel);
        BarGoLink bar(maxLevel);
        for (uint8 level = 1; level <= maxLevel; level++)
        {
            auto results = WorldDatabase.PQuery("SELECT `map`, `position_x`, `position_y`, `position_z` "
                "FROM (SELECT `map`, `position_x`, `position_y`, `position_z`, t.level_max, t.level_min, "
                "%u - (t.level_max + t.level_min) / 2 delta "
                "FROM creature c INNER JOIN creature_template t ON c.id = t.entry WHERE t.type != 8 AND t.npc_flags = 0 AND t.rank = 0 AND NOT (t.flags_extra & 1024 OR t.flags_extra & 65536 OR t.flags_extra & 64 OR t.unit_flags & 256 OR t.unit_flags & 512) AND t.loot_id != 0) q "
                "WHERE delta >= 0 AND delta <= %u AND map in (%s)",
                level,
                sPlayerbotAIConfig.randomBotTeleLevel,
                sPlayerbotAIConfig.randomBotMapsAsString.c_str()
            );
            if (results)
            {
                CharacterDatabase.BeginTransaction();
                do
                {
                    Field* fields = results->Fetch();
                    uint16 mapId = fields[0].GetUInt16();
                    float x = fields[1].GetFloat();
                    float y = fields[2].GetFloat();
                    float z = fields[3].GetFloat();
                    WorldLocation loc(mapId, x, y, z, 0);
                    locsPerLevelCache[level].push_back(loc);

                    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_tele_cache` (`level`, `map_id`, `x`, `y`, `z`) VALUES (%u, %u, %f, %f, %f)",
                        level, mapId, x, y, z);
                } while (results->NextRow());
                CharacterDatabase.CommitTransaction();
            }
            bar.step();
        }
    }

    sLog.outString("Preparing RPG teleport caches for %d factions...", sFactionTemplateStore.GetNumRows());

    results = WorldDatabase.PQuery("SELECT map, position_x, position_y, position_z, "
        "r.race, r.minl, r.maxl "
        "FROM creature c INNER JOIN ai_playerbot_rpg_races r ON c.id = r.entry "
        "WHERE r.race < 15");

    if (results)
    {
        do
        {
            for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
            {
                Field* fields = results->Fetch();
                uint16 mapId = fields[0].GetUInt16();
                float x = fields[1].GetFloat();
                float y = fields[2].GetFloat();
                float z = fields[3].GetFloat();
                //uint32 faction = fields[4].GetUInt32();
                //string name = fields[5].GetCppString();
                uint32 race = fields[4].GetUInt32();
                uint32 minl = fields[5].GetUInt32();
                uint32 maxl = fields[6].GetUInt32();

                if (level > maxl || level < minl) continue;

                WorldLocation loc(mapId, x, y, z, 0);
                for (uint32 r = 1; r < MAX_RACES; r++)
                {
                    if (race == r || race == 0) rpgLocsCacheLevel[r][level].push_back(loc);
                }
            }
            //bar.step();
        } while (results->NextRow());
    }

    sLog.outString("Enhancing RPG teleport cache");

    std::map<uint32, std::map<uint32, std::vector<std::string>>> areaNames;

    for (uint32 level = 1; level < sPlayerbotAIConfig.randomBotMaxLevel + 1; level++)
    {
        for (uint32 r = 1; r < MAX_RACES; r++)
        {
            for (auto p : rpgLocsCacheLevel[r][level])
            {
                areaNames[level][r].push_back(WorldPosition(p).getAreaName(true, true));
            }
        }
    }

    std::vector<std::pair<std::pair<uint32, uint32>, WorldPosition>> newPoints;
    std::vector<std::pair<std::pair<uint32, uint32>, GuidPosition>> innPoints;

    //Static portals.
    for (auto& goData : WorldPosition().getGameObjectsNear(0, 0))
    {
        GuidPosition go(goData);

        auto data = sGOStorage.LookupEntry<GameObjectInfo>(go.GetEntry());

        if (!data)
            continue;

        if (data->type != GAMEOBJECT_TYPE_SPELLCASTER)
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(data->spellcaster.spellId);

        if (pSpellInfo->EffectTriggerSpell[0])
            pSpellInfo = sServerFacade.LookupSpellInfo(pSpellInfo->EffectTriggerSpell[0]);

        if (pSpellInfo->Effect[0] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[1] != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->Effect[2] != SPELL_EFFECT_TELEPORT_UNITS)
            continue;

        SpellTargetPosition const* pos = sSpellMgr.GetSpellTargetPosition(pSpellInfo->Id);

        if (!pos)
            continue;

        std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(pos), areaNames);

        for (auto& range : ranges)
            newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), pos));
    }

    //Creatures.
    for (auto& creatureData : WorldPosition().getCreaturesNear(0, 0))
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(creatureData->second.creature_id[0]);

        if (!cInfo)
            continue;

        if (cInfo->ExtraFlags & CREATURE_EXTRA_FLAG_INVISIBLE)
            continue;

        std::vector<uint32> allowedNpcFlags;

        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BATTLEMASTER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_BANKER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_AUCTIONEER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_TRAINER);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_VENDOR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_REPAIR);
        allowedNpcFlags.push_back(UNIT_NPC_FLAG_INNKEEPER);

        for (auto flag : allowedNpcFlags)
        {          
            if ((cInfo->NpcFlags & flag) != 0)
            {
                std::vector<std::pair<uint32, uint32>> ranges = RpgLocationsNear(WorldPosition(creatureData), areaNames);

                if (cInfo->NpcFlags & UNIT_NPC_FLAG_INNKEEPER)
                {
                    for (auto& range : ranges)
                        innPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                else
                {
                    for (auto& range : ranges)
                        newPoints.push_back(std::make_pair(std::make_pair(range.first, range.second), creatureData));
                }
                break;
            }
        }
    }

    for (auto newPoint : newPoints)
        rpgLocsCacheLevel[newPoint.first.first][newPoint.first.second].push_back(newPoint.second);
    
    for (auto innPoint : innPoints)
        innCacheLevel[innPoint.first.first][innPoint.first.second].push_back(std::make_pair(innPoint.second, innPoint.second));
}

void RandomPlayerbotMgr::PrintTeleportCache()
{
    sPlayerbotAIConfig.openLog("telecache.csv", "w");

    for (auto& l : sRandomPlayerbotMgr.locsPerLevelCache)
    {
        uint32 level = l.first;
        for (auto& p : l.second)
        {
            std::ostringstream out;
            out << level << ",";
            WorldPosition(p).printWKT(out);
            out << "LEVEL" << ",0," << WorldPosition(p).getAreaName(true, true);
            sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
        }
    }

    for (auto r : sRandomPlayerbotMgr.rpgLocsCacheLevel)
    {
        uint32 race =  r.first;
        for (auto& l : r.second)
        {
            uint32 level = l.first;
            for (auto& p : l.second)
            {
                std::ostringstream out;
                out << level << ",";
                WorldPosition(p).printWKT(out);
                out << "RPG" << "," << race << "," << WorldPosition(p).getAreaName(true, true);
                sPlayerbotAIConfig.log("telecache.csv", out.str().c_str());
            }
        }
    }
}

// #421 C: another race's starting area of the bot's faction up to level 10,
// otherwise a level-appropriate inn of another race of the faction. The home
// point is not changed. Empty = no rescue (fail closed).
std::vector<WorldLocation> RandomPlayerbotMgr::QuestRescueTargets(Player* bot)
{
    std::vector<WorldLocation> targets;
    uint32 const level = bot->GetLevel();
    WorldPosition const botPos(bot);

    auto firstInfo = [](uint32 race) -> PlayerInfo const*
    {
        for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
            if (PlayerInfo const* info = sObjectMgr.GetPlayerInfo(race, cls))
                return info;
        return nullptr;
    };
    PlayerInfo const* own = firstInfo(bot->getRace());

    // Hotfix 8.29 (twow-repo#532): a goblin below 11 goes to Durotar, where its route continues.
    if (ai::quest_search::goblin_island::UsesGoblinStartRescue(bot->getRace(), level))
    {
        namespace gi = ai::quest_search::goblin_island;
        targets.push_back(WorldLocation(1, gi::PortMachineX, gi::PortMachineY, gi::PortMachineZ, 0.0f));
        targets.push_back(WorldLocation(1, gi::RazorHillX, gi::RazorHillY, gi::RazorHillZ, 0.0f));
        return targets;
    }

    // twow-repo#587 (Z1a, AiPlayerbot.ZoneExit.RescueHomeContinent): only targets on one continent - the bot's own, or its home
    // continent when it is off it (then its own race counts too: the way home). The cross-continent quest route rule
    // (#544) is untouched: the rescue is a teleport.
    bool const homeContinent = sPlayerbotAIConfig.zoneExitRescueHomeContinent && own && bot->GetMap() && bot->GetMap()->IsContinent();
    uint32 const rescueMap = homeContinent ? ai::zone_exit::RescueContinent(bot->GetMapId(), own->mapId) : bot->GetMapId();
    bool const offHome = homeContinent && rescueMap != bot->GetMapId();

    for (uint32 race = 1; race < MAX_RACES; ++race)
    {
        PlayerInfo const* info = firstInfo(race);
        if (!info || (race == bot->getRace() && !offHome) || Player::TeamForRace(race) != bot->GetTeam())
            continue;

        if (level <= ai::quest_search::StartAreaMaxLevel)
        {
            if (own && info->areaId == own->areaId && !offHome)
                continue;
            // twow-repo#575 (PATH-01 E6): no rescue of another race into the goblin / high-elf start zones.
            if (ai::path_fail::RescueSkipsStartOf(race, bot->getRace(), sPlayerbotAIConfig.pathRescueSkipCustomStarts))
                continue;
            targets.push_back(WorldLocation(info->mapId, info->positionX, info->positionY, info->positionZ, info->orientation));
            continue;
        }

        // Goblin / high elf 11-20: the hubs OB-50 named (spawn points, cached).
        if (ai::quest_search::UsesRescueAnchors(race, level))
        {
            for (uint32 entry : ai::quest_search::RescueAnchorEntries(race))
            {
                auto cached = questRescueAnchors.find(entry);
                if (cached == questRescueAnchors.end())
                {
                    std::vector<WorldLocation> spawns;
                    for (CreatureDataPair const* creature : WorldPosition().getCreaturesNear(0, entry))
                        spawns.push_back(WorldPosition(creature));
                    cached = questRescueAnchors.emplace(entry, spawns).first;
                }
                for (WorldLocation const& spawn : cached->second)
                    if (botPos.distance(WorldPosition(spawn)) > ai::quest_search::RescueMinDistance)
                        targets.push_back(spawn);
            }
            continue;
        }

        for (auto const& [innGuid, innLocation] : innCacheLevel[race][level])
            if (botPos.distance(WorldPosition(innLocation)) > ai::quest_search::RescueMinDistance)
                targets.push_back(innLocation);
    }

    if (homeContinent)
    {
        size_t const before = targets.size();
        targets.erase(std::remove_if(targets.begin(), targets.end(),
            [rescueMap](WorldLocation const& t) { return t.mapid != rescueMap; }), targets.end());
        sLog.outBasic("[ZoneExit] state=rescue_targets bot=%u level=%u map=%u home_map=%u off_home=%u targets=%u of %u",
            bot->GetGUIDLow(), level, bot->GetMapId(), own->mapId, offHome ? 1u : 0u, uint32(targets.size()), uint32(before));
    }
    return targets;
}

void RandomPlayerbotMgr::ProcessQuestRescues()
{
    uint32 const now = uint32(time(nullptr));
    if (now - lastQuestRescueScan < 10)
        return;
    lastQuestRescueScan = now;

    for (auto const& [guid, bot] : GetAllBots())
    {
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        // twow-repo#587 (AiPlayerbot.QuestRescue.KeepRequestWhenFull, soak 17.10.: 20 of 21 stranded not served promptly):
        // with the global limit used up a pending request stays with the bot for the next scan instead of being taken and
        // dropped - a stranded bot (Z1b) only asks again after StrandedRescueAfter more empty searches.
        if (sPlayerbotAIConfig.questRescueKeepRequestWhenFull && ai && ai->HasQuestRescueRequest() &&
                !questRescueLimiter.HasCapacity(now))
            break;
        // twow-repo#541/#551: a parked bot stands still on purpose - no rescue teleport.
        if (!ai || !ai->TakeQuestRescueRequest() || IsParkedBot(guid))
            continue;

        // The bot asks again next minute while it stays stuck.
        if (!questRescueLimiter.TryAcquire(now))
            continue;

        std::vector<WorldLocation> const targets = QuestRescueTargets(bot);
        if (targets.empty())
        {
            sLog.outBasic("[QuestRescue] state=no_target bot=%u level=%u race=%u zone=%u",
                bot->GetGUIDLow(), bot->GetLevel(), uint32(bot->getRace()), bot->GetZoneId());
            ai->OnQuestRescued(now);
            continue;
        }

        WorldLocation const& target = targets[urand(0, uint32(targets.size()) - 1)];
        sLog.outBasic("[QuestRescue] state=teleport bot=%u level=%u race=%u from_map=%u from_zone=%u to_map=%u to_x=%.0f to_y=%.0f idle_min=%u",
            bot->GetGUIDLow(), bot->GetLevel(), uint32(bot->getRace()), bot->GetMapId(), bot->GetZoneId(),
            target.mapid, target.coord_x, target.coord_y, ai->GetQuestIdleSeconds(now) / 60);

        bot->GetMotionMaster()->Clear();
        bot->TeleportTo(target.mapid, target.coord_x, target.coord_y, target.coord_z, target.orientation);

        // Hotfix 8.26 (v33, Rollotheo 4140): the hearthstone stayed bound to the start area, so the
        // next "long stuck" hearthstone (or the 8.14 home step) took the bot straight back to where
        // its route had failed. A rescued roster bot binds its hearthstone at the rescue target
        // (an inn or a level hub of its faction).
        if (ai::quest_search::RebindAtRescue(IsPersistentRosterMember(bot->GetGUIDLow()), ai->HasRealPlayerMaster(),
                bot->GetHomebindMapId(), target.mapid))
        {
            AreaTableEntry const* area = WorldPosition(target).GetArea();
            bot->SetHomebindToLocation(target, area ? area->ID : 0);
            sLog.outBasic("[QuestRescue] state=rebind bot=%u level=%u to_map=%u to_x=%.0f to_y=%.0f area=%u",
                bot->GetGUIDLow(), bot->GetLevel(), target.mapid, target.coord_x, target.coord_y, area ? area->ID : 0);
        }

        bot->SendHeartBeat();
        ai->Reset(true);
        // twow-repo#601 S1 (ResetOnProgressEvents): the rescue teleport starts the quest route search afresh (counter,
        // backoff, stage) in the bot's own update - the no_target branch above moves nothing and resets nothing.
        if (sPlayerbotAIConfig.questFirstProgressionResetOnProgressEvents)
            ai->RequestQuestRouteReset(ai::quest_failure_class::ResetRescue);
        ai->OnQuestRescued(now);
    }
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot, bool activeOnly)
{
    if (bot->InBattleGround())
        return;

    sLog.outDetail("Preparing location to random teleporting bot %s for level %u", bot->GetName(), bot->GetLevel());
    RandomTeleport(bot, locsPerLevelCache[bot->GetLevel()], false, activeOnly);
    Refresh(bot);

    WorldPosition botPos(bot);

    ObjectGuid closestInn;
    float minDistance = -1.0f;
    for (auto& [innGuid, innPosition] : innCacheLevel[bot->getRace()][bot->GetLevel()])
    {
        float distance = botPos.sqDistance(innPosition);
        if (minDistance > 0 || distance >= minDistance)
            continue;

        minDistance = distance;
        closestInn = innGuid;
    }

    if (closestInn)
    {
        WorldPacket data(SMSG_TRAINER_BUY_SUCCEEDED, (8 + 4));
        data << closestInn;
        data << uint32(3286);                                   // Bind
        bot->GetSession()->SendPacket(data);
    }
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot)
{
    if (bot->InBattleGround())
        return;

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomTeleport");
    std::vector<WorldLocation> locs;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.randomBotTeleportDistance;
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    if (!targets.empty())
    {
        for (std::list<Unit *>::iterator i = targets.begin(); i != targets.end(); ++i)
        {
            Unit* unit = *i;
            bot->SetPosition(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), 0);
            FleeManager manager(bot, sPlayerbotAIConfig.sightDistance, 0, true);
            float rx, ry, rz;
            if (manager.CalculateDestination(&rx, &ry, &rz))
            {
                WorldLocation loc(bot->GetMapId(), rx, ry, rz);
                locs.push_back(loc);
            }
        }
    }
    else
    {
        RandomTeleportForLevel(bot, true);
    }

    pmo.reset();

    Refresh(bot);
}

void RandomPlayerbotMgr::InstaRandomize(Player* bot)
{
    sRandomPlayerbotMgr.Randomize(bot);

    if(bot->GetLevel() > sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        sRandomPlayerbotMgr.RandomTeleportForLevel(bot, false);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetSession()->isLogingOut())
        return;

    bool initialRandom = false;
    if (bot->GetLevel() <= sPlayerbotAIConfig.randombotStartingLevel)
        initialRandom = true;
#ifdef MANGOSBOT_TWO
    else if (bot->GetLevel() < 60 && bot->getClass() == CLASS_DEATH_KNIGHT)
        initialRandom = true;
#endif

    // give bot random level if is above or below level sync
    if (!initialRandom && players.size() && sPlayerbotAIConfig.syncLevelWithPlayers)
    {
        uint32 maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel + sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));
        if (bot->GetLevel() > maxLevel || (bot->GetLevel() + sPlayerbotAIConfig.syncLevelMaxAbove) < playersLevel)
            initialRandom = true;
    }

    if (initialRandom)
    {
        RandomizeFirst(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear/level randomised", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else if (sPlayerbotAIConfig.randomGearUpgradeEnabled)
    {
        UpdateGearSpells(bot);
        sLog.outDetail("Bot #%d %s:%d <%s>: gear upgraded", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    }
    else
    {
        // schedule randomise
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
    }

    //SetValue(bot, "version", MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::UpdateGearSpells(Player* bot)
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "UpgradeGear");

    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    uint32 lastLevel = GetValue(bot, "level");
    uint32 level = bot->GetLevel();
    PlayerbotFactory factory(bot, level);

    // This used to fork: with DisableRandomLevels set it called UpgradeGearBest()
    // directly, because Randomize() would have returned before reaching the gear.
    // It was a workaround for that early return, and it treated the symptom --
    // bots got equipment and still no spells, skills or talents, which is the
    // half-initialised state this whole function is named for.
    //
    // Randomize() no longer skips anything for this flag, so the workaround is
    // not just unnecessary, it is now the thing holding the bug in place.
    factory.Randomize(true, false);

    if (lastLevel != level)
        SetValue(bot, "level", level);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    if (bot && IsPersistentRosterMember(bot->GetGUIDLow()))
        return;
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    // if lvl sync is enabled, max level is limited by online players lvl
    if (sPlayerbotAIConfig.syncLevelWithPlayers)
        maxLevel = std::max(sPlayerbotAIConfig.randomBotMinLevel, std::min(playersLevel+ sPlayerbotAIConfig.syncLevelMaxAbove, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL)));

    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "RandomizeFirst");
    uint32 level = urand(std::max(uint32(sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL)), sPlayerbotAIConfig.randomBotMinLevel), maxLevel);

#ifdef MANGOSBOT_TWO
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
        level = urand(std::max(bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL)), std::max(sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL), maxLevel));
#endif

    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomBotMaxLevelChance && level < maxLevel)
        level = maxLevel;

#ifndef MANGOSBOT_ZERO
    if (sWorldState.GetExpansion() == EXPANSION_NONE && level > 60)
        level = 60;
#endif

#ifdef MANGOSBOT_TWO
    // do not allow level down death knights
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < sWorld.getConfig(CONFIG_UINT32_START_HEROIC_PLAYER_LEVEL))
        return;

    // only randomise death knights to min lvl 60
    if (bot->getClass() == CLASS_DEATH_KNIGHT && level < 60)
        level = 60;
#endif

    // A bot rolled to exactly the starting level needs no level change, which is
    // all this return was ever about -- but it leaves before the factory runs, so
    // it also skips spells, skills, talents and gear. With DisableRandomLevels the
    // rolled value is discarded anyway and the bot is deliberately started low, so
    // this return would fire for a large share of the population and leave those
    // bots permanently uninitialised.
    if (!sPlayerbotAIConfig.disableRandomLevels &&
        level == sWorld.getConfig(CONFIG_UINT32_START_PLAYER_LEVEL))
        return;

    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false, false);

    // schedule randomise
    uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
    SetEventValue(bot->GetGUIDLow(), "randomize", 1, randomTime);

    bool hasPlayer = GetBotAI(bot)->HasRealPlayerMaster();
    GetBotAI(bot)->Reset(!hasPlayer);

    if (bot->GetGroup() && !hasPlayer)
        bot->RemoveFromGroup();
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ)
{
	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

	uint32 level;
    auto results = WorldDatabase.PQuery("SELECT AVG(t.level_min) minlevel, AVG(t.level_max) maxlevel FROM creature c "
            "INNER JOIN creature_template t ON c.id = t.entry "
            "WHERE map = '%u' AND t.level_min > 1 AND abs(position_x - '%f') < '%u' AND abs(position_y - '%f') < '%u'",
            mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY, sPlayerbotAIConfig.randomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        uint8 minLevel = fields[0].GetUInt8();
        uint8 maxLevel = fields[1].GetUInt8();
        level = urand(minLevel, maxLevel);
        if (level > maxLevel)
            level = maxLevel;
    }
    else
    {
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    if (bot->IsBeingTeleportedFar() || !bot->IsInWorld())
        return;

    if (sServerFacade.UnitIsDead(bot))
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        GetBotAI(bot)->ResetStrategies();
    }

    // The third early return of the same shape, and the one that explains the
    // empty purses: everything below repairs, heals, restores power, refreshes
    // consumables and grants a little money. None of it is a level change, and
    // all of it reads bot->GetLevel() rather than any rolled value, so there was
    // never anything here for this flag to protect. On the realm where this was
    // found, 16 of 5,039 bots had any money at all.
    if (bot->InBattleGround())
        return;

    sLog.outDetail("Refreshing bot #%d <%s>", bot->GetGUIDLow(), bot->GetName());
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "Refresh");

    GetBotAI(bot)->Reset();

    bot->DurabilityRepairAll(false, 1.0f
#ifndef MANGOSBOT_ZERO
        , false
#endif
    );
	bot->SetHealthPercent(100);
	bot->SetPvP(true);

    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));

    uint32 money = bot->GetMoney();
    bot->SetMoney(money + 500 * sqrt(urand(1, bot->GetLevel() * 5)));
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    if (bot && GetBotAI(bot))
    {
        if (GetBotAI(bot)->IsRealPlayer())
            return false;
    }
    if (bot)
    {
        if (sPlayerbotAIConfig.IsInRandomAccountList(bot->GetSession()->GetAccountId()))
            return true;

        return IsRandomBot(bot->GetGUIDLow());
    }

    return false;
}

bool RandomPlayerbotMgr::IsRandomBot(uint32 bot)
{
    ObjectGuid guid = ObjectGuid(HIGHGUID_PLAYER, bot);
    if (sPlayerbotAIConfig.IsInRandomAccountList(sObjectMgr.GetPlayerAccountIdByGUID(guid)))
        return true;

    return GetEventValue(bot, "add");
}

std::list<uint32> RandomPlayerbotMgr::GetBots()
{
    if (sPlayerbotAIConfig.persistentActiveRosterEnabled)
    {
        if (!persistentRosterInitialized)
            InitializePersistentRoster();
        std::list<uint32> desired;
        if (PersistentRosterAdmissionOpen())
            desired.assign(persistentRoster->Desired().begin(), persistentRoster->Desired().end());
        currentBots = desired;
        return desired;
    }
    if (!currentBots.empty()) return currentBots;

    auto results = CharacterDatabase.Query(PlayerbotDatabaseContract::EventStoreSql(
            "SELECT bot FROM ", " WHERE owner = 0 AND event = 'add'").c_str());

    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            currentBots.push_back(bot);
        } while (results->NextRow());
    }

    return currentBots;
}

std::list<uint32> RandomPlayerbotMgr::GetBgBots(uint32 bracket)
{
    //if (!currentBgBots.empty()) return currentBgBots;

    auto results = CharacterDatabase.PQuery(PlayerbotDatabaseContract::EventStoreSql(
        "SELECT bot FROM ", " WHERE event = 'bg' AND value = '%d'").c_str(), bracket);
    std::list<uint32> BgBots;
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            BgBots.push_back(bot);
        } while (results->NextRow());
    }

    return BgBots;
}

bool RandomPlayerbotMgr::HasCachedEvents(uint32 bot) const
{
    ai::context_lock::Shared const lock(eventCacheMutex, ai::context_lock::On());
    auto const cached = eventCache.find(bot);
    return cached != eventCache.end() && !cached->second.empty();
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, std::string event)
{
    // load all events at once on first event load
    // Audit A28 (twow-repo#563): the query runs outside the lock into a local map; the merge keeps entries
    // another thread stored meanwhile (emplace never overwrites). Switch off: the same steps as before.
    if (!HasCachedEvents(bot))
    {
        std::map<std::string, CachedEvent> loaded;
        auto results = CharacterDatabase.PQuery(PlayerbotDatabaseContract::EventStoreSql(
            "SELECT `event`, `value`, `time`, validIn, `data` FROM ", " WHERE owner = 0 AND bot = '%u'").c_str(), bot);
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                // defensive null-handling.
                // Penqle's Field::GetString() returns the bare pointer without null-check;
                // constructing std::string from nullptr is UB and segfaults. The bot's
                // ai_playerbot_random_bots table has `data` column nullable, and most rows
                // store NULL there. Guard against null so the cache load doesn't crash.
                const char* nameStr = fields[0].GetString();
                const char* dataStr = fields[4].GetString();
                std::string eventName = nameStr ? nameStr : "";
                if (eventName.empty())
                    continue; // skip malformed rows entirely
                CachedEvent e;
                e.value = fields[1].GetUInt32();
                e.lastChangeTime = fields[2].GetUInt32();
                e.validIn = fields[3].GetUInt32();
                e.data = dataStr ? std::string(dataStr) : std::string();
                loaded[eventName] = e;
            } while (results->NextRow());
        }

        ai::context_lock::Unique const lock(eventCacheMutex, ai::context_lock::On());
        std::map<std::string, CachedEvent>& cached = eventCache[bot];
        for (auto const& [eventName, loadedEvent] : loaded)
            cached.emplace(eventName, loadedEvent);
    }

    CachedEvent e;
    bool found = false;
    {
        ai::context_lock::Shared const lock(eventCacheMutex, ai::context_lock::On());
        auto const cached = eventCache.find(bot);
        if (cached != eventCache.end())
        {
            auto const stored = cached->second.find(event);
            if (stored != cached->second.end())
            {
                e = stored->second;
                found = true;
            }
        }
    }
    if (!found)
    {
        // As before: an unknown event is remembered as empty, so a bot without any stored event is not
        // queried again on every call.
        ai::context_lock::Unique const lock(eventCacheMutex, ai::context_lock::On());
        e = eventCache[bot][event];
    }

    if ((time(0) - e.lastChangeTime) >= e.validIn && event != "specNo" && event != "specLink" && event != "init" && event != "current_time" && event != "always" && event != "selfbot" && event != ai::profession::kEventName)
        e.value = 0;

    return e.value;
}

int32 RandomPlayerbotMgr::GetValueValidTime(uint32 bot, std::string event)
{
    CachedEvent e;
    {
        ai::context_lock::Shared const lock(eventCacheMutex, ai::context_lock::On());   // audit A28
        auto const cached = eventCache.find(bot);
        if (cached == eventCache.end())
            return 0;

        auto const stored = cached->second.find(event);
        if (stored == cached->second.end())
            return 0;

        e = stored->second;
    }

    return e.validIn-(time(0) - e.lastChangeTime);
}

std::string RandomPlayerbotMgr::GetEventData(uint32 bot, std::string event)
{
    std::string data = "";
    if (GetEventValue(bot, event))
    {
        // GetEventValue stored the entry; read it without inserting (audit A28).
        ai::context_lock::Shared const lock(eventCacheMutex, ai::context_lock::On());
        auto const cached = eventCache.find(bot);
        if (cached != eventCache.end())
        {
            auto const stored = cached->second.find(event);
            if (stored != cached->second.end())
                data = stored->second.data;
        }
    }
    return data;
}

// One event write is one statement inside one transaction, serialized against
// every other write to the same (owner, bot, event).
//
// The DELETE-then-INSERT pair this replaced was neither atomic nor ordered.
// Two worker threads writing the same key could interleave as DELETE(A),
// DELETE(B), INSERT(B), INSERT(A) and leave the older value winning; a crash
// between the two statements dropped the row entirely; and two INSERTs that
// both landed after both DELETEs left two rows for one key that nothing ever
// cleaned up.
//
// Two separate mechanisms are at work here and they cover different ground.
// EventWriteSerialId hashes (owner, bot, event) into a serialization slot, so
// concurrent writes to one bot's one event are ordered while writes to other
// bots or other events still run in parallel. That is a per-key lock inside
// this process only: it says nothing about a second mangosd, a migration, or
// somebody at a mysql prompt. The UNIQUE KEY uq_owner_bot_event added by
// sql/database_updates/character/20260906120000_ai_playerbot_random_bots_unique_event_key.sql
// is what makes a duplicate row impossible against any writer at all, and it
// is what turns EventUpsertSql's ON DUPLICATE KEY UPDATE into an upsert rather
// than a plain append. Neither mechanism substitutes for the other.
uint32 RandomPlayerbotMgr::SetEventValue(uint32 bot, std::string event, uint32 value, uint32 validIn, std::string data)
{
    if (event.empty())
    {
        sLog.outError("[PlayerBotEventStore] Refusing to write an empty event key for bot %u.", bot);
        return 0;
    }

    bool const ownsTransaction = !CharacterDatabase.InTransaction();
    if (ownsTransaction && !CharacterDatabase.BeginTransaction(
        PlayerbotDatabaseContract::EventWriteSerialId(0, bot, event)))
    {
        sLog.outError("[PlayerBotEventStore] Failed to open the serialized write for bot %u event %s.",
            bot, event.c_str());
        return 0;
    }

    // The statement ids are deliberately local rather than function-static.
    // Database::CreateStatement only consults the format string while the id is
    // uninitialized, so a static id would bind permanently to whichever table
    // name AiPlayerbot.EventStoreTable held on the very first write. Every other
    // event-store query in this file rebuilds its SQL from the config on each
    // call, and a .reload config that repointed the table would otherwise leave
    // this one write path still writing to the old table. A fresh id costs one
    // registry lookup and keeps the config-driven table name honest.
    bool queued = false;
    if (value)
    {
        uint32 const changedAt = (uint32)time(0);
        if (data.empty())
        {
            SqlStatementID upsertWithoutData;
            SqlStatement statement = CharacterDatabase.CreateStatement(
                upsertWithoutData, PlayerbotDatabaseContract::EventUpsertSql(false).c_str());
            queued = statement.PExecute(bot, changedAt, validIn, event.c_str(), value);
        }
        else
        {
            SqlStatementID upsertWithData;
            SqlStatement statement = CharacterDatabase.CreateStatement(
                upsertWithData, PlayerbotDatabaseContract::EventUpsertSql(true).c_str());
            queued = statement.PExecute(bot, changedAt, validIn, event.c_str(), value, data.c_str());
        }
    }
    else
    {
        SqlStatementID deleteEvent;
        SqlStatement statement = CharacterDatabase.CreateStatement(
            deleteEvent, PlayerbotDatabaseContract::EventDeleteSql().c_str());
        queued = statement.PExecute(uint32(0), bot, event.c_str());
    }

    if (!queued)
    {
        if (ownsTransaction)
            CharacterDatabase.RollbackTransaction();
        sLog.outError("[PlayerBotEventStore] Failed to queue the write for bot %u event %s.",
            bot, event.c_str());
        return 0;
    }

    if (ownsTransaction)
    {
        std::function<void(bool)> completion = [bot, event](bool success)
        {
            if (!success)
                sLog.outError("[PlayerBotEventStore] Database write failed for bot %u event %s.",
                    bot, event.c_str());
        };
        if (!CharacterDatabase.CommitTransaction(&completion))
        {
            CharacterDatabase.RollbackTransaction();
            sLog.outError("[PlayerBotEventStore] Failed to commit the serialized write for bot %u event %s.",
                bot, event.c_str());
            return 0;
        }
    }

    // Only reached once the write is on its way, so a rejected write leaves the
    // cache agreeing with the table rather than inventing a value nothing stored.
    CachedEvent e(value, (uint32)time(0), validIn, data);
    {
        ai::context_lock::Unique const lock(eventCacheMutex, ai::context_lock::On());   // audit A28
        eventCache[bot][event] = e;
    }
    return value;
}

uint32 RandomPlayerbotMgr::GetProfessionPair(uint32 bot)
{
    if (GetEventData(bot, ai::profession::kEventName) != ai::profession::kEventData)
        return 0;

    uint32 const pair = GetEventValue(bot, ai::profession::kEventName);
    return ai::profession::IsValid(pair) ? pair : 0;
}

uint32 RandomPlayerbotMgr::SetProfessionPair(uint32 bot, uint32 pair)
{
    if (!ai::profession::IsValid(pair))
        return 0;
    return SetEventValue(bot, ai::profession::kEventName, pair, std::numeric_limits<uint32>::max(), ai::profession::kEventData);
}

uint32 RandomPlayerbotMgr::GetValue(uint32 bot, std::string type)
{
    return GetEventValue(bot, type);
}

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string type)
{
    return GetValue(bot->GetObjectGuid().GetCounter(), type);
}

std::string RandomPlayerbotMgr::GetData(uint32 bot, std::string type)
{
    return GetEventData(bot, type);
}

void RandomPlayerbotMgr::SetValue(uint32 bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    SetEventValue(bot, type, value, validIn == -1 ? 15*24*3600 : validIn, data);
}

void RandomPlayerbotMgr::SetValue(Player* bot, std::string type, uint32 value, std::string data, int32 validIn)
{
    SetValue(bot->GetObjectGuid().GetCounter(), type, value, data, validIn);
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        sLog.outError("Playerbot system is currently disabled!");
        return false;
    }

    bool isRA = false;
    
    if (handler->GetSession()) //Client command
        isRA = true;
    else if (static_cast<CliHandler*>(handler) && static_cast<CliHandler*>(handler)->GetAccountId()) //RA call with account.
        isRA = true;

    if (!args || !*args)
    {
        sLog.outError("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");
        if (isRA)
            handler->SendSysMessage("Usage: rndbot help/stats/update/reset/init/refresh/add/remove/more..");

        std::list<std::string> messages = sRandomPlayerbotMgr.HandleHelp("");

        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if (isRA)
                handler->SendSysMessage(msg.c_str());
        }

        return true;
    }

    std::string cmd = args;

    // #354: `.rndbot` acts on the whole random/roster population and is
    // registered at player level. From a game session it needs GM rank; the
    // console keeps full access and roster admin stays console-only below.
    if (handler->GetSession() &&
        !ai::roster_control::IsGmBypass(handler->GetSession()->GetSecurity(), sPlayerbotAIConfig.rosterControlGmMinSecurity) &&
        !ai::roster_control::IsRndbotCommandAllowedForPlayer(cmd))
    {
        handler->SendSysMessage("GM only");
        return true;
    }
    if (handler->GetSession() && !ai::roster_control::IsRndbotCommandAllowedForPlayer(cmd))
        sLog.outBasic("[BotCtl] cmd=rndbot issuer=%u args='%s' result=gm_bypass",
            handler->GetSession()->GetPlayer() ? handler->GetSession()->GetPlayer()->GetGUIDLow() : 0, cmd.c_str());

    if (cmd == "roster status" || cmd.find("roster apply ") == 0 || cmd == "roster apply")
    {
        ai::roster::AdminTransport transport = handler->GetSession() ? ai::roster::AdminTransport::GAME_CHAT :
            (static_cast<CliHandler*>(handler)->GetAccountId() == 0 ? ai::roster::AdminTransport::LOCAL_CONSOLE :
                ai::roster::AdminTransport::REMOTE_ADMIN);
        if (!ai::roster::IsRosterAdminTransportAllowed(transport))
        {
            sLog.outError("[PersistentRoster] admin command rejected: local mangosd console required");
            if (isRA)
                handler->SendSysMessage("Persistent roster administration is restricted to the local mangosd console.");
            return true;
        }

        if (cmd == "roster status")
        {
            if (sPlayerbotAIConfig.persistentActiveRosterEnabled && !sRandomPlayerbotMgr.persistentRosterInitialized)
                sRandomPlayerbotMgr.InitializePersistentRoster();
            ai::roster::RuntimeStatus status;
            if (sRandomPlayerbotMgr.persistentRoster)
                status = sRandomPlayerbotMgr.persistentRoster->Status();
            std::ostringstream line;
            line << "PERSISTENT_ROSTER state=" << sRandomPlayerbotMgr.GetPersistentRosterState()
                << " maintenance=" << (sPlayerbotAIConfig.persistentActiveRosterMaintenanceMode ? 1 : 0)
                << " version=" << status.rosterVersionId << " target=" << status.rosterTarget
                << " available=" << status.rosterAvailable << " online=" << status.rosterOnline
                << " sha256=" << ai::roster::Hex(status.rosterSha256);
            sLog.outString("%s", line.str().c_str());
            for (auto const& diagnostic : status.diagnostics)
                sLog.outString("PERSISTENT_ROSTER_DIAGNOSTIC guid=%u code=%s", diagnostic.first, diagnostic.second.c_str());
            return true;
        }

        std::string fileName = cmd.size() > 13 ? cmd.substr(13) : "";
        std::string canonicalRequest;
        std::string result;
        if (!ai::roster::ReadCanonicalAdminRequestFile(fileName, canonicalRequest, result))
        {
            sLog.outError("[PersistentRoster] apply rejected code=%s", result.c_str());
            return true;
        }
        if (!sRandomPlayerbotMgr.persistentRosterInitialized)
            sRandomPlayerbotMgr.InitializePersistentRoster();
        sRandomPlayerbotMgr.ApplyPersistentRosterRequest(canonicalRequest, result);
        sLog.outString("PERSISTENT_ROSTER_APPLY result=%s", result.c_str());
        return true;
    }

    std::map<std::string, ConsoleCommandHandler> handlers;
    handlers["help"] = &RandomPlayerbotMgr::HandleHelp;
    handlers["reset"] = &RandomPlayerbotMgr::HandleConsoleReset;
    handlers["stats"] = &RandomPlayerbotMgr::HandleConsoleStats;
    handlers["update"] = &RandomPlayerbotMgr::HandleConsoleUpdate;
    handlers["pid "] = &RandomPlayerbotMgr::HandleConsolePid;
    handlers["diff"] = &RandomPlayerbotMgr::HandleConsoleDiff;
    handlers["diff "] = &RandomPlayerbotMgr::HandleConsoleDiff;
    handlers["clean map"] = &RandomPlayerbotMgr::HandleConsoleCleanMap;
    handlers["login debug"] = &RandomPlayerbotMgr::HandleConsoleLoginDebug;
    handlers["park"] = &RandomPlayerbotMgr::HandleConsolePark;
    handlers["unpark"] = &RandomPlayerbotMgr::HandleConsoleUnpark;

    for (auto& [prefix, consoleHandler] : handlers)
    {
        if (cmd.find(prefix) != 0)
            continue;

        size_t prefixLen = prefix.size();
        std::string param = cmd.size() > prefixLen + 1 ? cmd.substr(prefixLen + 1) : "";

        if (prefix == "stats")
            param = handler->GetSession() ? std::to_string(handler->GetSession()->GetPlayer()->GetObjectGuid()) : "";

        std::list<std::string> messages = (sRandomPlayerbotMgr.*consoleHandler)(param);
        for (auto& msg : messages)
        {
            sLog.outString("%s", msg.c_str());
            if(isRA)
                handler->SendSysMessage(msg.c_str());      
        }

        if (!messages.empty() && (prefix != "help" || param != "commands"))
            return true;
    }

    std::map<std::string, ConsolePlayerCommandHandler> playerHandlers;
    playerHandlers["init"] = &RandomPlayerbotMgr::HandleRandomizeFirst;
    playerHandlers["upgrade"] = &RandomPlayerbotMgr::HandleUpdateGearSpells;
    playerHandlers["refresh"] = &RandomPlayerbotMgr::HandleRefresh;
    playerHandlers["teleport"] = &RandomPlayerbotMgr::HandleRandomTeleportForLevel;
    playerHandlers["rpg"] = &RandomPlayerbotMgr::HandleRandomTeleportForRpg;
    playerHandlers["revive"] = &RandomPlayerbotMgr::HandleRevive;
    playerHandlers["grind"] = &RandomPlayerbotMgr::HandleRandomTeleport;
    playerHandlers["change_strategy"] = &RandomPlayerbotMgr::HandleChangeStrategy;
    playerHandlers["remove"] = &RandomPlayerbotMgr::HandleRemove;

    for (auto& [prefix, playerHandler] : playerHandlers)
    {
        if (cmd.find(prefix) != 0)
            continue;

        size_t prefixLen = prefix.size();
        std::string nameAndParams = cmd.size() > prefixLen + 1 ? cmd.substr(prefixLen + 1) : "";

        std::string name = "%";
        std::string params = "";

        if (!nameAndParams.empty())
        {
            size_t spacePos = nameAndParams.find(' ');
            if (spacePos != std::string::npos)
            {
                name = nameAndParams.substr(0, spacePos);
                params = nameAndParams.substr(spacePos + 1);
            }
            else
            {
                name = nameAndParams;
            }
        }

        sRandomPlayerbotMgr.consoleCmdParams = params;

        bool hasRandomBotCommand = false;

        ConsolePlayerCommandHandler handler_copy = playerHandler;

        sRandomPlayerbotMgr.ForEachPlayerbot([&](Player* bot) {
            std::string botName = bot->GetName();
            if (botName.find(name) == 0)
            {

                std::list<std::string> messages = (sRandomPlayerbotMgr.*handler_copy)(bot);
                for (auto& msg : messages)
                {
                    sLog.outString("%s", msg.c_str());
                    if (isRA)
                        handler->SendSysMessage(msg.c_str());
                    hasRandomBotCommand = true;
                }
            }
        });

        if (hasRandomBotCommand)
            return true;
    }

    std::list<std::string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args, handler->GetSession() ? handler->GetSession()->GetPlayer():nullptr, static_cast<CliHandler*>(handler) ? static_cast<CliHandler*>(handler)->GetAccessLevel() : SEC_PLAYER);
    for (std::list<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        sLog.outString("%s", i->c_str());
        if (isRA)
            handler->SendSysMessage(i->c_str());
    }

    if (!messages.empty())
        return true;

    if (isRA)
        handler->SendSysMessage("usage: help/list/reload/more.. or add/init/remove/more.. PLAYERNAME");

    return true;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, std::string channelName, Team team, uint32 lang, const std::string& to)
{
    ForEachPlayerbot([&](Player* bot)
    {
        if (type == CHAT_MSG_WHISPER && !to.empty() && bot->GetName() != to)
            return;

        if (type == CHAT_MSG_SAY)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 25)
            {
                return;
            }
        }

        if (type == CHAT_MSG_YELL)
        {
            if (bot->GetMapId() != fromPlayer.GetMapId() || sServerFacade.GetDistance2d(bot, &fromPlayer) > 300)
            {
                return;
            }
        }

        if (type == CHAT_MSG_PARTY || type == CHAT_MSG_RAID)
        {
            if (!fromPlayer.IsInGroup(bot, true))
            {
                return;
            }
        }

        if (team != TEAM_BOTH_ALLOWED && bot->GetTeam() != team)
        {
            return;
        }

        if (type == CHAT_MSG_GUILD && bot->GetGuildId() != fromPlayer.GetGuildId())
        {
            return;
        }

        if (!channelName.empty())
        {
            if (ChannelMgr* cMgr = channelMgr(bot->GetTeam()))
            {
                Channel* chn = cMgr->GetChannel(channelName, bot);
                if (!chn)
                {
                    return;
                }
            }
        }

        GetBotAI(bot)->HandleCommand(type, text, fromPlayer, lang);
    });
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    bool hadPlayerBot = GetPlayerBot(player->GetGUIDLow());

    if (IsPersistentRosterMember(player->GetGUIDLow()))
    {
        SetEventValue(player->GetGUIDLow(), "login", 0, 0);
        SchedulePersistentRosterRetry(player->GetGUIDLow(), "SESSION_ENDED");
    }
    // This hook observes a session that is already ending; it is cleanup,
    // not an ordinary rotation/logout request. The initiating ordinary paths
    // remain protected by the default false argument.
    DisablePlayerBot(player->GetGUIDLow(), true, true);

    if (!hadPlayerBot && GetBotAI(player) && GetBotAI(player)->IsRealPlayer() && player->GetGroup() && sPlayerbotAIConfig.IsFreeAltBot(player))
        player->GetSession()->SetOffline(); //Prevent groupkick

    ForEachPlayerbot([&](Player* bot) {
        PlayerbotAI* ai = GetBotAI(bot);
        if (player == ai->GetMaster())
        {
            ai->SetMaster(NULL);
            if (!bot->InBattleGround())
            {
                ai->ResetStrategies();
            }
        }
    });

    players.erase(player->GetGUIDLow());
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)
{
    sLog.outDetail("%u/%d Bot %s logged in", GetPlayerbotsAmount(), sRandomPlayerbotMgr.GetMaxAllowedBotCount(), bot->GetName());
    if (IsPersistentRosterMember(bot->GetGUIDLow()))
    {
        ClearPersistentRosterRetry(bot->GetGUIDLow());
        persistentRoster->RecordOnline(bot->GetGUIDLow());

        // This hook is invoked once by PlayerbotHolder after the bot login
        // has loaded the Player and attached its AI. Persistent-roster bots
        // bypass Randomize(), so backfill only their GUID-bound plan here.
        // Factory persistence failures are logged but never reject login.
        PlayerbotFactory(bot, bot->GetLevel()).EnsureProfessionPairPlan();
        ProvisionPersistentRosterStarterOutfit(bot);
        ProvisionPersistentRosterBags(bot);
    }
	//if (loginProgressBar && playerBots.size() < sRandomPlayerbotMgr.GetMaxAllowedBotCount()) { loginProgressBar->step(); }
	//if (loginProgressBar && playerBots.size() >= sRandomPlayerbotMgr.GetMaxAllowedBotCount() - 1) {
    //if (loginProgressBar && playerBots.size() + 1 >= sRandomPlayerbotMgr.GetMaxAllowedBotCount()) {
	//	sLog.outString("All bots logged in");
    //    delete loginProgressBar;
	//}
}

void RandomPlayerbotMgr::ProvisionPersistentRosterStarterOutfit(Player* bot)
{
    using namespace ai::roster::starter_outfit;
    if (!bot || !ShouldProvision(IsPersistentRosterMember(bot->GetGUIDLow()), bot->GetLevel()))
        return;

    PlayerInfo const* info = sObjectMgr.GetPlayerInfo(bot->GetRace(), bot->GetClass());
    if (!info)
    {
        sLog.outError("[PersistentRoster] starter outfit unavailable for bot %u: invalid race/class %u/%u",
            bot->GetGUIDLow(), bot->GetRace(), bot->GetClass());
        return;
    }

    std::map<uint32, uint32> required;
    for (PlayerCreateInfoItem const& item : info->item)
        if (item.item_id && item.item_amount)
            required[item.item_id] += item.item_amount;

    std::set<uint32> processed;
    for (PlayerCreateInfoItem const& item : info->item)
    {
        uint32 const itemId = item.item_id;
        if (!itemId || !processed.insert(itemId).second)
            continue;

        uint32 const missing = MissingAmount(required[itemId], bot->GetItemCount(itemId));
        if (!missing)
            continue;

        if (!bot->StoreNewItemInBestSlots(itemId, missing))
        {
            sLog.outError("[PersistentRoster] starter outfit incomplete for bot %u item %u missing=%u; retrying on next roster login",
                bot->GetGUIDLow(), itemId, missing);
            return;
        }
    }

    // This is the bounded equivalent of Player::Create's second pass. The
    // first canonical pass can leave an offhand weapon or shield in the main
    // backpack when the main hand did not exist yet. Never re-equip an item
    // that is already worn, and never inspect outer-bag slots or their
    // contents: only a canonical CreateInfo entry still in the main backpack
    // may be moved to an empty equipment slot.
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || required.find(item->GetEntry()) == required.end())
            continue;

        uint16 destination = 0;
        bool const canEquip = bot->CanEquipItem(NULL_SLOT, destination, item, false) == EQUIP_ERR_OK;
        bool const canUseAmmo = bot->CanUseAmmo(item->GetEntry()) == EQUIP_ERR_OK;
        switch (SelectSecondPassAction(true, false, canEquip, canUseAmmo))
        {
        case SecondPassAction::EQUIP:
            bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            bot->EquipItem(destination, item, true);
            break;
        case SecondPassAction::ACTIVATE_AMMO:
            bot->SetAmmo(item->GetEntry());
            break;
        case SecondPassAction::NONE:
            break;
        }
    }
}

void RandomPlayerbotMgr::ProvisionPersistentRosterBags(Player* bot)
{
    constexpr uint32 kPersistentRosterBagItemId = 50004;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(kPersistentRosterBagItemId);
    if (!proto || proto->Class != ITEM_CLASS_CONTAINER || proto->ContainerSlots != 36 || proto->BagFamily != BAG_FAMILY_NONE)
    {
        sLog.outError("[PersistentRoster] bag provisioner rejected item %u: missing or not a normal 36-slot bag",
            kPersistentRosterBagItemId);
        return;
    }

    using namespace ai::roster::bags;
    std::array<BagSlotState, kBagSlotCount> slots{};
    for (uint8 index = 0; index < kBagSlotCount; ++index)
    {
        uint8 const slot = INVENTORY_SLOT_BAG_START + index;
        Item* existing = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        slots[index].occupied = existing != nullptr;
    }

    auto const provision = SelectEmptySlots(slots);
    for (uint8 index = 0; index < kBagSlotCount; ++index)
    {
        if (!provision[index])
            continue;

        uint8 const slot = INVENTORY_SLOT_BAG_START + index;
        uint16 destination = 0;
        // `swap=false` is the no-overwrite part of the contract. A runtime
        // inventory change between the earlier read and this check cannot
        // turn this repair into an item replacement.
        InventoryResult const result = bot->CanEquipItem(slot, destination, proto, nullptr, false, false);
        if (result != EQUIP_ERR_OK)
        {
            sLog.outError("[PersistentRoster] bag provisioner cannot equip item %u for bot %u in slot %u: error=%u",
                kPersistentRosterBagItemId, bot->GetGUIDLow(), slot, static_cast<uint32>(result));
            continue;
        }

        if (!bot->EquipNewItem(destination, kPersistentRosterBagItemId, true))
        {
            sLog.outError("[PersistentRoster] bag provisioner failed to create or equip item %u for bot %u in slot %u",
                kPersistentRosterBagItemId, bot->GetGUIDLow(), slot);
        }
    }
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (!sPlayerbotAIConfig.enabled)
        return;

    ForEachPlayerbot([&](Player* bot)
    {
        if (player == bot)
            return;

        Group* group = bot->GetGroup();
        if (!group)
            return;

        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            PlayerbotAI* ai = GetBotAI(bot);
            if (member == player && (!ai->GetMaster() || GetBotAI(ai->GetMaster())))
            {
                if (!bot->InBattleGround())
                {
                    ai->SetMaster(player);
                    ai->ResetStrategies();
                    ai->TellPlayer(ai->GetMaster(), BOT_TEXT("hello"));
                }
                break;
            }
        }
    });

    if (IsFreeBot(player))
    {
        uint32 guid = player->GetGUIDLow();
        if (!sPlayerbotAIConfig.IsFreeAltBot(player))
           SetEventValue(guid, "login", 0, 0);
    }
    else
    {
        players[player->GetGUIDLow()] = player;
        sLog.outDebug("Including non-random bot player %s into random bot update", player->GetName());
    }
}

void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)
{
    if (IsPersistentRosterMember(bot))
    {
        SetEventValue(bot, "login", 0, 0);
        SchedulePersistentRosterRetry(bot, "LOGIN_FAILED");
        return;
    }
    SetEventValue(bot, "add", 0, 0);
    SetEventValue(bot, "login", 0, 0);
    currentBots.remove(bot);
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return NULL;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

Player* RandomPlayerbotMgr::GetPlayer(uint32 playerGuid)
{
    PlayerBotMap::const_iterator it = players.find(playerGuid);
    return (it == players.end()) ? nullptr : it->second ? it->second : nullptr;
}

void RandomPlayerbotMgr::PrintStats(uint32 requesterGuid)
{
    // World thread only: the walk below dereferences live Player* objects and
    // writes AI context caches, neither of which is safe off the world thread.
    std::chrono::steady_clock::time_point printStatsStarted = std::chrono::steady_clock::now();

    // A single resolve is enough: nothing between here and the end of the function
    // yields, so the requester cannot be logged out underneath us.
    Player* requester = GetPlayer(requesterGuid);
    std::stringstream ss; ss << GetPlayerbotsAmount() << " Random Bots online";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    std::map<uint32, int> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    std::map<uint8, int> perRace, perClass;
    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        perRace[race] = 0;
    }
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
    }

    uint32 dps = 0, heal = 0, tank = 0, active = 0, update = 0, randomize = 0, teleport = 0, changeStrategy = 0, dead = 0, combat = 0, revive = 0, taxi = 0, moving = 0, mounted = 0, afk = 0;
    int stateCount[(uint8)TravelState::MAX_TRAVEL_STATE + 1] = { 0 };
    std::vector<std::pair<Quest const*, int32>> questCount;

    ForEachPlayerbot([this, &dps, &heal, &tank, &active, &update, &randomize, &teleport, &changeStrategy, &dead, &combat, &revive, &taxi, &moving, &mounted, &afk, &alliance, &horde, &perRace, &perClass, &stateCount, &questCount](Player* bot)
    {
        if (IsAlliance(bot->getRace()))
            alliance[bot->GetLevel() / 10]++;
        else
            horde[bot->GetLevel() / 10]++;

        perRace[bot->getRace()]++;
        perClass[bot->getClass()]++;

        if (GetBotAI(bot)->AllowActivity())
            active++;

        if (GetBotAI(bot)->GetAiObjectContext()->GetValue<bool>("random bot update")->Get())
            update++;

        uint32 botId = bot->GetGUIDLow();
        if (!GetEventValue(botId, "randomize"))
            randomize++;

        if (!GetEventValue(botId, "teleport"))
            teleport++;

        if (!GetEventValue(botId, "change_strategy"))
            changeStrategy++;

        if (bot->IsTaxiFlying())
            taxi++;

        if (bot->IsMoving() && !bot->IsTaxiFlying() && !bot->IsFlying())
            moving++;

        if (bot->IsMounted() && !bot->IsTaxiFlying())
            mounted++;

        if (bot->IsInCombat())
            combat++;

        if (bot->isAFK())
            afk++;

        if (sServerFacade.UnitIsDead(bot))
        {
            dead++;
            //if (!GetEventValue(botId, "dead"))
            //    revive++;
        }

        int spec = AiFactory::GetPlayerSpecTab(bot);
        switch (bot->getClass())
        {
        case CLASS_DRUID:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_PALADIN:
            if (spec == 1)
                tank++;
            else if (spec == 0)
                heal++;
            else
                dps++;
            break;
        case CLASS_PRIEST:
            if (spec != 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_SHAMAN:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_WARRIOR:
            if (spec == 2)
                tank++;
            else
                dps++;
            break;
#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
            if (spec == 0)
                tank++;
            else
                dps++;
            break;
#endif
        default:
            dps++;
            break;
        }

        TravelTarget* target = GetBotAI(bot)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
        if (target)
        {
            TravelState state = target->GetTravelState();
            stateCount[(uint8)state]++;            
        }
    });

    ss.str(""); ss << "Bots level:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

	uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
	for (uint32 i = 0; i < 10; ++i)
    {
        if (!alliance[i] && !horde[i])
            continue;

        uint32 from = i*10;
        uint32 to = std::min(from + 9, maxLevel);
        if (!from) from = 1;

        ss.str(""); ss << "    " << from << ".." << to << ": " << alliance[i] << " alliance, " << horde[i] << " horde";
        sLog.outString("%s", ss.str().c_str());
        if (requester) { requester->SendMessageToPlayer(ss.str()); }
    }

    ss.str(""); ss << "Bots race:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        if (perRace[race])
        {
            ss.str(""); ss << "    " << ChatHelper::formatRace(race) << ": " << perRace[race];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots class:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
        {
            ss.str(""); ss << "    " << ChatHelper::formatClass(cls) << ": " << perClass[cls];
            sLog.outString("%s", ss.str().c_str());
            if (requester) { requester->SendMessageToPlayer(ss.str()); }
        }
    }

    ss.str(""); ss << "Bots role:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    tank: " << tank << ", heal: " << heal << ", dps: " << dps;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots status:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Active: " << active;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Moving: " << moving;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    //sLog.outString("Bots to:");
    //sLog.outString("    update: %d", update);
    //sLog.outString("    randomize: %d", randomize);
    //sLog.outString("    teleport: %d", teleport);
    //sLog.outString("    change_strategy: %d", changeStrategy);
    //sLog.outString("    revive: %d", revive);

    ss.str(""); ss << "    On taxi: " << taxi;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    On mount: " << mounted;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    In combat: " << combat;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Dead: " << dead;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    AFK: " << afk;
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "Bots questing:";
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Picking quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_PICK_UP_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_PICK_UP_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Doing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_DO_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_DO_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Completing quests: " << stateCount[(uint8)TravelState::TRAVEL_STATE_TRAVEL_HAND_IN_QUEST] + stateCount[(uint8)TravelState::TRAVEL_STATE_WORK_HAND_IN_QUEST];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    ss.str(""); ss << "    Idling: " << stateCount[(uint8)TravelState::TRAVEL_STATE_IDLE];
    sLog.outString("%s", ss.str().c_str());
    if (requester) { requester->SendMessageToPlayer(ss.str()); }

    // This walk now happens inside a world tick and its cost scales with the bot
    // count, so log what it actually cost instead of assuming it is negligible.
    uint32 printStatsElapsedMs = (uint32)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - printStatsStarted).count();
    sLog.outString("PrintStats took %u ms on the world thread for %u bots", printStatsElapsedMs, GetPlayerbotsAmount());
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    uint32 value = GetEventValue(id, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUIDLow();
    uint32 value = GetEventValue(id, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!master) return;
    uint32 discount = GetTradeDiscount(bot, master);
    int32 result = (int32)discount + value;
    discount = (result < 0 ? 0 : result);

    SetTradeDiscount(bot, master, discount);
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!master) return;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId =  master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    SetEventValue(botId, name.str(), value, sPlayerbotAIConfig.maxRandomBotInWorldTime);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!master) return 0;
    uint32 botId =  bot->GetGUIDLow();
    uint32 masterId = master->GetGUIDLow();
    std::ostringstream name; name << "trade_discount_" << masterId;
    return GetEventValue(botId, name.str());
}

std::future<std::string> RandomPlayerbotMgr::PostRemoteCommand(std::string request)
{
    return m_remoteCommandQueue.Post(std::move(request));
}

void RandomPlayerbotMgr::RunQueuedRemoteCommands()
{
    // The handler is passed in here rather than stored in the queue, which is
    // what keeps WorldThreadCommandQueue free of every game and playerbot
    // header. This is the one place the two sides meet, and it is on the
    // world thread.
    m_remoteCommandQueue.Drain(WorldThreadCommandQueue::MaxCommandsPerTick,
        [this](std::string const& request) { return HandleRemoteCommandOnWorldThread(request); });
}

// WORLD THREAD ONLY - the name says so because the body below resolves a guid
// to a Player* and a PlayerbotAI* and dereferences them. Reached from a
// network thread this was a use-after-free whenever the world despawned the
// bot on the same tick; it is correct exactly because RunQueuedRemoteCommands()
// is the only thing that calls it now. The body itself is unchanged.
std::string RandomPlayerbotMgr::HandleRemoteCommandOnWorldThread(std::string request)
{
    std::string::iterator pos = find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        std::ostringstream out; out << "invalid request: " << request;
        return out.str();
    }

    std::string command = std::string(request.begin(), pos);
    uint32 guid = std::atoi(std::string(pos + 1, request.end()).c_str());
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI *ai = GetBotAI(bot);
    if (!ai)
        return "invalid guid";

    return ai->HandleRemoteCommand(command);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    uint32 bot = player->GetGUIDLow();

    if (urand(0, 100) > 100 * sPlayerbotAIConfig.randomBotRpgChance) // select grind / pvp
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to grind spot", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        // teleport in different places only if players are online
        RandomTeleportForLevel(player, players.size());
        ScheduleTeleport(bot);
    }
    else
    {
        sLog.outDetail("Bot #%d %s:%d <%s>: sent to inn", bot, player->GetTeam() == ALLIANCE ? "A" : "H", player->GetLevel(), player->GetName());
        RandomTeleportForRpg(player, players.size());
        ScheduleTeleport(bot);
    }
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot, bool activeOnly)
{
    uint32 race = bot->getRace();
    uint32 level = bot->GetLevel();

    // Some races (e.g. custom playable races) have no dedicated ai_playerbot_rpg_races
    // rows, or their low-level rows reference creature templates that were never
    // actually placed anywhere - leaving rpgLocsCacheLevel[race][level] empty and the
    // bot permanently stuck with no RPG/hearth destination. Fall back to the
    // race-neutral RPG cache, then to the general grind-spot cache, rather than failing.
    std::vector<WorldLocation>* locs = &rpgLocsCacheLevel[race][level];
    if (locs->empty())
        locs = &rpgLocsCacheLevel[0][level];
    if (locs->empty())
        locs = &locsPerLevelCache[level];

    sLog.outDetail("Random teleporting bot %s for RPG (%zu locations available)", bot->GetName(), locs->size());
    RandomTeleport(bot, *locs, true, activeOnly);
    Refresh(bot);

    //Travel cooldown for 10 minutes.
    if (GetBotAI(bot))
    {
        AiObjectContext* context = GetBotAI(bot)->GetAiObjectContext();
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

        sTravelMgr.SetNullTravelTarget(travelTarget);
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
        travelTarget->SetExpireIn(10 * MINUTE * IN_MILLISECONDS);
    }
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    SC_LOG("RandomPlayerbotMgr::Remove entry bot=%s",
           bot ? bot->GetName() : "(null)");
    if (!bot)
        return;
    uint32 owner = bot->GetGUIDLow();
    if (!PersistentRosterDestructiveMutationAllowed(owner))
    {
        sLog.outError("[PersistentRoster] ordinary remove rejected for desired guid=%u", owner);
        return;
    }
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — deleting random_bots row", owner);
    CharacterDatabase.PExecute(PlayerbotDatabaseContract::EventStoreSql(
        "DELETE FROM ", " WHERE owner = 0 AND bot = '%d'").c_str(), owner);
    {
        ai::context_lock::Unique const lock(eventCacheMutex, ai::context_lock::On());   // audit A28
        eventCache[owner].clear();
    }
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — calling LogoutPlayerBot", owner);

    LogoutPlayerBot(owner);
    SC_LOG("RandomPlayerbotMgr::Remove guid=%u — DONE", owner);
}

const CreatureDataPair* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (entry != 0 && sObjectMgr.GetCreatureTemplate(entry))
    {
        FindCreatureData worker(entry, NULL);
        sObjectMgr.DoCreatureData(worker);
        CreatureDataPair const* dataPair = worker.GetResult();
        return dataPair;
    }
    return NULL;
}

uint32 RandomPlayerbotMgr::GetCreatureGuidByEntry(uint32 entry)
{
    uint32 guid = 0;

    CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(entry);
    guid = dataPair->first;

    return guid;
}

uint32 RandomPlayerbotMgr::GetBattleMasterEntry(Player* bot, BattleGroundTypeId bgTypeId, bool fake)
{
    Team team = bot->GetTeam();
    uint32 entry = 0;
    std::vector<uint32> Bms;

    for (auto i = begin(BattleMastersCache[team][bgTypeId]); i != end(BattleMastersCache[team][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    for (auto i = begin(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); i != end(BattleMastersCache[TEAM_BOTH_ALLOWED][bgTypeId]); ++i)
    {
        Bms.insert(Bms.end(), *i);
    }

    if (Bms.empty())
        return entry;

    float dist1 = FLT_MAX;

    for (auto i = begin(Bms); i != end(Bms); ++i)
    {
        CreatureDataPair const* dataPair = sRandomPlayerbotMgr.GetCreatureDataByEntry(*i);
        if (!dataPair)
            continue;

        CreatureData const* data = &dataPair->second;

        Unit* Bm = sMapMgr.FindMap((uint32)data->position.mapid)->GetUnit(ObjectGuid(HIGHGUID_UNIT, *i, dataPair->first));
        if (!Bm)
            continue;

        if (bot->GetMapId() != Bm->GetMapId())
            continue;

        // return first available guid on map if queue from anywhere
        if (fake)
        {
            entry = *i;
            break;
        }

        AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(Bm));
        if (!area)
            continue;

        if (area->team == 4 && bot->GetTeam() == ALLIANCE)
            continue;
        if (area->team == 2 && bot->GetTeam() == HORDE)
            continue;

        if (Bm->GetDeathState() == DEAD)
            continue;

        float dist2 = sServerFacade.GetDistance2d(bot, data->position.coord_x, data->position.coord_y);
        if (dist2 < dist1)
        {
            dist1 = dist2;
            entry = *i;
        }
    }

    return entry;
}

void RandomPlayerbotMgr::Hotfix(Player* bot, uint32 version)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    uint32 exp = bot->GetUInt32Value(PLAYER_XP);
    uint32 level = bot->GetLevel();
    uint32 id = bot->GetGUIDLow();

    for (int fix = version; fix <= MANGOSBOT_VERSION; fix++)
    {
        int count = 0;
        switch (fix)
        {
            case 1: // Apply class quests to previously made random bots

                if (level < 10)
                {
                    break;
                }

                for (std::list<uint32>::iterator i = factory.classQuestIds.begin(); i != factory.classQuestIds.end(); ++i)
                {
                    uint32 questId = *i;
                    Quest const *quest = sObjectMgr.GetQuestTemplate(questId);

                    if (!bot->SatisfyQuestClass(quest, false) ||
                        quest->GetMinLevel() > bot->GetLevel() ||
                        !bot->SatisfyQuestRace(quest, false) || bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                        continue;

                    bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
                    bot->RewardQuest(quest, 0, bot, false);
                    bot->SetLevel(level);
                    bot->SetUInt32Value(PLAYER_XP, exp);
                    sLog.outDetail("Bot %d rewarded quest %d",
                        bot->GetGUIDLow(), questId);
                    count++;
                }

                if (count > 0)
                {
                    sLog.outDetail("Bot %d hotfix (Class Quests), %d quests rewarded",
                        bot->GetGUIDLow(), count);
                    count = 0;
                }
                break;
            case 2: // Init Riding skill fix

                if (level < 20)
                {
                    break;
                }
                factory.InitSkills();
                sLog.outDetail("Bot %d hotfix (Riding Skill) applied",
                    bot->GetGUIDLow());
                break;

            default:
                break;
        }
    }
    SetValue(bot, "version", MANGOSBOT_VERSION);
    sLog.outDetail("Bot %d hotfix v%d applied",
        bot->GetGUIDLow(), MANGOSBOT_VERSION);
}

void RandomPlayerbotMgr::MirrorAh()
{
    sRandomPlayerbotMgr.m_ahActionMutex.lock();

    ahMirror.clear();

    // Iterate all DBC auction house entries, deduplicating by object pointer
    // (cross-faction mode collapses all entries to one object).
    std::vector<AuctionHouseObject*> visited;
    for (uint32 i = 0; i < sAuctionHouseStore.GetNumRows(); ++i)
    {
        AuctionHouseEntry const* houseEntry = sAuctionHouseStore.LookupEntry(i);
        if (!houseEntry)
            continue;

        AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(houseEntry);
        if (!auctionHouse)
            continue;
        if (std::find(visited.begin(), visited.end(), auctionHouse) != visited.end())
            continue;
        visited.push_back(auctionHouse);

        // Pure copy loop, no DB and no mail, so simply hold the auction lock
        // across it rather than snapshotting - otherwise the ahbot thread can
        // delete entries while we are copying them.
        AuctionHouseObject::Guard ahGuard(auctionHouse->GetLock());
        AuctionHouseObject::AuctionEntryMapBounds bounds = auctionHouse->GetAuctionsBounds_locked();

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (!itr->second)
                continue;

            AuctionEntry auctionEntry = *itr->second;

            if (!auctionEntry.buyout)
                continue;

            if (!auctionEntry.itemCount)
                continue;

            ahMirror[auctionEntry.itemTemplate].push_back(auctionEntry);
        }
    }
    sRandomPlayerbotMgr.m_ahActionMutex.unlock();
}

typedef std::unordered_map <uint32, std::list<float>> botPerformanceMetric;
std::unordered_map<std::string, botPerformanceMetric> botPerformanceMetrics;

void RandomPlayerbotMgr::PushMetric(botPerformanceMetric& metric, const uint32 bot, const float value, uint32 maxNum) const
{
    metric[bot].push_back(value);

    if (metric[bot].size() > maxNum)
        metric[bot].pop_front();
}

float RandomPlayerbotMgr::GetMetricDelta(botPerformanceMetric& metric) const
{
    float deltaMetric = 0;
    for (auto& botMetric : metric)
    {
        std::list<float> values = botMetric.second;
        if (values.size() > 1)
            deltaMetric += (values.back() - values.front()) / values.size();
    }

    if (metric.empty())
        return 0;

    return deltaMetric / metric.size();
}

std::string RandomPlayerbotMgr::GetCommandTexts(const std::string& command)
{
    auto texts = GetCommandTexts();
    auto it = texts.find(command);
    if (it != texts.end())
        return it->second;
    return "";
}

std::unordered_map<std::string, std::string> RandomPlayerbotMgr::GetCommandTexts()
{
    return std::unordered_map<std::string, std::string>
    {
        {"init", "Randomize the first available bot.\nUsage: init"},
        {"upgrade", "Update gear and spells for all random bots.\nUsage: upgrade"},
        {"refresh", "Log out and log in all random bots to refresh their status.\nUsage: refresh"},
        {"teleport", "Teleport all random bots to a location suitable for their level.\nUsage: teleport"},
        {"rpg", "Teleport all random bots to a location for RPG activities.\nUsage: rpg"},
        {"revive", "Revive all dead random bots.\nUsage: revive"},
        {"grind", "Teleport all random bots to a grinding location.\nUsage: grind"},
        {"change_strategy", "Change the AI strategy for random bots.\nUsage: change_strategy <botname> <strategy>"},
        {"remove", "Remove a random bot from the server.\nUsage: remove <botname>"},
        {"reset", "Reset all random bots and clear event cache.\nUsage: reset"},
        {"diff", "Show server performance metrics.\nUsage: diff [player_diff] [empty_diff]"},
        {"stats", "Print bot statistics.\nUsage: stats"},
        {"update", "Trigger immediate bot AI update.\nUsage: update"},
        {"pid", "Adjust PID controller values.\nUsage: pid p i d"},
        {"clean map", "Unload and reload map files.\nUsage: clean map"},
        {"login debug", "Toggle login debug mode.\nUsage: login debug"},
        {"cmd", "Send command to a bot.\nUsage: cmd <botname> <command>"},
        {"help", "Show help for commands.\nUsage: help [command]"}
    };
}

std::list<std::string> RandomPlayerbotMgr::HandleHelp(std::string param)
{
    std::list<std::string> messages;
        
    if (param.empty())
    {
        messages.push_back("Type 'help commands for all available commands.");
        messages.push_back("Type 'help <command>' for more information on a specific command.");
        return messages;
    }

    if (param == "commands")
    {
        std::string commands = "Commands: ";
        for (auto& [command, help] : GetCommandTexts())
        {
            commands += command + ", ";
        }

        commands = commands.substr(0, commands.size() - 2);
        messages.push_back(commands);
        return messages;
    }
    
    
    std::string helpText = GetCommandTexts(param);
    if (!helpText.empty())
    {
        messages.push_back(helpText);
    }  
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomizeFirst(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomizeFirst(bot);
    messages.push_back("init applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleUpdateGearSpells(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    UpdateGearSpells(bot);
    messages.push_back("upgrade applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRefresh(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Refresh(bot);
    messages.push_back("refresh applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForLevel(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleportForLevel(bot);
    messages.push_back("teleport applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleportForRpg(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleportForRpg(bot);
    messages.push_back("rpg applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRevive(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Revive(bot);
    messages.push_back("revive applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRandomTeleport(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    RandomTeleport(bot);
    messages.push_back("grind applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleChangeStrategy(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    ChangeStrategy(bot);
    messages.push_back("change_strategy applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleRemove(Player* bot)
{
    std::list<std::string> messages;
    if (!bot)
    {
        messages.push_back("Bot not found");
        return messages;
    }
    Remove(bot);
    messages.push_back("remove applied to " + std::string(bot->GetName()));
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReset(std::string param)
{
    std::list<std::string> messages;
    if (sPlayerbotAIConfig.persistentActiveRosterEnabled)
    {
        messages.push_back("Persistent roster enabled: reset rejected; use an explicit canonical roster admin operation.");
        return messages;
    }
    CharacterDatabase.PExecute(PlayerbotDatabaseContract::EventStoreSql(
        "delete from ", " where event not in ('temporary')").c_str());
    {
        ai::context_lock::Unique const lock(sRandomPlayerbotMgr.eventCacheMutex, ai::context_lock::On());   // audit A28
        sRandomPlayerbotMgr.eventCache.clear();
    }
    std::string msg = "Random bots were reset for all players. Please restart the Server.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleStats(std::string param)
{
    if (!Qualified::isValidNumberString(param))
    {
        return {"Stats: Error parsing " + param};
    }

    std::list<std::string> messages;
    std::string msg = "Stats requested.";
    messages.push_back(msg);

    // PrintStats walks every live bot and touches AI contexts, so it runs here on
    // the world thread rather than on a worker. This handler is only ever reached
    // from ChatHandler::ParseCommands, i.e. either an in-game GM command or
    // World::ProcessCliCommands, both of which run inside World::Update.
    ObjectGuid guid = ObjectGuid(uint64(std::stoull(param)));
    // GetPlayer() indexes the `players` map, which is keyed by GetGUIDLow(), so the
    // low GUID is what it wants - not the raw value ObjectGuid::operator uint64()
    // would silently narrow into the uint32 parameter.
    PrintStats(guid.GetCounter());
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleReload(std::string param)
{
    std::list<std::string> messages;
    sPlayerbotAIConfig.Initialize();
    std::string msg = "Playerbot config reloaded.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleUpdate(std::string param)
{
    std::list<std::string> messages;
    sRandomPlayerbotMgr.UpdateAIInternal(0);
    std::string msg = "Playerbot update triggered.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsolePid(std::string param)
{
    std::list<std::string> messages;
    std::string pids = param.substr(4);
    std::vector<std::string> pid = Qualified::getMultiQualifiers(pids, " ");

    if (pid.size() == 0)
        pid.push_back("0");
    if (pid.size() == 1)
        pid.push_back("0");
    if (pid.size() == 2)
        pid.push_back("0");
    sRandomPlayerbotMgr.pid.adjust(stof(pid[0]), stof(pid[1]), stof(pid[2]));

    std::string msg = "Pid set to p:" + std::to_string(stof(pid[0])) + " i:" + std::to_string(stof(pid[1])) + " d:" + std::to_string(stof(pid[2]));
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleDiff(std::string param)
{
    std::list<std::string> messages;
    if (param.empty())
    {
        std::stringstream ss;
        ss << "Avg diff: " << sWorld.GetAverageDiff() << "\n";
        ss << "Max diff: " << sWorld.GetMaxDiff() << "\n";
        ss << "char db ping: " << sRandomPlayerbotMgr.GetDatabaseDelay("CharacterDatabase") << "\n";
        ss << "Sessions online: " << sWorld.GetActiveSessionCount() << "\n";
        ss << "Bots online: " << sRandomPlayerbotMgr.botCount << " (active: " << sRandomPlayerbotMgr.activeBots << ")";

        messages.push_back(ss.str());
        return messages;
    }
    else if (param.find(" ") != std::string::npos)
    {
        std::vector<std::string> diff = Qualified::getMultiQualifiers(param, " ");
        if (diff.size() == 0)
            diff.push_back("100");
        if (diff.size() == 1)
            diff.push_back(diff[0]);
        sPlayerbotAIConfig.diffWithPlayer = stoi(diff[0]);
        sPlayerbotAIConfig.diffEmpty = stoi(diff[1]);

        std::string msg = "Diff set to " + std::to_string(stoi(diff[0])) + " (player), " + std::to_string(stoi(diff[1])) + " (empty)";
        messages.push_back(msg);
        return messages;
    }
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleCleanMap(std::string param)
{
    std::list<std::string> messages;
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->MapID;
        boost::thread t([mapId]()
        {
            try
            {
                WorldPosition::unloadMapAndVMaps(mapId);
            }
            catch (...)
            {
                // An escaping exception on a detached thread is std::terminate
                // for the whole worldserver. Swallow it silently: that one map
                // simply stays loaded. We do not log here because the logging
                // subsystem is not guaranteed thread-safe off the world thread.
            }
        });
        t.detach();
    }

    std::string msg = "Map cleaning initiated.";
    messages.push_back(msg);
    return messages;
}

std::list<std::string> RandomPlayerbotMgr::HandleConsoleLoginDebug(std::string param)
{
    std::list<std::string> messages;
    sPlayerBotLoginMgr.ToggleDebug();
    std::string msg = "Login debug toggled.";
    messages.push_back(msg);
    return messages;
}

uint32 RandomPlayerbotMgr::GetOrCreateAccount(Player* master, std::string& error)
{
    uint32 maxCharsPerAccount = 9;
#ifdef MANGOSBOT_TWO
    maxCharsPerAccount = 10;
#endif

    auto accountNrQr = LoginDatabase.PQuery("SELECT max(replace(lower(username), lower('%s'), '') + 1 - 1) maxAccountNr FROM account WHERE replace(lower(username), lower('%s'), '') != 0", sPlayerbotAIConfig.randomBotAccountPrefix.c_str(), sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    if (!accountNrQr)
    {
        error = "Failed to find last " + sPlayerbotAIConfig.randomBotAccountPrefix + " account nr.";
        return 0;
    }

    Field* fields = accountNrQr->Fetch();
    // Solo-project fix: start at 0 (not randomBotAccountCount) so the lazy
    // creator reuses the same RNDBOT0..N pool that the startup loader
    // populates into randomBotAccounts. Upstream cmangos starts at
    // randomBotAccountCount, which puts manually-created bots at 200+
    // (outside the trusted-account list) and breaks `.rndbot add` for
    // non-admin users. The loop already skips occupied account numbers
    // (via `accountNumber++` at the bottom on full accounts), so starting
    // at 0 just walks forward to the first slot with room.
    uint32 accountNumber = 0;
    uint32 maxAccountNum = fields[0].GetUInt32();

    for (uint32 i = 0; i < 10000; i++)
    {
        std::ostringstream accountNameStr;
        accountNameStr << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        std::string accountName = accountNameStr.str();

        uint32 accountId = sAccountMgr.GetId(accountName);

        if (!accountId)
        {
            std::string password;
            if (sPlayerbotAIConfig.randomBotRandomPassword)
            {
                for (int i = 0; i < 10; i++)
                    password += (char)urand('!', 'z');
            }
            else
                password = accountName;

            LoginDatabase.BeginTransaction();
#ifndef MANGOSBOT_ZERO
            uint8 max_expansion = MAX_EXPANSION;
            AccountOpResult result = sAccountMgr.CreateAccount(accountName, password, max_expansion);
#else
            AccountOpResult result = sAccountMgr.CreateAccount(accountName, password);
#endif
            LoginDatabase.CommitTransactionDirect();

            if (result == AOR_OK)
            {
                uint32 accountId = sAccountMgr.GetId(accountName);
                if (accountId)
                {
                    sPlayerbotAIConfig.AddRandomBotAccount(accountId);
                    return accountId;
                }
            }

            error = "Failed to create account";
            return 0;        
        }

        uint32 charCount = sAccountMgr.GetCharactersCount(accountId);

        if (charCount < maxCharsPerAccount)
        {
            if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
            {
                sPlayerbotAIConfig.AddRandomBotAccount(accountId);
            }
            return accountId;
        }

        accountNumber++;
    }

    error = "Failed to find a suitable account.";
    return 0;
}

void RandomPlayerbotMgr::OnBotDeleted(uint32 botGuid, uint32 accountId)
{
    if (accountId > 0 && sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        uint32 maxCharsPerAccount = 9;
    #ifdef MANGOSBOT_TWO
        maxCharsPerAccount = 10;
    #endif
    
        if (sAccountMgr.GetCharactersCount(accountId) == 0)
        {
            std::ostringstream prefix;
            prefix << sPlayerbotAIConfig.randomBotAccountPrefix;
            size_t prefixLen = prefix.str().length();
            
            auto result = LoginDatabase.PQuery("SELECT username FROM account WHERE id = '%u'", accountId);
            if (result)
            {
                std::string username = result->Fetch()[0].GetString();
                if (username.substr(0, prefixLen) == prefix.str())
                {
                    uint32 accountNum = std::stoul(username.substr(prefixLen));
                    if (accountNum >= sPlayerbotAIConfig.randomBotAccountCount)
                    {
                        sAccountMgr.DeleteAccount(accountId);
                        sLog.outString("Deleted empty random bot account: %s (id: %u)", username.c_str(), accountId);
                    }
                }
            }
        }
    }
}

// --- twow-repo#541/#551 (owner 07.10.2026): rndbot park / unpark ------------------------------------
// A parked bot stands at an inn of its own faction for its level band nearest to where it is, its
// hearthstone bound there and checked in the database, with no travel, quest, grind or rpg and an AI
// update every ai::park::AiIntervalMs (PlayerbotAI::UpdateAI). World thread only.

namespace
{
    ai::park::Point ParkPoint(WorldLocation const& loc)
    {
        return ai::park::Point{ loc.mapid, loc.coord_x, loc.coord_y, loc.coord_z };
    }

    ai::park::Point ParkPoint(Player const* bot)
    {
        return ai::park::Point{ bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() };
    }

    // Strategies a parked bot drops (non-combat); an attacked bot still defends itself.
    char const* const ParkDroppedStrategies =
        "-travel,-rpg,-grind,-quest,-wander,-tfish,-rpg craft,-bot brain,-maintenance,-group,-guild,-bg,-lfg,-explore";

    // twow-repo#551 (AiPlayerbot.Park.Quiet): a quiet parked bot also drops its social strategies.
    std::string ParkStrategyChange()
    {
        return std::string(ParkDroppedStrategies) + (sPlayerbotAIConfig.parkQuiet ? ai::park::QuietDroppedStrategies() : "");
    }

    // Capacity key of a spot: map and position rounded to whole yards.
    uint64 ParkSpotKey(WorldLocation const& loc)
    {
        return (uint64(loc.mapid) << 48) ^ (uint64(uint32(int32(loc.coord_x)) & 0xFFFFFF) << 24) ^ uint64(uint32(int32(loc.coord_y)) & 0xFFFFFF);
    }

    // twow-repo#551 (Park.MaxPerCity): the capital zone a spot lies in, 0 when it is not in a capital.
    uint32 CapitalZone(WorldLocation const& loc)
    {
        AreaTableEntry const* area = WorldPosition(loc).GetArea();
        if (area && area->zone)
            area = GetAreaEntryByAreaID(area->zone);
        return area && (area->flags & AREA_FLAG_CAPITAL) ? area->ID : 0;
    }

    bool IsCapitalSpot(WorldLocation const& loc)
    {
        return CapitalZone(loc) != 0;
    }
}

// Levels an inn may lie above or below the bot's level and still be chosen (ParkBot).
static constexpr uint32 ParkLevelSlack = 5;

// twow-repo#551 (Park.Seats): every seat of the CHAIR objects around a spot, once per spot (world thread). The order is the
// spawn order, so slot n is always the same seat.
std::vector<ai::park::Seat> const& RandomPlayerbotMgr::SeatsFor(WorldLocation const& spot, uint64 spotKey)
{
    auto const cached = parkSeatCache.find(spotKey);
    if (cached != parkSeatCache.end())
        return cached->second;

    std::vector<ai::park::Seat>& seats = parkSeatCache[spotKey];
    ai::park::Point const center = ParkPoint(spot);
    std::map<uint32, GameObjectData const*> chairs;   // by guid: a stable order
    auto const collect = [&](GameObjectDataPair const& pair)
    {
        GameObjectData const& data = pair.second;
        if (data.position.mapId != spot.mapid)
            return false;
        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(data.id);
        if (!info || info->type != GAMEOBJECT_TYPE_CHAIR)
            return false;
        if (ai::park::SeatNearSpot(center, ai::park::Point{ data.position.mapId, data.position.x, data.position.y, data.position.z }))
            chairs[pair.first] = &data;
        return false;
    };
    sObjectMgr.DoGOData(collect);
    for (auto const& [guid, data] : chairs)
    {
        GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(data->id);
        for (ai::park::Seat const& seat : ai::park::ChairSeats(ai::park::Point{ data->position.mapId, data->position.x, data->position.y,
                data->position.z }, data->position.o, info->size, info->chair.slots, info->chair.height))
            seats.push_back(seat);
    }
    sLog.outBasic("[Park] state=seats map=%u x=%.0f y=%.0f chairs=%u seats=%u", spot.mapid, spot.coord_x, spot.coord_y,
        uint32(chairs.size()), uint32(seats.size()));
    return seats;
}

void RandomPlayerbotMgr::BuildParkSpots()
{
    if (parkSpotsBuilt)
        return;
    parkSpotsBuilt = true;

    // Inns: every innkeeper of the cache once per faction, with the level range it serves.
    std::map<uint64, size_t> innIndex[2];
    std::set<uint64> hostileSkipped[2];
    for (auto const& [race, byLevel] : innCacheLevel)
    {
        if (!race)
            continue;
        uint32 const team = Player::TeamForRace(uint8(race)) == ALLIANCE ? 0 : 1;
        // twow-repo#551 (owner 10.10.2026: "geparkte bots nur gasthäuser ihrer fraktion"): the inn cache assigns
        // innkeepers to races by the races of the area around them (race 0 = every race), never by the innkeeper's
        // own faction - in mixed zones a Horde bot could get an Alliance inn. An innkeeper hostile to the race is
        // left out; neutral ones (goblin towns) stay for both factions.
        FactionTemplateEntry const* raceFaction = sFactionTemplateStore.LookupEntry(Player::GetFactionForRace(uint8(race)));
        for (auto const& [level, inns] : byLevel)
            for (auto const& [innGuid, loc] : inns)
            {
                CreatureInfo const* innkeeper = sObjectMgr.GetCreatureTemplate(innGuid.GetEntry());
                FactionTemplateEntry const* innFaction = innkeeper ? sFactionTemplateStore.LookupEntry(innkeeper->Faction) : nullptr;
                if (raceFaction && innFaction && innFaction->IsHostileTo(*raceFaction))
                {
                    hostileSkipped[team].insert(ParkSpotKey(loc));
                    continue;
                }
                uint64 const key = ParkSpotKey(loc);
                auto const it = innIndex[team].find(key);
                if (it == innIndex[team].end())
                {
                    innIndex[team][key] = parkInns[team].size();
                    AreaTableEntry const* innArea = WorldPosition(loc).GetArea();
                    parkInns[team].push_back(ParkInn{ loc, level, level, CapitalZone(loc), innArea ? innArea->ID : 0u });
                }
                else
                {
                    ParkInn& inn = parkInns[team][it->second];
                    inn.minLevel = std::min(inn.minLevel, uint32(level));
                    inn.maxLevel = std::max(inn.maxLevel, uint32(level));
                }
            }
    }

    // Capital spots (bank, auction house, trainers of the faction's capitals), any level, once each.
    std::set<uint64> citySeen[2];
    for (auto const& [race, byLevel] : rpgLocsCacheLevel)
    {
        if (!race)
            continue;
        uint32 const team = Player::TeamForRace(uint8(race)) == ALLIANCE ? 0 : 1;
        for (auto const& [level, locs] : byLevel)
            for (WorldLocation const& loc : locs)
                if (citySeen[team].insert(ParkSpotKey(loc)).second && IsCapitalSpot(loc))
                {
                    parkCities[team].push_back(loc);
                    parkCityZones[team].push_back(CapitalZone(loc));   // twow-repo#551 (Park.MaxPerCity)
                }
    }

    sLog.outBasic("[Park] state=spots alliance_inns=%u horde_inns=%u alliance_city=%u horde_city=%u alliance_hostile_inns_skipped=%u horde_hostile_inns_skipped=%u",
        uint32(parkInns[0].size()), uint32(parkInns[1].size()), uint32(parkCities[0].size()), uint32(parkCities[1].size()),
        uint32(hostileSkipped[0].size()), uint32(hostileSkipped[1].size()));

    // twow-repo#551 (owner 10.10.): per capital zone and faction, the inns inside it and its city spots, once.
    for (uint32 team = 0; team < 2; ++team)
    {
        std::map<uint32, std::pair<uint32, uint32>> perCity;   // zone -> (inns, city spots)
        for (ParkInn const& inn : parkInns[team])
            if (inn.cityZone)
                ++perCity[inn.cityZone].first;
        for (uint32 const zone : parkCityZones[team])
            ++perCity[zone].second;
        for (auto const& [zone, counts] : perCity)
            sLog.outBasic("[Park] state=spots_city team=%s zone=%u inns=%u city_spots=%u",
                team == 0 ? "alliance" : "horde", zone, counts.first, counts.second);
    }
}

void RandomPlayerbotMgr::ReleaseParkSpot(ParkEntry const& entry)
{
    parkCityCounts.Remove(entry.cityZone);   // twow-repo#551 (Park.MaxPerCity); 0 = no city, no-op
    auto const it = parkSpotSlots.find(entry.spotKey);
    if (it == parkSpotSlots.end())
        return;
    it->second.erase(entry.slot);
    if (it->second.empty())
        parkSpotSlots.erase(it);
}

bool RandomPlayerbotMgr::ParkBot(Player* bot, std::string const& teleName, std::string& reason)
{
    PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
    if (!ai)
    {
        reason = "no_ai";
        return false;
    }
    if (IsParkedBot(bot->GetGUIDLow()))
    {
        reason = "already_parked";
        return false;
    }
    if (ai->HasRealPlayerMaster() || bot->GetGroup() || !bot->IsInWorld() || bot->IsBeingTeleported() ||
        bot->InBattleGround() || bot->GetMap()->Instanceable() || bot->IsTaxiFlying() || !bot->IsAlive())
    {
        reason = "busy";
        return false;
    }

    // The spot: a game_tele name if given; else the nearest inn of the bot's race and level band with
    // room (innCacheLevel, innkeepers per area level and faction); a full inn overflows to the next
    // inn, then to a capital spot of the race (rpg cache, bank/auction/trainer NPCs), each spot with
    // at most ai::park::SpotCapacity parked bots (owner: "ab 25 ist ein gasthaus voll").
    WorldLocation spot;
    std::string via;
    uint32 cityZone = 0;   // twow-repo#551 (Park.MaxPerCity): capital zone of the chosen spot
    // twow-repo#551 (owner 10.10., Park.MaxPerInn / Park.MaxPerCity, both 0 = as before): inns hold MaxPerInn,
    // city spots SpotCapacity; every spot of a capital zone at MaxPerCity counts as full. overflowed = the
    // nearest spot (ignoring room) was full, so the bot went to another one.
    bool overflowed = false;
    auto const pick = [&](std::vector<WorldLocation> const& candidates, std::vector<uint32> const& zones, bool inns) -> int
    {
        uint32 const capacity = ai::park::SpotCapacityFor(inns, sPlayerbotAIConfig.parkMaxPerInn);
        std::vector<ai::park::Point> points;
        std::vector<uint32> occupied;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            WorldLocation const& loc = candidates[i];
            points.push_back(ParkPoint(loc));
            auto const it = parkSpotSlots.find(ParkSpotKey(loc));
            uint32 const taken = it == parkSpotSlots.end() ? 0 : uint32(it->second.size());
            uint32 const zone = i < zones.size() ? zones[i] : 0;
            occupied.push_back(ai::park::EffectiveOccupied(taken, capacity, zone, parkCityCounts.Get(zone),
                sPlayerbotAIConfig.parkMaxPerCity));
        }
        int const chosen = ai::park::ChooseSpot(ParkPoint(bot), points, occupied, capacity);
        if (chosen >= 0)
            overflowed = chosen != ai::park::ChooseSpot(ParkPoint(bot), points, {}, capacity);
        return chosen;
    };

    // "here" (OB-00 test variant): park where the bot stands - no travel, no bind, no capacity.
    if (teleName == "here")
    {
        ParkEntry entry;
        entry.inn = WorldLocation(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation());
        entry.stage = ai::park::Stage::Parked;
        entry.since = uint32(time(nullptr));
        parkedBots[bot->GetGUIDLow()] = entry;
        PersistPark(bot->GetGUIDLow(), "here");   // twow-repo#551 (AiPlayerbot.Park.Persist)
        ai->SetParked(true);
        bot->SetHiddenFromBots(sPlayerbotAIConfig.parkHideFromBots);  // twow-repo#541/#551 (default off)
        ai->ChangeStrategy(ParkStrategyChange(), BotState::BOT_STATE_NON_COMBAT);
    if (sPlayerbotAIConfig.parkSeats)   // twow-repo#551 (Park.Seats): nobody rides into the inn, nor mounts while parked
    {
        ai->ChangeStrategy("-mount", BotState::BOT_STATE_NON_COMBAT);
        if (bot->IsMounted())
            bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    }
        AiObjectContext* context = ai->GetAiObjectContext();
        sTravelMgr.SetNullTravelTarget(AI_VALUE(TravelTarget*, "travel target"));
        ai->StopMoving();
        bot->GetMotionMaster()->Clear();
        sLog.outBasic("[Park] state=parked bot=%u level=%u via=here map=%u zone=%u", bot->GetGUIDLow(), bot->GetLevel(),
            bot->GetMapId(), bot->GetZoneId());
        return true;
    }

    if (!teleName.empty())
    {
        GameTele const* tele = sObjectMgr.GetGameTele(teleName);
        if (!tele)
        {
            reason = "unknown_tele";
            return false;
        }
        spot = WorldLocation(tele->mapId, tele->x, tele->y, tele->z, tele->o);
        // twow-repo#551 (Park.Seats): a tele target next to an inn of the faction (Goldshire: the street in front of the
        // Lion's Pride) parks in that inn, on its chairs.
        bool snappedInn = false;
        if (sPlayerbotAIConfig.parkSeats)
        {
            BuildParkSpots();
            std::vector<ai::park::Point> innPoints;
            std::vector<ParkInn> const& factionInns = parkInns[bot->GetTeam() == ALLIANCE ? 0 : 1];
            for (ParkInn const& inn : factionInns)
                innPoints.push_back(ParkPoint(inn.loc));
            int const snapped = ai::park::SnapToInn(ParkPoint(spot), innPoints);
            if (snapped >= 0)
            {
                spot = factionInns[snapped].loc;
                cityZone = factionInns[snapped].cityZone;
                snappedInn = true;
            }
        }
        if (pick({ spot }, {}, snappedInn) < 0)
        {
            reason = "spot_full";
            return false;
        }
        via = snappedInn ? "tele_inn" : "tele";
    }
    else
    {
        // OB-30 test 08.10. (park 800 of 1600, level 1-20): the exact race x level cache key found no inn
        // for 668 bots and the capital overflow, keyed the same way, nothing for 498. Inns of every race of
        // the bot's faction whose level range is within ParkLevelSlack of the bot, then the faction's
        // capital spots of any level, then the bot parks where it stands.
        BuildParkSpots();
        uint32 const team = bot->GetTeam() == ALLIANCE ? 0 : 1;
        uint32 const level = bot->GetLevel();
        std::vector<WorldLocation> inns;
        std::vector<uint32> innZones;
        // Owner 09.10.2026 ("damit die Städte nicht zu voll werden ... alle Gasthäuser mit 25 Bots füllen"):
        // with AiPlayerbot.Park.AnyLevelInn every inn of the faction counts, whatever level band it serves;
        // ChooseSpot then takes the nearest one with room, and only when all are full a capital spot.
        for (ParkInn const& inn : parkInns[team])
            if (sPlayerbotAIConfig.parkAnyLevelInn ||
                (level + ParkLevelSlack >= inn.minLevel && level <= inn.maxLevel + ParkLevelSlack))
            {
                inns.push_back(inn.loc);
                innZones.push_back(inn.cityZone);
            }
        int index = pick(inns, innZones, true);
        if (index >= 0)
        {
            spot = inns[index];
            cityZone = innZones[index];
            via = "inn";
            ++parkShare.inn;
        }
        else
        {
            index = pick(parkCities[team], parkCityZones[team], false);
            if (index < 0)
            {
                // twow-repo#551 (owner 10.10.): with a cap on the bot is not parked - never in the open world
                // because of a cap; it stays active and the next park command can try again.
                if (!ai::park::FallbackHere(sPlayerbotAIConfig.parkMaxPerInn, sPlayerbotAIConfig.parkMaxPerCity))
                {
                    sLog.outBasic("[Park] state=no_spot bot=%u level=%u inns=%u cities=%u", bot->GetGUIDLow(), level,
                        uint32(inns.size()), uint32(parkCities[team].size()));
                    ++parkShare.noSpot;
                    reason = "no_spot";
                    return false;
                }
                sLog.outBasic("[Park] state=fallback_here bot=%u level=%u reason=%s", bot->GetGUIDLow(), level,
                    inns.empty() && parkCities[team].empty() ? "no_inn" : "all_full");
                ++parkShare.here;
                return ParkBot(bot, "here", reason);
            }
            spot = parkCities[team][index];
            cityZone = parkCityZones[team][index];
            via = "city";
            ++parkShare.city;
            overflowed = overflowed || !inns.empty();   // every inn of the bot was full
        }
        if (overflowed)
            ++parkShare.overflow;
    }

    // The bot's own place at the spot: rings around it, >= 2 yd from the others (owner).
    uint64 const spotKey = ParkSpotKey(spot);
    std::set<uint32>& takenSlots = parkSpotSlots[spotKey];
    uint32 slot = 0;
    while (takenSlots.count(slot))
        ++slot;
    takenSlots.insert(slot);
    ai::park::Point place = ai::park::SlotOffset(ParkPoint(spot), slot);
    float orientation = spot.orientation;
    int32 seatHeight = -1;
    // twow-repo#551 (Park.Seats): a free chair seat of the spot first; without one the rings from 5.5 yd on, so the
    // innkeeper stays reachable.
    if (sPlayerbotAIConfig.parkSeats)
    {
        std::vector<ai::park::Seat> const& seats = SeatsFor(spot, spotKey);
        if (slot < seats.size())
        {
            place = seats[slot].at;
            orientation = seats[slot].orientation;
            seatHeight = int32(seats[slot].height);
        }
        else
            place = ai::park::FallbackPlace(ParkPoint(spot), slot, uint32(seats.size()));
    }
    WorldLocation const target(place.map, place.x, place.y, place.z, orientation);

    AreaTableEntry const* area = WorldPosition(spot).GetArea();

    ParkEntry entry;
    entry.inn = target;
    entry.area = area ? area->ID : 0;
    entry.since = uint32(time(nullptr));
    entry.spotKey = spotKey;
    entry.slot = slot;
    entry.seatHeight = seatHeight;
    entry.cityZone = cityZone;
    parkedBots[bot->GetGUIDLow()] = entry;
    parkCityCounts.Add(cityZone);   // twow-repo#551 (Park.MaxPerCity); released in ReleaseParkSpot
    PersistPark(bot->GetGUIDLow(), teleName);   // twow-repo#551 (AiPlayerbot.Park.Persist)

    ai->SetParked(true);
    bot->SetHiddenFromBots(sPlayerbotAIConfig.parkHideFromBots);  // twow-repo#541/#551 (default off)
    ai->ChangeStrategy(ParkStrategyChange(), BotState::BOT_STATE_NON_COMBAT);
    if (sPlayerbotAIConfig.parkSeats)   // twow-repo#551 (Park.Seats): nobody rides into the inn, nor mounts while parked
    {
        ai->ChangeStrategy("-mount", BotState::BOT_STATE_NON_COMBAT);
        if (bot->IsMounted())
            bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    }
    AiObjectContext* context = ai->GetAiObjectContext();
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    sTravelMgr.SetNullTravelTarget(travelTarget);
    ai->StopMoving();
    bot->GetMotionMaster()->Clear();
    bot->TeleportTo(target.mapid, target.coord_x, target.coord_y, target.coord_z, target.orientation);

    sLog.outBasic("[Park] state=travel bot=%u level=%u via=%s map=%u x=%.0f y=%.0f area=%u slot=%u hidden=%u",
        bot->GetGUIDLow(), bot->GetLevel(), via.c_str(), target.mapid, target.coord_x, target.coord_y, entry.area, slot,
        bot->IsHiddenFromBots() ? 1u : 0u);
    return true;
}

// twow-repo#551 (AiPlayerbot.Park.Persist): the park state in the bot event store (existing table, ParkPolicy.h).
void RandomPlayerbotMgr::PersistPark(uint32 guid, std::string const& teleName)
{
    if (sPlayerbotAIConfig.parkPersist)
        SetEventValue(guid, ai::park::PersistEvent, 1, ai::park::PersistValidSeconds, ai::park::PersistData(teleName.c_str()));
}

void RandomPlayerbotMgr::ClearPersistedPark(uint32 guid)
{
    // Also with the switch off, so a row left by an earlier run never parks a bot the operator unparked.
    if (GetEventValue(guid, ai::park::PersistEvent))
        SetEventValue(guid, ai::park::PersistEvent, 0, 0);
}

void RandomPlayerbotMgr::RestoreParkedBots()
{
    for (auto const& [guid, bot] : GetAllBots())
    {
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (!ai || ai->parkRestoreChecked || IsParkedBot(guid))
            continue;
        if (!GetEventValue(guid, ai::park::PersistEvent))
        {
            ai->parkRestoreChecked = true;
            continue;
        }
        std::string const data = GetEventData(guid, ai::park::PersistEvent);
        std::string reason;
        ++ai->parkRestoreAttempts;
        if (ParkBot(bot, ai::park::RestoreTarget(data.c_str()), reason))
        {
            ai->parkRestoreChecked = true;
            sLog.outBasic("[Park] state=restored bot=%u via=%s attempts=%u", guid, data.c_str(), ai->parkRestoreAttempts);
        }
        else if (!ai::park::RestoreRetries(reason.c_str(), ai->parkRestoreAttempts))
        {
            ai->parkRestoreChecked = true;
            sLog.outBasic("[Park] state=restore_failed bot=%u via=%s reason=%s attempts=%u", guid, data.c_str(), reason.c_str(),
                ai->parkRestoreAttempts);
        }
    }
}

void RandomPlayerbotMgr::UnparkBot(Player* bot)
{
    auto const it = parkedBots.find(bot->GetGUIDLow());
    if (it == parkedBots.end())
        return;
    ReleaseParkSpot(it->second);
    parkedBots.erase(it);
    ClearPersistedPark(bot->GetGUIDLow());   // twow-repo#551 (AiPlayerbot.Park.Persist)

    if (PlayerbotAI* ai = GetBotAI(bot))
    {
        ai->SetParked(false);
        ai->ResetStrategies();
    }
    bot->SetHiddenFromBots(false);  // twow-repo#541/#551: visible again to every bot
    if (bot->HasGridSleepFlag())    // twow-repo#541: its cells wake in the next map update
    {
        ++parkSleepUnparkWakes;
        bot->SetGridSleep(false);
    }
    bot->SetStandState(UNIT_STAND_STATE_STAND);
    sLog.outBasic("[Park] state=unparked bot=%u level=%u", bot->GetGUIDLow(), bot->GetLevel());
}

// twow-repo#541 (grid sleep): hands the switches to the core every pass (cheap, follows a config reload)
// and writes one [ParkSleep] line per minute. Per-update values are averages over continent cell updates
// (one region update = one sample): sleeping/woken bots per region update and marked cells per region update.
// wake_attack / wake_other count the transitions asleep -> awake in the map threads, wake_unpark the
// unparked bots that carried the flag.
void RandomPlayerbotMgr::ReportParkSleep()
{
    bool const stats = sPlayerbotAIConfig.parkSleepGrid || sPlayerbotAIConfig.botUpdateTrace;
    Map::SetGridSleepSwitches(sPlayerbotAIConfig.parkSleepGrid, stats);

    uint32 const now = uint32(time(nullptr));

    // twow-repo#541 P2 D0 (AiPlayerbot.Park.CellSourceStats): the continent cells by source, per continent cell update.
    // cells_total = real + bot_only + npc_only; activation = the grid activation distance (yards, it shrinks under load).
    Map::SetCellSourceStats(sPlayerbotAIConfig.parkCellSourceStats);
    static uint32 lastSourceReport = 0;
    if (sPlayerbotAIConfig.parkCellSourceStats && now >= lastSourceReport + 60)
    {
        bool const firstSource = lastSourceReport == 0;
        lastSourceReport = now;
        Map::CellSourceStats& c = Map::GetCellSourceStats();
        uint64 const updates = c.updates.exchange(0, std::memory_order_relaxed);
        uint64 const real = c.cellsReal.exchange(0, std::memory_order_relaxed);
        uint64 const botOnly = c.cellsBotOnly.exchange(0, std::memory_order_relaxed);
        uint64 const npcOnly = c.cellsNpcOnly.exchange(0, std::memory_order_relaxed);
        uint64 const realPlayers = c.realPlayers.exchange(0, std::memory_order_relaxed);
        uint64 const bots = c.bots.exchange(0, std::memory_order_relaxed);
        uint64 const activeNpcs = c.activeNpcs.exchange(0, std::memory_order_relaxed);
        uint64 const yards = c.activationYards.exchange(0, std::memory_order_relaxed);
        // The first window starts whenever the switch was read; it is reset, not reported.
        if (!firstSource)
        {
            auto const perUpdate = [updates](uint64 sum) { return updates ? double(sum) / double(updates) : 0.0; };
            sLog.outBasic("[CellSource] region_updates=%llu cells_total=%.0f cells_real=%.0f cells_bot_only=%.0f cells_npc_only=%.0f "
                "real_players=%.1f bots=%.1f active_npcs=%.1f activation=%.0f", (unsigned long long)updates,
                perUpdate(real + botOnly + npcOnly), perUpdate(real), perUpdate(botOnly), perUpdate(npcOnly), perUpdate(realPlayers),
                perUpdate(bots), perUpdate(activeNpcs), perUpdate(yards));
        }
    }

    static uint32 lastReport = 0;
    if (!stats || now < lastReport + 60)
        return;
    bool const first = lastReport == 0;
    lastReport = now;

    Map::GridSleepStats& s = Map::GetGridSleepStats();
    uint64 const updates = s.updates.exchange(0, std::memory_order_relaxed);
    uint64 const sleeping = s.sleeping.exchange(0, std::memory_order_relaxed);
    uint64 const woken = s.woken.exchange(0, std::memory_order_relaxed);
    uint64 const wakeAttack = s.wakeAttack.exchange(0, std::memory_order_relaxed);
    uint64 const wakeOther = s.wakeOther.exchange(0, std::memory_order_relaxed);
    uint64 const cells = s.cellsMarked.exchange(0, std::memory_order_relaxed);
    uint32 const wakeUnpark = parkSleepUnparkWakes;
    parkSleepUnparkWakes = 0;
    // The first window starts whenever the switch was read; it is reset, not reported.
    if (first)
        return;

    uint32 flagged = 0;
    for (auto const& [guid, entry] : parkedBots)
        if (Player* bot = GetPlayerBot(guid))
            if (bot->HasGridSleepFlag())
                ++flagged;

    auto const perUpdate = [updates](uint64 sum) { return updates ? double(sum) / double(updates) : 0.0; };
    sLog.outBasic("[ParkSleep] enabled=%u parked=%u flagged=%u region_updates=%llu sleeping_per_update=%.1f "
        "woken_per_update=%.1f cells_marked_per_update=%.0f wake_attack=%llu wake_other=%llu wake_unpark=%u",
        sPlayerbotAIConfig.parkSleepGrid ? 1u : 0u, uint32(parkedBots.size()), flagged, (unsigned long long)updates,
        perUpdate(sleeping), perUpdate(woken), perUpdate(cells), (unsigned long long)wakeAttack,
        (unsigned long long)wakeOther, wakeUnpark);
}

void RandomPlayerbotMgr::ProcessParkedBots()
{
    uint32 const now = uint32(time(nullptr));

    // twow-repo#551: once a minute while bots were parked - how many went to an inn, a capital spot or
    // stayed where they stood, and whether inns of any level were allowed (AiPlayerbot.Park.AnyLevelInn).
    bool const capsOn = sPlayerbotAIConfig.parkMaxPerInn || sPlayerbotAIConfig.parkMaxPerCity;
    if (now >= parkShare.lastLog + 60 &&
        (parkShare.inn || parkShare.city || parkShare.here || parkShare.noSpot || (capsOn && !parkedBots.empty())))
    {
        sLog.outBasic("[Park] state=share inn=%u city=%u here=%u any_level_inn=%u", parkShare.inn, parkShare.city,
            parkShare.here, sPlayerbotAIConfig.parkAnyLevelInn ? 1u : 0u);
        // twow-repo#551 (owner 10.10., Park.MaxPerInn / Park.MaxPerCity): occupancy with the caps - inns in use and
        // full, the fullest inn, bots per capital zone, how many overflowed to another spot and how many found none.
        if (capsOn)
        {
            uint32 const innCapacity = ai::park::SpotCapacityFor(true, sPlayerbotAIConfig.parkMaxPerInn);
            uint32 innsUsed = 0, innsFull = 0, innMax = 0, innBots = 0;
            std::ostringstream innList;
            for (uint32 team = 0; team < 2; ++team)
                for (ParkInn const& inn : parkInns[team])
                {
                    auto const slots = parkSpotSlots.find(ParkSpotKey(inn.loc));
                    uint32 const taken = slots == parkSpotSlots.end() ? 0 : uint32(slots->second.size());
                    if (!taken)
                        continue;
                    ++innsUsed;
                    innBots += taken;
                    innMax = std::max(innMax, taken);
                    if (taken >= innCapacity)
                        ++innsFull;
                    innList << (innsUsed > 1 ? "," : "") << inn.area << ":" << taken;
                }
            std::ostringstream cityList;
            for (auto const& [zone, count] : parkCityCounts.Snapshot())
                cityList << (cityList.tellp() > 0 ? "," : "") << zone << ":" << count;
            sLog.outBasic("[Park] state=caps max_inn=%u max_city=%u inns_used=%u inns_full=%u inn_max=%u inn_bots=%u overflow=%u no_spot=%u cities=%s inns=%s",
                sPlayerbotAIConfig.parkMaxPerInn, sPlayerbotAIConfig.parkMaxPerCity, innsUsed, innsFull, innMax, innBots,
                parkShare.overflow, parkShare.noSpot, cityList.str().c_str(), innList.str().c_str());
        }
        parkShare.inn = parkShare.city = parkShare.here = 0;
        parkShare.overflow = parkShare.noSpot = 0;
        parkShare.lastLog = now;
    }

    // twow-repo#551 (AiPlayerbot.Park.Persist): bots that were parked before a restart or logout are parked again.
    if (sPlayerbotAIConfig.parkPersist && now >= lastParkRestore + ai::park::RestoreIntervalSeconds)
    {
        lastParkRestore = now;
        RestoreParkedBots();
    }

    for (auto it = parkedBots.begin(); it != parkedBots.end();)
    {
        uint32 const guid = it->first;
        ParkEntry& entry = it->second;
        Player* bot = GetPlayerBot(guid);
        if (!bot)
        {
            sLog.outBasic("[Park] state=lost bot=%u", guid);
            ReleaseParkSpot(entry);
            it = parkedBots.erase(it);
            continue;
        }
        ++it;

        if (!bot->IsInWorld() || bot->IsBeingTeleported())
            continue;

        switch (entry.stage)
        {
            case ai::park::Stage::Travel:
                if (ai::park::Arrived(ParkPoint(bot), ParkPoint(entry.inn)))
                    entry.stage = ai::park::Stage::Bind;
                else if (now - entry.since >= ai::park::ArriveTimeoutSeconds)
                {
                    entry.since = now;
                    bot->TeleportTo(entry.inn.mapid, entry.inn.coord_x, entry.inn.coord_y, entry.inn.coord_z, entry.inn.orientation);
                }
                break;
            case ai::park::Stage::Bind:
                // Server side and written to the database (Player::SetHomebindToLocation); the former
                // random-bot "bind" only sent a trainer packet to the bot and bound nothing.
                bot->SetHomebindToLocation(entry.inn, entry.area);
                ++entry.tries;
                entry.checkAt = now + ai::park::BindCheckDelaySeconds;
                entry.checkPending = false;
                entry.stage = ai::park::Stage::Verify;
                sLog.outBasic("[Park] state=bind bot=%u try=%u map=%u area=%u", guid, entry.tries, entry.inn.mapid, entry.area);
                break;
            case ai::park::Stage::Verify:
                if (!entry.checkPending && now >= entry.checkAt)
                {
                    entry.checkPending = true;
                    CharacterDatabase.AsyncPQuery(&RandomPlayerbotMgr::ParkBindCheck, guid,
                        "SELECT `map`, `position_x`, `position_y`, `position_z` FROM `character_homebind` WHERE `guid` = '%u'", guid);
                }
                break;
            case ai::park::Stage::Parked:
            case ai::park::Stage::BindFailed:
                // twow-repo#551 (Park.Seats): on its chair like a player (place, facing, chair height); otherwise as before.
                if (entry.seatHeight >= 0)
                {
                    uint8 const chairState = uint8(UNIT_STAND_STATE_SIT_LOW_CHAIR + entry.seatHeight);
                    if (bot->getStandState() != chairState && !bot->IsInCombat() && !bot->IsMoving() && bot->IsAlive())
                    {
                        if (!bot->IsWithinDist3d(entry.inn.coord_x, entry.inn.coord_y, entry.inn.coord_z, 1.0f, SizeFactor::None))
                            bot->NearTeleportTo(entry.inn.coord_x, entry.inn.coord_y, entry.inn.coord_z, entry.inn.orientation);
                        else
                        {
                            bot->SetFacingTo(entry.inn.orientation);
                            bot->SetStandState(chairState);
                            // Once per park: the acceptance signal (seat, place, stand state) - the distance to the innkeeper
                            // follows from the coordinates.
                            if (!entry.seatedLogged)
                            {
                                entry.seatedLogged = true;
                                sLog.outBasic("[Park] state=seated bot=%u slot=%u seat=1 stand=%u map=%u x=%.1f y=%.1f z=%.1f seconds=%u", guid, entry.slot,
                                    uint32(bot->getStandState()), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                    now - entry.since);
                            }
                        }
                    }
                }
                else if (bot->getStandState() != UNIT_STAND_STATE_SIT && !bot->IsInCombat() && !bot->IsMoving())
                    bot->SetStandState(UNIT_STAND_STATE_SIT);
                // twow-repo#551 (Park.Seats): a bot without a free seat stands on the outer rings - logged once as seat=0.
                if (sPlayerbotAIConfig.parkSeats && entry.seatHeight < 0 && !entry.seatedLogged && !bot->IsMoving())
                {
                    entry.seatedLogged = true;
                    sLog.outBasic("[Park] state=seated bot=%u slot=%u seat=0 stand=%u map=%u x=%.1f y=%.1f z=%.1f seconds=%u", guid, entry.slot,
                        uint32(bot->getStandState()), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                        now - entry.since);
                }
                // twow-repo#541 (AiPlayerbot.Park.SleepGrid): only once it has arrived; the core keeps the
                // cells awake while the bot fights, is dead, teleports or flies.
                if (bot->HasGridSleepFlag() != sPlayerbotAIConfig.parkSleepGrid)
                    bot->SetGridSleep(sPlayerbotAIConfig.parkSleepGrid);
                break;
        }
    }
}

void RandomPlayerbotMgr::ParkBindCheck(QueryResult* result, uint32 guid)
{
    ai::park::Point stored;
    bool const found = result != nullptr;
    if (result)
    {
        Field* fields = result->Fetch();
        stored.map = fields[0].GetUInt32();
        stored.x = fields[1].GetFloat();
        stored.y = fields[2].GetFloat();
        stored.z = fields[3].GetFloat();
        delete result;
    }
    sRandomPlayerbotMgr.OnParkBindChecked(guid, found, stored);
}

void RandomPlayerbotMgr::OnParkBindChecked(uint32 guid, bool found, ai::park::Point const& stored)
{
    auto const it = parkedBots.find(guid);
    if (it == parkedBots.end() || it->second.stage != ai::park::Stage::Verify)
        return;
    ParkEntry& entry = it->second;
    Player* bot = GetPlayerBot(guid);

    if (found && ai::park::BindMatches(stored, ParkPoint(entry.inn)))
    {
        entry.stage = ai::park::Stage::Parked;
        sLog.outBasic("[Park] state=bound bot=%u try=%u map=%u x=%.0f y=%.0f", guid, entry.tries, stored.map, stored.x, stored.y);
        if (bot)
        {
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            sLog.outBasic("[Park] state=parked bot=%u level=%u area=%u", guid, bot->GetLevel(), entry.area);
        }
        return;
    }

    if (ai::park::RetryBind(entry.tries))
    {
        entry.stage = ai::park::Stage::Bind;
        sLog.outBasic("[Park] state=bind_retry bot=%u try=%u found=%u", guid, entry.tries, found ? 1u : 0u);
        return;
    }

    entry.stage = ai::park::Stage::BindFailed;
    sLog.outBasic("[Park] state=bind_failed bot=%u tries=%u found=%u stored_map=%u", guid, entry.tries, found ? 1u : 0u, stored.map);
    if (bot)
        bot->SetStandState(UNIT_STAND_STATE_SIT);
}

// rndbot park <name|count> [game_tele name]
std::list<std::string> RandomPlayerbotMgr::HandleConsolePark(std::string param)
{
    std::list<std::string> messages;
    std::string target = param, teleName;
    size_t const space = param.find(' ');
    if (space != std::string::npos)
    {
        target = param.substr(0, space);
        teleName = param.substr(space + 1);
    }
    if (target.empty())
    {
        messages.push_back("Usage: rndbot park <name|count> [game_tele name | here]");
        return messages;
    }

    uint32 parked = 0, refused = 0;
    std::map<std::string, uint32> reasons;
    bool const byCount = target.find_first_not_of("0123456789") == std::string::npos;
    if (byCount)
    {
        uint32 const wanted = uint32(std::stoul(target));
        for (auto const& [guid, bot] : GetAllBots())
        {
            if (parked >= wanted)
                break;
            if (!bot || IsParkedBot(guid))
                continue;
            std::string reason;
            if (ParkBot(bot, teleName, reason))
                ++parked;
            else
            {
                ++reasons[reason];
                ++refused;
            }
        }
    }
    else
    {
        std::string reason = "not_found";
        Player* bot = ObjectAccessor::FindPlayerByName(target.c_str());
        if (bot && GetPlayerBot(bot->GetGUIDLow()) && ParkBot(bot, teleName, reason))
            ++parked;
        else
        {
            ++reasons[reason];
            ++refused;
        }
    }

    std::string text = "park: parked=" + std::to_string(parked) + " refused=" + std::to_string(refused) +
        " total_parked=" + std::to_string(parkedBots.size());
    for (auto const& [reason, count] : reasons)
        text += " " + reason + "=" + std::to_string(count);
    messages.push_back(text);
    return messages;
}

// rndbot unpark <name|count|all>
std::list<std::string> RandomPlayerbotMgr::HandleConsoleUnpark(std::string param)
{
    std::list<std::string> messages;
    std::vector<uint32> guids;
    if (param == "all" || param.empty())
    {
        for (auto const& [guid, entry] : parkedBots)
            guids.push_back(guid);
    }
    else if (param.find_first_not_of("0123456789") == std::string::npos)
    {
        uint32 const wanted = uint32(std::stoul(param));
        for (auto const& [guid, entry] : parkedBots)
        {
            if (guids.size() >= wanted)
                break;
            guids.push_back(guid);
        }
    }
    else if (Player* bot = ObjectAccessor::FindPlayerByName(param.c_str()))
        guids.push_back(bot->GetGUIDLow());

    uint32 unparked = 0;
    for (uint32 guid : guids)
    {
        if (Player* bot = GetPlayerBot(guid))
        {
            UnparkBot(bot);
            ++unparked;
        }
        else
        {
            auto const lost = parkedBots.find(guid);
            if (lost != parkedBots.end())
            {
                ReleaseParkSpot(lost->second);
                parkedBots.erase(lost);
                ClearPersistedPark(guid);   // twow-repo#551 (AiPlayerbot.Park.Persist)
            }
        }
    }
    messages.push_back("unpark: unparked=" + std::to_string(unparked) + " total_parked=" + std::to_string(parkedBots.size()));
    return messages;
}
