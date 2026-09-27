// Test double for src/playerbot/playerbot.h, for the perfmon collection suite.
//
// The real header includes Spell.h, World.h, ObjectMgr.h, Chat.h and a dozen
// more, i.e. the entire game library, and then redefines `sLog` to
// BotLog::Instance(). PerformanceMonitor.cpp needs three things out of all of
// that: `sLog`, ChatHandler (it defines ChatHandler::HandlePerfMonCommand), and
// the standard library.
//
// So this file supplies a capturing `sLog` -- which is what makes the suite able
// to assert on the report the monitor prints, rather than only on its return
// value -- plus a ChatHandler with the one method the command handler calls.
//
// The relocation trick that makes this reachable is in tests.cmake: a
// quote-include is searched in the directory of the file containing the
// directive first, so as long as PerformanceMonitor.cpp sits next to the real
// playerbot.h, no -I ordering can win. The suite therefore compiles a
// configure-time copy of the real .cpp placed in the build tree, where the only
// "playerbot.h" on offer is this one.

#pragma once

#include "Common.h"

// The real playerbot.h includes PlayerbotAI.h, and PerformanceMonitor.h relies on
// that: it declares the start(metric, name, PlayerbotAI*) overload without
// forward-declaring the type. Mirror it, or the relocated PerformanceMonitor.cpp
// (which includes playerbot.h and PerformanceMonitor.h before PlayerbotAI.h)
// does not compile.
#include "PlayerbotAI.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <mutex>
#include <sstream>

// ---------------------------------------------------------------------------
// Capturing logger. The real sLog in a bot translation unit is
// BotLog::Instance(); the only method PerformanceMonitor.cpp calls on it is
// outString, in both its no-argument and printf forms.
// ---------------------------------------------------------------------------
class PerfMonTestLog
{
public:
    static PerfMonTestLog& Instance()
    {
        static PerfMonTestLog instance;
        return instance;
    }

    void outString() { m_lines.emplace_back(); }

    void outString(char const* fmt, ...)
    {
        char buffer[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);
        m_lines.emplace_back(buffer);
    }

    void Clear() { m_lines.clear(); }

    std::vector<std::string> const& Lines() const { return m_lines; }

    std::string Text() const
    {
        std::string out;
        for (auto const& line : m_lines)
        {
            out += line;
            out += '\n';
        }
        return out;
    }

private:
    std::vector<std::string> m_lines;
};

#define sLog PerfMonTestLog::Instance()

// ---------------------------------------------------------------------------
// ChatHandler. PerformanceMonitor.cpp defines ChatHandler::HandlePerfMonCommand
// out of line, so the class has to declare it; the handler itself calls
// SendSysMessage, which the double records so the suite can assert on what the
// operator would have been told.
// ---------------------------------------------------------------------------
class ChatHandler
{
public:
    bool HandlePerfMonCommand(char* args);

    void SendSysMessage(char const* message) { m_messages.emplace_back(message); }

    std::vector<std::string> const& Messages() const { return m_messages; }
    void ClearMessages() { m_messages.clear(); }

private:
    std::vector<std::string> m_messages;
};
