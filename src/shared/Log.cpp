/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "Config/Config.h"
#include "Util.h"
#include "ByteBuffer.h"
#include "DiscordBot/Bot.hpp"
#include "Timer.h"

#include <stdarg.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <thread>

#include "ace/OS_NS_unistd.h"

typedef MaNGOS::ClassLevelLockable<Log, std::mutex> LogLock;
INSTANTIATE_SINGLETON_2(Log, LogLock);
INSTANTIATE_CLASS_MUTEX(Log, std::mutex);

LogFilterData logFilterData[LOG_FILTER_COUNT] =
{
    { "transport_moves",     "LogFilter_TransportMoves",     true  },
    { "creature_moves",      "LogFilter_CreatureMoves",      true  },
    { "visibility_changes",  "LogFilter_VisibilityChanges",  true  },
    { "",                    "",                             true  },
    { "weather",             "LogFilter_Weather",            true  },
    { "player_stats",        "LogFilter_PlayerStats",        false },
    { "sql_text",            "LogFilter_SQLText",            false },
    { "player_moves",        "LogFilter_PlayerMoves",        false },
    { "periodic_effects",    "LogFilter_PeriodicAffects",    false },
    { "ai_and_movegens",     "LogFilter_AIAndMovegens",      false },
    { "damage",              "LogFilter_Damage",             false },
    { "combat",              "LogFilter_Combat",             false },
    { "spell_cast",          "LogFilter_SpellCast",          false },
    { "db_stricted_check",   "LogFilter_DbStrictedCheck",    true  },
    { "pathfinding",         "LogFilter_Pathfinding",        false },
    { "honor",               "LogFilter_Honor",              true  },
};

Log::Log() :
    logfile(nullptr), gmLogfile(nullptr), dberLogfile(nullptr), wardenLogfile(nullptr), anticheatLogfile(nullptr), honorLogfile(nullptr), raidLogFile(nullptr), m_colored(false), m_includeTime(false), m_wardenDebug(false), m_gmlog_per_account(false)
{
    for (int i = 0; i < LOG_MAX_FILES; ++i)
    {
        logFiles[i] = nullptr;
        timestampPrefix[i] = true;
    }
    Initialize();
}

void Log::InitColors(const std::string& str)
{
    if (str.empty())
    {
        m_colored = false;
        return;
    }

    int color[LOG_TYPE_MAX];

    std::istringstream ss(str);

    for(int i = 0; i < LOG_TYPE_MAX; ++i)
    {
        ss >> color[i];

        if(!ss)
            return;

        if(color[i] < 0 || color[i] >= Color_count)
            return;
    }

    for(int i = 0; i < LOG_TYPE_MAX; ++i)
        m_colors[i] = Color(color[i]);

    m_colored = true;
}

void Log::InitSmartlogEntries(const std::string& str)
{
    m_smartlogExtraEntries.clear();

    if (str.empty())
        return;

    uint32 entry;
    std::istringstream ss(str);

    while (ss)
    {
        ss >> entry;
        m_smartlogExtraEntries.push_back(entry);
    }
}

void Log::InitSmartlogGuids(const std::string& str)
{
    m_smartlogExtraGuids.clear();

    if (str.empty())
        return;

    uint32 entry;
    std::istringstream ss(str);

    while (ss)
    {
        ss >> entry;
        m_smartlogExtraGuids.push_back(entry);
    }
}

void Log::LogDiscord(LogFile type, std::string log)
{
#ifdef USING_DISCORD_BOT
    static const std::unordered_map<LogFile, uint64_t> ChannelLookup =
    {
       // {LOG_MONEY_TRADES, 1078715732013105252}
    };

    if (ChannelLookup.find(type) == ChannelLookup.end())
        return;

    sDiscordBot->SendMessageToChannel(ChannelLookup.find(type)->second, std::move(log));
#endif
}

void Log::SetColor(bool stdout_stream, Color color)
{
    #if PLATFORM == PLATFORM_WINDOWS

    static WORD WinColorFG[Color_count] =
    {
        0,                                                  // BLACK
        FOREGROUND_RED,                                     // RED
        FOREGROUND_GREEN,                                   // GREEN
        FOREGROUND_RED | FOREGROUND_GREEN,                  // BROWN
        FOREGROUND_BLUE,                                    // BLUE
        FOREGROUND_RED |                    FOREGROUND_BLUE,// MAGENTA
        FOREGROUND_GREEN | FOREGROUND_BLUE,                 // CYAN
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,// WHITE
                                                            // YELLOW
        FOREGROUND_RED | FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
                                                            // RED_BOLD
        FOREGROUND_RED |                                      FOREGROUND_INTENSITY,
                                                            // GREEN_BOLD
        FOREGROUND_GREEN |                   FOREGROUND_INTENSITY,
        FOREGROUND_BLUE | FOREGROUND_INTENSITY,             // BLUE_BOLD
                                                            // MAGENTA_BOLD
        FOREGROUND_RED |                    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // CYAN_BOLD
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
                                                            // WHITE_BOLD
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
    };

    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
    SetConsoleTextAttribute(hConsole, WinColorFG[color]);
    #else

    enum ANSITextAttr
    {
        TA_NORMAL=0,
        TA_BOLD=1,
        TA_BLINK=5,
        TA_REVERSE=7
    };

    enum ANSIFgTextAttr
    {
        FG_BLACK=30, FG_RED,  FG_GREEN, FG_BROWN, FG_BLUE,
        FG_MAGENTA,  FG_CYAN, FG_WHITE, FG_YELLOW
    };

    enum ANSIBgTextAttr
    {
        BG_BLACK=40, BG_RED,  BG_GREEN, BG_BROWN, BG_BLUE,
        BG_MAGENTA,  BG_CYAN, BG_WHITE
    };

    static uint8 UnixColorFG[Color_count] =
    {
        FG_BLACK,                                           // BLACK
        FG_RED,                                             // RED
        FG_GREEN,                                           // GREEN
        FG_BROWN,                                           // BROWN
        FG_BLUE,                                            // BLUE
        FG_MAGENTA,                                         // MAGENTA
        FG_CYAN,                                            // CYAN
        FG_WHITE,                                           // WHITE
        FG_YELLOW,                                          // YELLOW
        FG_RED,                                             // LRED
        FG_GREEN,                                           // LGREEN
        FG_BLUE,                                            // LBLUE
        FG_MAGENTA,                                         // LMAGENTA
        FG_CYAN,                                            // LCYAN
        FG_WHITE                                            // LWHITE
    };

    fprintf((stdout_stream? stdout : stderr), "\x1b[%d%sm",UnixColorFG[color],(color>=YELLOW&&color<Color_count ?";1":""));
    #endif
}

void Log::ResetColor(bool stdout_stream)
{
    #if PLATFORM == PLATFORM_WINDOWS
    HANDLE hConsole = GetStdHandle(stdout_stream ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED );
    #else
    fprintf(( stdout_stream ? stdout : stderr ), "\x1b[0m");
    #endif
}

void Log::SetLogLevel(char* level)
{
    int32 newLevel =atoi((char*)level);

    if (newLevel < LOG_LVL_MINIMAL)
        newLevel = LOG_LVL_MINIMAL;
    else if (newLevel > LOG_LVL_DEBUG)
        newLevel = LOG_LVL_DEBUG;

    m_logLevel = LogLevel(newLevel);

    printf("LogLevel is %d\n", m_logLevel);
}

void Log::SetLogFileLevel(char* level)
{
    int32 newLevel =atoi((char*)level);

    if (newLevel < LOG_LVL_MINIMAL)
        newLevel = LOG_LVL_MINIMAL;
    else if (newLevel > LOG_LVL_DEBUG)
        newLevel = LOG_LVL_DEBUG;

    m_logFileLevel = LogLevel(newLevel);

    printf("LogFileLevel is %d\n", m_logFileLevel);
}

void Log::Initialize()
{
    // not ideal place for that, but Log::Initialize used everywhere
    CPU::Init();
    /// Common log files data
    m_logsDir = sConfig.GetStringDefault("LogsDir","");
    if (!m_logsDir.empty())
    {
        if ((m_logsDir.at(m_logsDir.length()-1)!='/') && (m_logsDir.at(m_logsDir.length()-1)!='\\'))
            m_logsDir.append("/");
    }

    m_bIsChatLogFileActivated = sConfig.GetBoolDefault("ChatLogEnable", false);

    m_logsTimestamp = "_" + GetTimestampStr();

    if (FILE* pFile = fopen("splitlogs", "rb"))
    {
        fread(&m_lastLogSplitTime, sizeof(size_t), 1, pFile);
        fclose(pFile);
    }
    else
        m_lastLogSplitTime = 0;

    /// Open specific log files
    logfile = openLogFile("LogFile","LogTimestamp","w");

    m_gmlog_per_account = sConfig.GetBoolDefault("GmLogPerAccount",false);
    if (!m_gmlog_per_account)
        gmLogfile = openLogFile("GMLogFile","GmLogTimestamp","a");
    else
    {
        // GM log settings for per account case
        m_gmlog_filename_format = sConfig.GetStringDefault("GMLogFile", "");
        if (!m_gmlog_filename_format.empty())
        {
            bool m_gmlog_timestamp = sConfig.GetBoolDefault("GmLogTimestamp",false);

            size_t dot_pos = m_gmlog_filename_format.find_last_of('.');
            if (dot_pos!=m_gmlog_filename_format.npos)
            {
                if (m_gmlog_timestamp)
                    m_gmlog_filename_format.insert(dot_pos,m_logsTimestamp);

                m_gmlog_filename_format.insert(dot_pos,"_#%u");
            }
            else
            {
                m_gmlog_filename_format += "_#%u";

                if (m_gmlog_timestamp)
                    m_gmlog_filename_format += m_logsTimestamp;
            }

            m_gmlog_filename_format = m_logsDir + m_gmlog_filename_format;
        }
    }

    dberLogfile = openLogFile("DBErrorLogFile", nullptr, "a+");
    worldLogfile = openLogFile("WorldLogFile", "WorldLogTimestamp", "a+");
    nostalriusLogFile = openLogFile("NostalriusLogFile", "NostalriusLogTimestamp", "a+");
    honorLogfile = openLogFile("HonorLogFile", "HonorLogTimestamp", "a+");
    wardenLogfile = openLogFile("WardenLogFile", "WardenLogTimestamp", "a+");
    anticheatLogfile = openLogFile("AnticheatLogFile", "AnticheatLogTimestamp", "a+");
    discordLogFile = openLogFile("DiscordLogFile", "DiscordLogTimestamp", "a+");
    discordCoreLogFile = openLogFile("DiscordCoreLogFile", "DiscordLogTimestamp", "a+");
    raidLogFile = openLogFile("RaidLogFile", "RaidLogTimestamp", "a+");
    logFiles[LOG_CHAT] = openLogFile("ChatLogFile", "ChatLogTimestamp", "a+");
    logFiles[LOG_BG] = openLogFile("BgLogFile", "BgLogTimestamp", "a+");
    logFiles[LOG_CHAR] = openLogFile("CharLogFile", "CharLogTimestamp", "a+");
    logFiles[LOG_RA] = openLogFile("RaLogFile", nullptr, "a+");
    logFiles[LOG_DBERRFIX] = openLogFile("DBErrorFixFile", nullptr, "w+");
    logFiles[LOG_CLIENT_IDS] = openLogFile("ClientIdsLogFile", nullptr, "a+");
    logFiles[LOG_LOOTS] = openLogFile("LootsLogFile", nullptr, "a+");
    logFiles[LOG_RARELOOTS] = openLogFile("RareLootsLogFile", nullptr, "a+");
    logFiles[LOG_LEVELUP] = openLogFile("LevelupLogFile", nullptr, "a+");
    logFiles[LOG_PERFORMANCE] = openLogFile("PerformanceLog.File", nullptr, "a+");
    logFiles[LOG_MONEY_TRADES] = openLogFile("LogMoneyTrades", nullptr, "a+");
    logFiles[LOG_ANTICHEAT_DEBUG] = openLogFile("AnticheatDebugLogFile", nullptr, "a+");
    logFiles[LOG_ANTICHEAT_BASIC] = openLogFile("AnticheatLogFile", nullptr, "a+");
    logFiles[LOG_MAIL_AH] = openLogFile("LogMailAH", nullptr, "a+");
    logFiles[LOG_GM_CRITICAL] = openLogFile("CriticalCommandsLogFile", nullptr, "a+");
    logFiles[LOG_CHAT_SPAM] = openLogFile("ChatSpamLogFile", nullptr, "a+");
    logFiles[LOG_EXPLOITS] = openLogFile("ExploitsLogFile", nullptr, "a+");
    logFiles[LOG_HARDCORE_MODE] = openLogFile("HardcoreModeLogFile", nullptr, "a+");
logFiles[LOG_AUTOUPDATER] = openLogFile("DBUpdaterLogFile", nullptr, "a+");
logFiles[LOG_API] = openLogFile("ApiLogFile", nullptr, "a+");
logFiles[LOG_RACE_CHANGE] = fopen((m_logsDir + "racechange.log").c_str(), "a+");
#ifdef ENABLE_ELUNA
logFiles[LOG_ELUNA] = openLogFile("ElunaErrorLogFile", nullptr, "a+");
#endif

timestampPrefix[LOG_DBERRFIX] = false;

// Main log file settings
m_wardenDebug = sConfig.GetBoolDefault("Warden.DebugLog", false);
m_includeTime = sConfig.GetBoolDefault("LogTime", false);
m_logLevel = LogLevel(sConfig.GetIntDefault("LogLevel", 0));
m_logFileLevel = LogLevel(sConfig.GetIntDefault("LogFileLevel", 0));
InitColors(sConfig.GetStringDefault("LogColors", ""));

// Smartlog data
InitSmartlogEntries(sConfig.GetStringDefault("Smartlog.ExtraEntries", ""));
InitSmartlogGuids(sConfig.GetStringDefault("Smartlog.ExtraGuids", ""));

m_logFilter = 0;
for (int i = 0; i < LOG_FILTER_COUNT; ++i)
    if (*logFilterData[i].name)
        if (sConfig.GetBoolDefault(logFilterData[i].configName, logFilterData[i].defaultState))
            m_logFilter |= (1 << i);

// Char log settings
m_charLog_Dump = sConfig.GetBoolDefault("CharLogDump", false);
}

FILE* Log::openLogFile(char const* configFileName, char const* configTimeStampFlag, char const* mode)
{
    std::string logfn = sConfig.GetStringDefault(configFileName, "");
    if (logfn.empty())
        return nullptr;

    if (configTimeStampFlag && sConfig.GetBoolDefault(configTimeStampFlag, false))
    {
        size_t dot_pos = logfn.find_last_of('.');
        if (dot_pos != logfn.npos)
            logfn.insert(dot_pos, m_logsTimestamp);
        else
            logfn += m_logsTimestamp;
    }

#ifndef WIN32
    if (sConfig.GetBoolDefault("SplitLogs", false) && ((m_lastLogSplitTime + MONTH) < time(nullptr)))
    {
        if (FILE* pFile = fopen((m_logsDir + logfn).c_str(), mode))
        {
            enum
            {
                KB = 1000,
                MB = KB * 1000,
                GB = MB * 1000

            };

            fseek(pFile, 0L, SEEK_END);
            if (ftell(pFile) > 50 * GB && !(configTimeStampFlag && sConfig.GetBoolDefault(configTimeStampFlag, false)))
            {
                printf("splitting log file %s\n", configFileName);
                fclose(pFile);

                if (pFile = fopen("splitlogs", "wb"))
                {
                    m_lastLogSplitTime = time(nullptr);
                    fwrite(&m_lastLogSplitTime, sizeof(size_t), 1, pFile);
                    fclose(pFile);
                }

                std::string cmd = "split -b 50G -d  \"" + (m_logsDir + logfn) + "\" \"" + (m_logsDir + logfn) + "\"";
                system(cmd.c_str());

                uint32 i = 0;
                do
                {
                    std::string num = std::to_string(i);
                    if (num.size() == 1)
                        num = "0" + num;

                    pFile = fopen((m_logsDir + logfn + num).c_str(), "rb");
                    if (!pFile)
                    {
                        printf("error, %s does not exist\n", (m_logsDir + logfn + num).c_str());
                        break;
                    }
                    fclose(pFile);

                    std::string num2 = std::to_string(i + 1);
                    if (num2.size() == 1)
                        num2 = "0" + num2;

                    // check if there is a next file
                    // last one we rename to default name
                    if (FILE* pNextFile = fopen((m_logsDir + logfn + num2).c_str(), "rb"))
                    {
                        fclose(pNextFile);

                        std::string name = logfn;
                        {
                            size_t dot_pos = name.find_last_of('.');
                            if (dot_pos != name.npos)
                                name.insert(dot_pos, "_split_" + m_logsTimestamp + num);
                            else
                                name += "_split_" + m_logsTimestamp + num;
                        }

                        cmd = "mv \"" + (m_logsDir + logfn + num) + "\" \"" + (m_logsDir + name) + "\"";
                        system(cmd.c_str());
                    }
                    else
                    {
                        // this is the last file, rename it to default name and continue using it

                        cmd = "rm " + (m_logsDir + logfn);
                        system(cmd.c_str());

                        cmd = "mv \"" + (m_logsDir + logfn + num) + "\" \"" + (m_logsDir + logfn) + "\"";
                        system(cmd.c_str());

                        break;
                    }

                    ++i;
                } while (true);

                pFile = fopen((m_logsDir + logfn).c_str(), mode);
            }

            return pFile;
        }
    }
#endif

    return fopen((m_logsDir + logfn).c_str(), mode);
}

FILE* Log::openGmlogPerAccount(uint32 account)
{
    if (m_gmlog_filename_format.empty())
        return nullptr;

    char namebuf[MANGOS_PATH_MAX];
    snprintf(namebuf,MANGOS_PATH_MAX,m_gmlog_filename_format.c_str(),account);
    return fopen(namebuf, "a");
}

void Log::outTimestamp(FILE* file)
{
    time_t t = time(nullptr);
    tm* aTm = localtime(&t);
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    fprintf(file,"%-4d-%02d-%02d %02d:%02d:%02d ",aTm->tm_year+1900,aTm->tm_mon+1,aTm->tm_mday,aTm->tm_hour,aTm->tm_min,aTm->tm_sec);
}

void Log::outTime(FILE* where)
{
    time_t t = time(nullptr);
    tm* aTm = localtime(&t);
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    fprintf(where, "%02d:%02d:%02d ",aTm->tm_hour,aTm->tm_min,aTm->tm_sec);
}

std::string Log::GetTimestampStr()
{
    time_t t = time(nullptr);
    tm* aTm = localtime(&t);
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    char buf[20];
    snprintf(buf,20,"%04d-%02d-%02d_%02d-%02d-%02d",aTm->tm_year+1900,aTm->tm_mon+1,aTm->tm_mday,aTm->tm_hour,aTm->tm_min,aTm->tm_sec);
    return std::string(buf);
}

// twow-repo#597 (TICK-01, Log.AsyncSink): the queue and the writer thread. One short std::mutex guards
// the FIFO and the flags; the writer pops at most log_sink::PopChunk lines under it (bounded time under
// the lock, also right after a stall) and writes them without any lock. It never touches sLog: only this
// state and the FILE* handles in the lines.
struct LogAsyncSinkState
{
    explicit LogAsyncSinkState(std::size_t maxLines) : fifo(maxLines, log_sink::MaxQueuedBytes) {}

    std::mutex mutex;
    std::condition_variable work;   // writer: lines queued, stop or abandon
    std::condition_variable idle;   // drain / flush: lines written
    log_sink::BoundedFifo<log_sink::Line> fifo;
    bool stop = false;      // drained: the writer ends
    bool abandon = false;   // the drain timed out: the writer ends after its current chunk
    std::thread thread;     // joined or detached by Log::StopAsyncSink only

    log_sink::PushResult Push(log_sink::Line& line)
    {
        std::size_t const bytes = line.Bytes();
        log_sink::PushResult r;
        {
            std::lock_guard<std::mutex> guard(mutex);
            r = fifo.TryPush(line, bytes);
        }
        if (r == log_sink::PushResult::Queued)
            work.notify_one();
        return r;
    }

    void Run()
    {
        std::vector<log_sink::Line> batch;   // at most log_sink::PopChunk lines
        batch.reserve(log_sink::PopChunk);
        std::unique_lock<std::mutex> lock(mutex);
        while (true)
        {
            work.wait(lock, [this] { return fifo.Depth() != 0 || stop || abandon; });
            if (abandon || (stop && fifo.Depth() == 0))
                break;
            std::size_t bytes = 0;
            fifo.PopUpTo(batch, log_sink::PopChunk, bytes);
            lock.unlock();
            uint64 maxUs = 0;
            for (log_sink::Line& line : batch)
            {
                uint64 const startUs = tick_diag::NowUs();
                log_sink::WriteLine(line);
                maxUs = std::max<uint64>(maxUs, tick_diag::NowUs() - startUs);
            }
            std::size_t const written = batch.size();
            batch.clear();
            lock.lock();
            fifo.MarkWritten(written, bytes, maxUs);
            idle.notify_all();
        }
    }

    // Closes the queue and waits (bounded) until everything queued is written; then the writer ends
    // (drained) or ends after its current chunk (timed out, the rest stays unwritten).
    bool Drain(uint32 waitMs)
    {
        std::unique_lock<std::mutex> lock(mutex);
        fifo.Close();
        bool const drained = idle.wait_for(lock, std::chrono::milliseconds(waitMs), [this] { return fifo.Pending() == 0; });
        if (drained)
            stop = true;
        else
            abandon = true;
        work.notify_all();
        return drained;
    }

    // Waits (bounded) until the lines queued before the call are written. try_lock: a crash path must
    // not hang on a mutex another thread holds.
    bool Flush(uint32 waitMs)
    {
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        while (!lock.owns_lock())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lock.try_lock();
        }
        std::uint64_t const target = fifo.Accepted();
        idle.wait_until(lock, deadline, [this, target] { return fifo.Written() >= target || abandon; });
        return fifo.Written() >= target;
    }

    log_sink::Window TakeWindow()
    {
        std::lock_guard<std::mutex> guard(mutex);
        return fifo.TakeWindow();
    }

    log_sink::StopReport Report(char const* reason, bool drained, uint64 drainMs)
    {
        std::lock_guard<std::mutex> guard(mutex);
        log_sink::StopReport r;
        r.reason = reason;
        r.written = fifo.Written();
        r.dropped = fifo.DroppedTotal();
        r.unwritten = fifo.Pending();
        r.drained = drained;
        r.drainMs = drainMs;
        return r;
    }
};

// The running sink for the std::terminate handler (null when there is none).
static std::atomic<LogAsyncSinkState*> s_terminateSink{nullptr};

#if PLATFORM != PLATFORM_WINDOWS
static std::terminate_handler s_prevTerminate = nullptr;

// Installed when the sink starts: an uncaught exception (MANGOS_ASSERT throws) or the freeze detector
// must not lose the queued lines (the assertion and its backtrace). Then the previous handler.
static void LogSinkTerminate()
{
    if (LogAsyncSinkState* sink = s_terminateSink.load())
        sink->Flush(log_sink::CrashFlushWaitMs);
    if (s_prevTerminate)
        s_prevTerminate();
    std::abort();
}
#endif

static std::string SinkFormat(char const* fmt, va_list ap)
{
    std::string text;
    log_sink::AppendV(text, fmt, ap);
    return text;
}

static void SinkClock(std::string& out)
{
    time_t t = time(nullptr);
    log_sink::AppendClock(out, *localtime(&t));
}

static void SinkStamp(std::string& out)
{
    time_t t = time(nullptr);
    log_sink::AppendStamp(out, *localtime(&t));
}

void Log::SinkConsole(log_sink::Line& line, FILE* stream, int colorType, bool withTime, char const* prefix, std::string const& text)
{
    std::string* out = line.Add(stream);
    if (!out)
        return;
    bool const colored = m_colored && colorType >= 0 && colorType < LOG_TYPE_MAX;
    if (colored)
        log_sink::AppendAnsiColor(*out, int(m_colors[colorType]));
    if (withTime && m_includeTime)
        SinkClock(*out);
    out->append(prefix);
    out->append(text);
    if (colored)
        log_sink::AppendAnsiReset(*out);
    out->push_back('\n');
}

void Log::SinkFile(log_sink::Line& line, FILE* file, bool stamp, char const* prefix, std::string const& text, bool close)
{
    std::string* out = line.Add(file, close);
    if (!out)
        return;
    if (stamp)
        SinkStamp(*out);
    out->append(prefix);
    out->append(text);
    out->push_back('\n');
}

void Log::SinkSubmit(log_sink::Line& line, uint64 lwStartUs)
{
    if (!line.count)
        return;
    log_sink::PushResult const r = m_sink->Push(line);
    if (r == log_sink::PushResult::Closed)
        log_sink::WriteLine(line);    // the sink is stopping: same bytes, written here
    else if (r == log_sink::PushResult::Dropped)
        log_sink::CloseOwned(line);   // queue full: lost and counted ([LogSink] dropped=)
    if (lwStartUs)
        m_logWait.Record(tick_diag::NowUs() - lwStartUs, 0);
}

void Log::outString()
{
    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        log_sink::Line line;
        SinkConsole(line, stdout, -1, true, "", "");
        SinkFile(line, logfile, true, "", "");
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_includeTime)
        outTime(stdout);
    printf( "\n" );
    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile)
    {
        outTimestamp(logfile);
        fprintf(logfile, "\n" );
        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outString( const char * str, ... )
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stdout, LogNormal, true, "", text);
        SinkFile(line, logfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(true,m_colors[LogNormal]);

    if (m_includeTime)
        outTime(stdout);

    va_list ap;

    va_start(ap, str);
    vutf8printf(stdout, str, &ap);
    va_end(ap);

    if (m_colored)
        ResetColor(true);

    printf( "\n" );

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile)
    {
        outTimestamp(logfile);

        va_start(ap, str);
        vfprintf(logfile, str, ap);
        fprintf(logfile, "\n" );
        va_end(ap);

        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outInfo( const char * str, ...)
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };


    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stdout, -1, false, "", text);
        SinkFile(line, nostalriusLogFile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    va_list ap;
    va_start(ap, str);
    vutf8printf(stdout, str, &ap);
    va_end(ap);

    printf ("\n");
    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (nostalriusLogFile)
    {
        outTimestamp(nostalriusLogFile);

        va_start(ap, str);
        vfprintf(nostalriusLogFile, str, ap);
        fprintf(nostalriusLogFile, "\n");
        fflush(nostalriusLogFile);
        va_end(ap);

        fflush(nostalriusLogFile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);
    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outHonor(const char *str, ...)
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    // twow-repo#597 ([LogWait]), 0 = off or nothing written
    uint64 const lwStartUs = LogWaitStartIf(!HasLogFilter(LOG_FILTER_HONOR) || honorLogfile);
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        bool const toConsole = !HasLogFilter(LOG_FILTER_HONOR);
        if (!toConsole && !honorLogfile)
            return;
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        if (toConsole)
        {
            // As the synchronous path: the time goes to stdout, the text to stderr.
            if (m_includeTime)
                if (std::string* clock = line.Add(stdout))
                    SinkClock(*clock);
            SinkConsole(line, stderr, LogNormal, false, "", text);
        }
        SinkFile(line, honorLogfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (!HasLogFilter(LOG_FILTER_HONOR))
    {
        if (m_colored)
            SetColor(false, m_colors[LogNormal]);

        if (m_includeTime)
            outTime(stdout);

        va_list ap;

        va_start(ap, str);
        vutf8printf(stderr, str, &ap);
        va_end(ap);

        if (m_colored)
            ResetColor(false);

        fprintf(stderr, "\n");
        fflush(stderr);
    }

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (honorLogfile)
    {
        outTimestamp(honorLogfile);
        fprintf(honorLogfile, "%s", "");

        va_list ap;

        va_start(ap, str);
        vfprintf(honorLogfile, str, ap);
        va_end(ap);

        fprintf(honorLogfile, "\n" );
        fflush(honorLogfile);
    }
    LogWaitDone(lwStartUs, lwFileUs, LogWaitMark(lwStartUs));
}

void Log::outRaid(const char* str, ...)
{
    if (!str)
        return;

    // only logged to file
    if (raidLogFile)
    {
        std::shared_lock<std::shared_mutex> l{ logLock };

        uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
        if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
        {
            va_list sap;
            va_start(sap, str);
            std::string const text = SinkFormat(str, sap);
            va_end(sap);
            log_sink::Line line;
            SinkFile(line, raidLogFile, true, "", text);
            SinkSubmit(line, lwStartUs);
            return;
        }
        outTimestamp(raidLogFile);
        fprintf(raidLogFile, "%s", "");

        va_list ap;

        va_start(ap, str);
        vfprintf(raidLogFile, str, ap);
        va_end(ap);

        fprintf(raidLogFile, "\n");
        fflush(raidLogFile);
        LogWaitDone(lwStartUs, lwStartUs, LogWaitMark(lwStartUs));
    }
}

void Log::outError( const char * err, ... )
{
    if (!err)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread (queued, see Log.h)
    {
        va_list sap;
        va_start(sap, err);
        std::string const text = SinkFormat(err, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stderr, LogError, true, "", text);
        SinkFile(line, logfile, true, "ERROR:", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(false,m_colors[LogError]);

    if (m_includeTime)
        outTime(stderr);

    va_list ap;

    va_start(ap, err);
    vutf8printf(stderr, err, &ap);
    va_end(ap);

    if (m_colored)
        ResetColor(false);

    fprintf( stderr, "\n" );
    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile)
    {
        outTimestamp(logfile);
        fprintf(logfile, "ERROR:" );

        va_start(ap, err);
        vfprintf(logfile, err, ap);
        va_end(ap);

        fprintf(logfile, "\n" );
        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stderr);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outErrorDb()
{
    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        log_sink::Line line;
        SinkConsole(line, stderr, -1, true, "", "");
        SinkFile(line, logfile, true, "ERROR:", "");
        SinkFile(line, dberLogfile, true, "", "");
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_includeTime)
        outTime(stderr);

    fprintf( stderr, "\n" );

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile)
    {
        outTimestamp(logfile);
        fprintf(logfile, "ERROR:\n" );
        fflush(logfile);
    }

    if (dberLogfile)
    {
        outTimestamp(dberLogfile);
        fprintf(dberLogfile, "\n" );
        fflush(dberLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stderr);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outErrorDb( const char * err, ... )
{
    if (!err)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, err);
        std::string const text = SinkFormat(err, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stderr, LogError, true, "", text);
        SinkFile(line, logfile, true, "ERROR:", text);
        SinkFile(line, dberLogfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(false,m_colors[LogError]);

    if (m_includeTime)
        outTime(stderr);

    va_list ap;

    va_start(ap, err);
    vutf8printf(stderr, err, &ap);
    va_end(ap);

    if (m_colored)
        ResetColor(false);

    fprintf( stderr, "\n" );

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile)
    {
        outTimestamp(logfile);
        fprintf(logfile, "ERROR:" );

        va_start(ap, err);
        vfprintf(logfile, err, ap);
        va_end(ap);

        fprintf(logfile, "\n" );
        fflush(logfile);
    }

    if (dberLogfile)
    {
        outTimestamp(dberLogfile);

        va_list ap;
        va_start(ap, err);
        vfprintf(dberLogfile, err, ap);
        va_end(ap);

        fprintf(dberLogfile, "\n" );
        fflush(dberLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stderr);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outBasic( const char * str, ... )
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    // twow-repo#597 ([LogWait]), 0 = off or both levels filter the line out (nothing written)
    uint64 const lwStartUs = LogWaitStartIf(m_logLevel >= LOG_LVL_BASIC || (logfile && m_logFileLevel >= LOG_LVL_BASIC));
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        bool const toConsole = m_logLevel >= LOG_LVL_BASIC;
        bool const toFile = logfile && m_logFileLevel >= LOG_LVL_BASIC;
        if (!toConsole && !toFile)
            return;
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        if (toConsole)
            SinkConsole(line, stdout, LogDetails, true, "", text);
        if (toFile)
            SinkFile(line, logfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_logLevel >= LOG_LVL_BASIC)
    {
        if (m_colored)
            SetColor(true,m_colors[LogDetails]);

        if (m_includeTime)
            outTime(stdout);

        va_list ap;
        va_start(ap, str);
        vutf8printf(stdout, str, &ap);
        va_end(ap);

        if (m_colored)
            ResetColor(true);

        printf( "\n" );
    }

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile && m_logFileLevel >= LOG_LVL_BASIC)
    {
        va_list ap;
        outTimestamp(logfile);
        va_start(ap, str);
        vfprintf(logfile, str, ap);
        fprintf(logfile, "\n" );
        va_end(ap);
        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outDetail( const char * str, ... )
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    // twow-repo#597 ([LogWait]), 0 = off or both levels filter the line out (nothing written)
    uint64 const lwStartUs = LogWaitStartIf(m_logLevel >= LOG_LVL_DETAIL || (logfile && m_logFileLevel >= LOG_LVL_DETAIL));
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        bool const toConsole = m_logLevel >= LOG_LVL_DETAIL;
        bool const toFile = logfile && m_logFileLevel >= LOG_LVL_DETAIL;
        if (!toConsole && !toFile)
            return;
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        if (toConsole)
            SinkConsole(line, stdout, LogDetails, true, "", text);
        if (toFile)
            SinkFile(line, logfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_logLevel >= LOG_LVL_DETAIL)
    {

        if (m_colored)
            SetColor(true,m_colors[LogDetails]);

        if (m_includeTime)
            outTime(stdout);

        va_list ap;
        va_start(ap, str);
        vutf8printf(stdout, str, &ap);
        va_end(ap);

        if (m_colored)
            ResetColor(true);

        printf( "\n" );
    }

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile && m_logFileLevel >= LOG_LVL_DETAIL)
    {
        outTimestamp(logfile);

        va_list ap;
        va_start(ap, str);
        vfprintf(logfile, str, ap);
        va_end(ap);

        fprintf(logfile, "\n" );
        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outDebug( const char * str, ... )
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    // twow-repo#597 ([LogWait]), 0 = off or both levels filter the line out (nothing written)
    uint64 const lwStartUs = LogWaitStartIf(m_logLevel >= LOG_LVL_DEBUG || (logfile && m_logFileLevel >= LOG_LVL_DEBUG));
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        bool const toConsole = m_logLevel >= LOG_LVL_DEBUG;
        bool const toFile = logfile && m_logFileLevel >= LOG_LVL_DEBUG;
        if (!toConsole && !toFile)
            return;
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        if (toConsole)
            SinkConsole(line, stdout, LogDebug, true, "", text);
        if (toFile)
            SinkFile(line, logfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_logLevel >= LOG_LVL_DEBUG)
    {
        if (m_colored)
            SetColor(true,m_colors[LogDebug]);

        if (m_includeTime)
            outTime(stdout);

        va_list ap;
        va_start(ap, str);
        vutf8printf(stdout, str, &ap);
        va_end(ap);

        if (m_colored)
            ResetColor(true);

        printf( "\n" );
    }

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile && m_logFileLevel >= LOG_LVL_DEBUG)
    {
        outTimestamp(logfile);

        va_list ap;
        va_start(ap, str);
        vfprintf(logfile, str, ap);
        va_end(ap);

        fprintf(logfile, "\n" );
        fflush(logfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outWarden(const char *wrd, ...)
{
    if (!wrd)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, wrd);
        std::string const text = SinkFormat(wrd, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stdout, LogWarden, true, "[Warden] ", text);
        SinkFile(line, wardenLogfile, true, "[Warden] ", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(true, m_colors[LogWarden]);

    if (m_includeTime)
        outTime(stdout);

    // Append tag to console warden messages.
    printf("[Warden] ");

    va_list ap;
    va_start(ap, wrd);
    vutf8printf(stdout, wrd, &ap);
    va_end(ap);

    if (m_colored)
        ResetColor(true);

    printf("\n");

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (wardenLogfile)
    {
        outTimestamp(wardenLogfile);
        fprintf(wardenLogfile, "[Warden] ");

        va_list ap;
        va_start(ap, wrd);
        vfprintf(wardenLogfile, wrd, ap);
        va_end(ap);

        fprintf(wardenLogfile, "\n");
        fflush(wardenLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outWardenDebug(const char *wrd, ...)
{
    if (!m_wardenDebug)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    if (!wrd)
        return;

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, wrd);
        std::string const text = SinkFormat(wrd, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stdout, LogWarden, true, "[Warden] ", text);
        SinkFile(line, wardenLogfile, true, "[Warden] ", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(true, m_colors[LogWarden]);

    if (m_includeTime)
        outTime(stdout);

    // Append tag to console warden messages.
    printf("[Warden] ");

    va_list ap;
    va_start(ap, wrd);
    vutf8printf(stdout, wrd, &ap);
    va_end(ap);

    if (m_colored)
        ResetColor(true);

    printf("\n");

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (wardenLogfile)
    {
        outTimestamp(wardenLogfile);
        fprintf(wardenLogfile, "[Warden] ");

        va_list ap;
        va_start(ap, wrd);
        vfprintf(wardenLogfile, wrd, ap);
        va_end(ap);

        fprintf(wardenLogfile, "\n");
        fflush(wardenLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outAnticheat(const char* detector, const char* player, const char* reason, const char* penalty)
{
    if (!detector || !player || !reason || !penalty)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        std::string text;
        log_sink::AppendF(text, "[%s] Player %s, Cheat: %s, Penalty: %s", detector, player, reason, penalty);
        log_sink::Line line;
        SinkConsole(line, stdout, LogWarden, true, "", text);
        SinkFile(line, anticheatLogfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_colored)
        SetColor(true, m_colors[LogWarden]);

    if (m_includeTime)
        outTime(stdout);

    printf("[%s] Player %s, Cheat: %s, Penalty: %s", detector, player, reason, penalty);

    if (m_colored)
        ResetColor(true);

    printf("\n");

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (anticheatLogfile)
    {
        outTimestamp(anticheatLogfile);
        fprintf(anticheatLogfile, "[%s] Player %s, Cheat: %s, Penalty: %s", detector, player, reason, penalty);
        fprintf(anticheatLogfile, "\n");
        fflush(anticheatLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

//For application-level Discord bot logging.
void Log::outDiscord(char const* str, ...)
{
    if (!str)
        return;

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        SinkConsole(line, stdout, -1, true, "", text);
        if (discordLogFile)  // as the synchronous path: checks the Discord log, writes the server log
            SinkFile(line, logfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_includeTime)
        outTime(stdout);

    va_list ap;
    va_start(ap, str);
    vutf8printf(stdout, str, &ap);
    va_end(ap);

    printf("\n");

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (discordLogFile)
    {
        va_list ap;
        outTimestamp(logfile);
        va_start(ap, str);
        vfprintf(logfile, str, ap);
        fprintf(logfile, "\n");
        va_end(ap);
        fflush(logfile);
    }
    LogWaitDone(lwStartUs, lwFileUs, LogWaitMark(lwStartUs));
}


//For internal Discord hooks such as rate limits and bad gateways.
void Log::outDiscordCore(char const* str)
{
    if (!str)
        return;

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        log_sink::Line line;
        SinkConsole(line, stdout, -1, true, "", str);
        SinkFile(line, discordCoreLogFile, true, "", str);  // the text as is (not used as a format)
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_includeTime)
        outTime(stdout);

    puts(str);

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (discordCoreLogFile)
    {
        outTimestamp(discordCoreLogFile);
        fprintf(discordCoreLogFile, str);
        fprintf(discordCoreLogFile, "\n");
        fflush(discordCoreLogFile);
    }
    LogWaitDone(lwStartUs, lwFileUs, LogWaitMark(lwStartUs));
}

void Log::outSpam(const char* wrd, ...)
{
    if (!wrd)
        return;

    if (logFiles[LOG_CHAT_SPAM])
    {
        uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
        if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
        {
            va_list sap;
            va_start(sap, wrd);
            std::string const text = SinkFormat(wrd, sap);
            va_end(sap);
            log_sink::Line line;
            SinkFile(line, logFiles[LOG_CHAT_SPAM], true, "", text);
            SinkSubmit(line, lwStartUs);
            return;
        }
        outTimestamp(logFiles[LOG_CHAT_SPAM]);

        va_list ap;
        va_start(ap, wrd);
        vfprintf(logFiles[LOG_CHAT_SPAM], wrd, ap);
        va_end(ap);

        fprintf(logFiles[LOG_CHAT_SPAM], "\n");
        fflush(logFiles[LOG_CHAT_SPAM]);
        LogWaitDone(lwStartUs, lwStartUs, LogWaitMark(lwStartUs));
    }
}

void Log::outCommand( uint32 account, const char * str, ... )
{
    if (!str)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    // twow-repo#597 ([LogWait]), 0 = off or nothing written (levels filter it out, no GM log)
    uint64 const lwStartUs = LogWaitStartIf(m_logLevel >= LOG_LVL_DETAIL || (logfile && m_logFileLevel >= LOG_LVL_DETAIL) ||
        m_gmlog_per_account || gmLogfile);
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        bool const toConsole = m_logLevel >= LOG_LVL_DETAIL;
        bool const toFile = logfile && m_logFileLevel >= LOG_LVL_DETAIL;
        if (!toConsole && !toFile && !m_gmlog_per_account && !gmLogfile)
            return;
        va_list sap;
        va_start(sap, str);
        std::string const text = SinkFormat(str, sap);
        va_end(sap);
        log_sink::Line line;
        if (toConsole)
            SinkConsole(line, stdout, LogDetails, true, "", text);
        if (toFile)
            SinkFile(line, logfile, true, "", text);
        if (m_gmlog_per_account)
            SinkFile(line, openGmlogPerAccount(account), true, "", text, true);  // the writer closes it
        else
            SinkFile(line, gmLogfile, true, "", text);
        SinkSubmit(line, lwStartUs);
        return;
    }
    if (m_logLevel >= LOG_LVL_DETAIL)
    {
        if (m_colored)
            SetColor(true,m_colors[LogDetails]);

        if (m_includeTime)
            outTime(stdout);

        va_list ap;
        va_start(ap, str);
        vutf8printf(stdout, str, &ap);
        va_end(ap);

        if (m_colored)
            ResetColor(true);

        printf( "\n" );
    }

    uint64 const lwFileUs = LogWaitMark(lwStartUs);
    if (logfile && m_logFileLevel >= LOG_LVL_DETAIL)
    {
        va_list ap;
        outTimestamp(logfile);
        va_start(ap, str);
        vfprintf(logfile, str, ap);
        fprintf(logfile, "\n" );
        va_end(ap);
        fflush(logfile);
    }

    if (m_gmlog_per_account)
    {
        if (FILE* per_file = openGmlogPerAccount (account))
        {
            va_list ap;
            outTimestamp(per_file);
            va_start(ap, str);
            vfprintf(per_file, str, ap);
            fprintf(per_file, "\n" );
            va_end(ap);
            fclose(per_file);
        }
    }
    else if (gmLogfile)
    {
        va_list ap;
        outTimestamp(gmLogfile);
        va_start(ap, str);
        vfprintf(gmLogfile, str, ap);
        fprintf(gmLogfile, "\n" );
        va_end(ap);
        fflush(gmLogfile);
    }
    uint64 const lwFileEndUs = LogWaitMark(lwStartUs);

    fflush(stdout);
    LogWaitDone(lwStartUs, lwFileUs, lwFileEndUs);
}

void Log::outWorldPacketDump(ACE_HANDLE socketHandle, uint32 opcode,
                             char const* opcodeName, ByteBuffer const* packet,
                             bool incoming)
{
    if (!worldLogfile)
        return;

    std::shared_lock<std::shared_mutex> l{ logLock };

    uint64 const lwStartUs = LogWaitStart();  // twow-repo#597 ([LogWait]), 0 = off
    if (AsyncSinkOn())  // twow-repo#597 (Log.AsyncSink): same bytes, written by the sink thread
    {
        log_sink::Line line;
        if (std::string* dump = line.Add(worldLogfile))
        {
            SinkStamp(*dump);
            log_sink::AppendF(*dump, "\n%s:\nSOCKET: %p\nLENGTH: %zu\nOPCODE: %s (0x%.4X)\nDATA:\n",
                incoming ? "CLIENT" : "SERVER", socketHandle, packet->size(), opcodeName, opcode);
            size_t q = 0;
            while (q < packet->size())
            {
                for (size_t j = 0; j < 16 && q < packet->size(); ++j)
                    log_sink::AppendF(*dump, "%.2X ", (*packet)[q++]);
                dump->append("\n");
            }
            dump->append("\n\n");
        }
        SinkSubmit(line, lwStartUs);
        return;
    }
    outTimestamp(worldLogfile);

    fprintf(worldLogfile,
            "\n%s:\nSOCKET: %p\nLENGTH: %zu\nOPCODE: %s (0x%.4X)\nDATA:\n",
            incoming ? "CLIENT" : "SERVER", socketHandle, packet->size(),
            opcodeName, opcode);

    size_t p = 0;
    while (p < packet->size())
    {
        for (size_t j = 0; j < 16 && p < packet->size(); ++j)
            fprintf(worldLogfile, "%.2X ", (*packet)[p++]);

        fprintf(worldLogfile, "\n");
    }

    fprintf(worldLogfile, "\n\n");
    fflush(worldLogfile);
    LogWaitDone(lwStartUs, lwStartUs, LogWaitMark(lwStartUs));
}

// twow-repo#597 (TICK-01, PerformanceLog.LogWriteTrace): from World::LoadConfigSettings (also on a reload).
// A switch-on starts with fresh counters.
void Log::SetLogWriteTrace(bool on)
{
    if (on && !m_logWriteTrace.load(std::memory_order_relaxed))
        m_logWait.Take();
    m_logWriteTrace.store(on, std::memory_order_relaxed);
}

// twow-repo#597 (TICK-01): the [LogWait] minute line (world thread). The counters are taken first and
// no lock is held here, so writing this line through out() only counts into the next minute.
void Log::ReportLogWait(bool log)
{
    tick_diag::LogWaitSnapshot const s = m_logWait.Take();
    if (!log)
        return;
    out(LOG_PERFORMANCE, "[LogWait] lines=%llu stdout_max_us=%llu file_max_us=%llu over10ms=%llu over100ms=%llu",
        (unsigned long long)s.lines, (unsigned long long)s.stdoutMaxUs, (unsigned long long)s.fileMaxUs,
        (unsigned long long)s.over10ms, (unsigned long long)s.over100ms);
}

// twow-repo#597 (TICK-01, Log.AsyncSink): from World::LoadConfigSettings. Startup: 1 starts the sink
// (Log.AsyncSinkMaxLines only read then). Reload: 1 -> 0 drains and goes back to the synchronous path;
// 0 -> 1 needs a restart.
void Log::ApplyAsyncSinkConfig(bool reload)
{
    bool const on = sConfig.GetIntDefault("Log.AsyncSink", 0) != 0;
    if (!reload)
    {
        if (on && !m_sink)
            StartAsyncSink(log_sink::EffectiveMaxLines(sConfig.GetIntDefault("Log.AsyncSinkMaxLines", 0)));
        return;
    }
    if (!on && AsyncSinkOn())
        StopAsyncSink("reload");
    else if (on && !AsyncSinkOn())
        outString("[LogSink] Log.AsyncSink = 1 needs a restart, the log stays synchronous");
}

// Fails closed: any start problem leaves the synchronous path and says so once.
void Log::StartAsyncSink(std::size_t maxLines)
{
#if PLATFORM == PLATFORM_WINDOWS
    (void)maxLines;
    outString("[LogSink] Log.AsyncSink = 1 ignored: not supported on Windows (console colors and code page go through the console API), the log stays synchronous");
#else
    std::shared_ptr<LogAsyncSinkState> state = std::make_shared<LogAsyncSinkState>(maxLines);
    try
    {
        state->thread = std::thread([state]() { state->Run(); });
    }
    catch (std::exception const& e)
    {
        outError("[LogSink] Log.AsyncSink = 1 ignored: cannot start the writer thread (%s), the log stays synchronous", e.what());
        return;
    }
    // Still synchronous: this line comes before every queued one.
    outString("[LogSink] async sink on: max_lines=%u max_bytes=%u", uint32(maxLines), uint32(log_sink::MaxQueuedBytes));
    {
        // No caller is in a synchronous write while the mode switches.
        std::unique_lock<std::shared_mutex> l{ logLock };
        m_sink = state;
        s_terminateSink.store(state.get());
        s_prevTerminate = std::set_terminate(LogSinkTerminate);
        m_asyncSinkOn.store(true, std::memory_order_release);
    }
#endif
}

// Holds logLock exclusively while it drains: callers wait (at most log_sink::DrainWaitMs) and then
// write synchronously, after every queued line. The writer needs no logLock, so this cannot deadlock.
void Log::StopAsyncSink(char const* reason)
{
    if (!m_sink)
        return;
    log_sink::StopReport report;
    {
        std::unique_lock<std::shared_mutex> l{ logLock };
        if (!m_asyncSinkOn.load(std::memory_order_relaxed))
            return;
        m_asyncSinkOn.store(false, std::memory_order_release);
        s_terminateSink.store(nullptr);
        uint64 const startUs = tick_diag::NowUs();
        bool const drained = m_sink->Drain(log_sink::DrainWaitMs);
        if (drained)
        {
            if (m_sink->thread.joinable())
                m_sink->thread.join();
        }
        else
        {
            // The writer is blocked in a write (stalled console pipe or disk): leave it, it ends after
            // its current chunk; the state lives on in its shared_ptr.
            m_sink->thread.detach();
            m_sinkAbandoned = true;
        }
        report = m_sink->Report(reason, drained, (tick_diag::NowUs() - startUs) / 1000);
    }
    outString("%s", log_sink::FormatStopLine(report).c_str());
}

bool Log::FlushAsyncSink(uint32 waitMs)
{
    if (!AsyncSinkOn())
        return true;
    return m_sink->Flush(waitMs);
}

// World thread only (m_sinkMinuteStartUs). The counters are taken before the line is written, so the
// line itself counts into the next minute.
void Log::AsyncSinkMinute()
{
    if (!AsyncSinkOn())
        return;
    uint64 const nowUs = tick_diag::NowUs();
    if (!m_sinkMinuteStartUs)
    {
        m_sinkMinuteStartUs = nowUs;
        m_sink->TakeWindow();
        return;
    }
    if (nowUs - m_sinkMinuteStartUs < 60000000)
        return;
    m_sinkMinuteStartUs = nowUs;
    log_sink::Window const w = m_sink->TakeWindow();
    out(LOG_PERFORMANCE, "%s", log_sink::FormatMinuteLine(w).c_str());
}

void Log::WaitBeforeContinueIfNeed()
{
    int mode = sConfig.GetIntDefault("WaitAtStartupError",0);

    if (mode < 0)
    {
        printf("\nPress <Enter> for continue\n");

        std::string line;
        std::getline (std::cin, line);
    }
    else if (mode > 0)
    {
        printf("\nWait %d secs for continue.\n",mode);
        for(int i = 0; i < mode; ++i)
        {
            ACE_OS::sleep(1);
        }
    }
}
