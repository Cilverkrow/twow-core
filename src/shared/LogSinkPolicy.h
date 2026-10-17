#pragma once

// twow-repo#597 (TICK-01, Refs #592 #541, Log.AsyncSink): pure parts of the asynchronous log sink.
// With Log.AsyncSink = 1 the writing paths of Log format every line exactly as the synchronous path
// writes it (same bytes, timestamps taken at the call), put the finished bytes for all targets of the
// line (console stream and log files) into ONE bounded FIFO, and one writer thread does the
// fwrite/fflush. One queue for every target keeps the relative order of all lines across console and
// files in call order. Queue full: the line is dropped and counted, the caller never blocks.
//
// Header-only and free of server types (std only) so t/log_sink_597_test.cpp can check it without a
// server. Not thread-safe by itself: Log.cpp holds its own short lock around every queue call.

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace log_sink
{
// Built-in queue bound (Log.AsyncSinkMaxLines = 0) and the range a configured value is clamped to.
static constexpr std::size_t DefaultMaxLines = 50000;
static constexpr std::size_t MinMaxLines = 16;
static constexpr std::size_t MaxMaxLines = 1000000;
// Fixed byte bound on top of the line bound (a packet dump is one large line).
static constexpr std::size_t MaxQueuedBytes = 64u * 1024u * 1024u;
// Bounded waits: drain at shutdown / reload to 0, flush before a crash ends the process.
static constexpr std::uint32_t DrainWaitMs = 5000;
static constexpr std::uint32_t CrashFlushWaitMs = 2000;
// Short wait of the Discord log read, which holds logLock exclusively (every logging thread waits).
static constexpr std::uint32_t DiscordReadFlushWaitMs = 100;
// Lines the writer takes per turn of the sink lock (bounded time under the lock after a stall).
static constexpr std::size_t PopChunk = 256;

// Log.AsyncSinkMaxLines: 0 or less = built-in bound, else clamped to [MinMaxLines, MaxMaxLines].
inline std::size_t EffectiveMaxLines(std::int64_t configured)
{
    if (configured <= 0)
        return DefaultMaxLines;
    std::int64_t const lo = std::int64_t(MinMaxLines);
    std::int64_t const hi = std::int64_t(MaxMaxLines);
    return std::size_t(std::min(std::max(configured, lo), hi));
}

// ---- One line: the finished bytes per target ----

// One target of a line. close = per-account GM log: the writer closes the file after the write (the
// synchronous path opens and closes it per line too).
struct Segment
{
    std::FILE* file = nullptr;
    std::string bytes;
    bool close = false;
};

// At most three targets, in the order the synchronous path writes them (outErrorDb: stderr, server
// log, DB error log; outCommand: console, server log, GM log; outHonor: stdout time, stderr, honor log).
struct Line
{
    static constexpr std::size_t MaxSegments = 3;
    std::array<Segment, MaxSegments> seg;
    std::size_t count = 0;

    // A target that is not configured (null file) adds nothing: nullptr.
    std::string* Add(std::FILE* file, bool close = false)
    {
        if (!file || count >= MaxSegments)
            return nullptr;
        Segment& s = seg[count++];
        s.file = file;
        s.bytes.clear();
        s.close = close;
        return &s.bytes;
    }

    std::size_t Bytes() const
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < count; ++i)
            n += seg[i].bytes.size();
        return n;
    }
};

// Writes a line like the synchronous path: per target fwrite, then fflush (fclose for a per-line file).
// Used by the writer thread, and by a caller that finds the sink already closed.
inline void WriteLine(Line& line)
{
    for (std::size_t i = 0; i < line.count; ++i)
    {
        Segment& s = line.seg[i];
        if (!s.file)
            continue;
        if (!s.bytes.empty())
            std::fwrite(s.bytes.data(), 1, s.bytes.size(), s.file);
        if (s.close)
            std::fclose(s.file);
        else
            std::fflush(s.file);
        s.file = nullptr;
    }
}

// A dropped line: closes the per-line files it owns (nothing is written).
inline void CloseOwned(Line& line)
{
    for (std::size_t i = 0; i < line.count; ++i)
    {
        Segment& s = line.seg[i];
        if (s.file && s.close)
            std::fclose(s.file);
        s.file = nullptr;
    }
}

// ---- Formatting: the same bytes as the synchronous path ----

// printf into out, the same formatting as vfprintf (an embedded NUL from %c is kept).
inline void AppendV(std::string& out, char const* fmt, va_list ap)
{
    char buf[1024];
    va_list copy;
    va_copy(copy, ap);
    int const n = std::vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (n <= 0)
        return;
    if (std::size_t(n) < sizeof(buf))
    {
        out.append(buf, std::size_t(n));
        return;
    }
    std::size_t const at = out.size();
    out.resize(at + std::size_t(n) + 1);
    std::vsnprintf(&out[at], std::size_t(n) + 1, fmt, ap);
    out.resize(at + std::size_t(n));
}

inline void AppendF(std::string& out, char const* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    AppendV(out, fmt, ap);
    va_end(ap);
}

// "HH:MM:SS " as Log::outTime writes it (console, LogTime = 1).
inline void AppendClock(std::string& out, std::tm const& t)
{
    AppendF(out, "%02d:%02d:%02d ", t.tm_hour, t.tm_min, t.tm_sec);
}

// "YYYY-MM-DD HH:MM:SS " as Log::outTimestamp writes it (log files).
inline void AppendStamp(std::string& out, std::tm const& t)
{
    AppendF(out, "%-4d-%02d-%02d %02d:%02d:%02d ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

// The bytes Log::SetColor / Log::ResetColor write on a non-Windows console (index = Color of Log.h:
// BLACK .. WHITE; YELLOW (8) and up are bold).
inline void AppendAnsiColor(std::string& out, int color)
{
    static int const fg[15] = { 30, 31, 32, 33, 34, 35, 36, 37, 38, 31, 32, 34, 35, 36, 37 };
    if (color < 0 || color >= 15)
        return;
    AppendF(out, "\x1b[%d%sm", fg[color], color >= 8 ? ";1" : "");
}

inline void AppendAnsiReset(std::string& out)
{
    out.append("\x1b[0m");
}

// ---- The bounded FIFO ----

enum class PushResult
{
    Queued,
    Dropped,  // full (lines or bytes): not queued, counted
    Closed    // the sink stops: the caller writes the line itself
};

// The [LogSink] minute window. lines = lines the writer wrote, max_depth = deepest backlog (lines queued
// or being written; the window starts at the backlog it found), write_max_us = slowest line of the writer
// (all its targets).
struct Window
{
    std::uint64_t lines = 0;
    std::uint64_t dropped = 0;
    std::uint64_t maxDepth = 0;
    std::uint64_t writeMaxUs = 0;
};

// Ring of queued items (grown on demand, never above maxLines). The bounds cover every item not written
// yet: queued plus the chunk the writer has popped and is still writing (Pending(), PendingBytes()). So
// at most maxLines items / maxBytes payload are ever held, and that is also the most a crash can lose.
template <typename T>
class BoundedFifo
{
public:
    BoundedFifo(std::size_t maxLines, std::size_t maxBytes)
        : m_maxLines(std::max<std::size_t>(maxLines, 1)), m_maxBytes(maxBytes) {}

    // Queued: item moved in. Dropped / Closed: item untouched (the caller still owns it).
    PushResult TryPush(T& item, std::size_t bytes)
    {
        if (m_closed)
            return PushResult::Closed;
        if (Pending() >= m_maxLines || bytes > m_maxBytes - m_pendingBytes)
        {
            ++m_window.dropped;
            ++m_droppedTotal;
            return PushResult::Dropped;
        }
        if (m_count == m_ring.size())
            Grow();
        Slot& s = m_ring[(m_head + m_count) % m_ring.size()];
        s.item = std::move(item);
        s.bytes = bytes;
        ++m_count;
        m_bytes += bytes;
        m_pendingBytes += bytes;
        ++m_accepted;
        m_window.maxDepth = std::max<std::uint64_t>(m_window.maxDepth, Pending());
        return PushResult::Queued;
    }

    // Moves at most maxItems of the oldest queued items into out (cleared first), oldest first; bytes =
    // their payload, handed back to MarkWritten. The work per call is bounded by maxItems: the writer
    // holds the sink lock for it, and every logging caller waits on that lock.
    std::size_t PopUpTo(std::vector<T>& out, std::size_t maxItems, std::size_t& bytes)
    {
        out.clear();
        bytes = 0;
        std::size_t const n = std::min(m_count, maxItems);
        for (std::size_t i = 0; i < n; ++i)
        {
            Slot& s = m_ring[m_head];
            out.push_back(std::move(s.item));
            s.item = T();
            bytes += s.bytes;
            s.bytes = 0;
            m_head = (m_head + 1) % m_ring.size();
        }
        m_count -= n;
        m_bytes -= bytes;
        if (m_count == 0)
            m_head = 0;
        return n;
    }

    // The writer finished n popped items of bytes payload; maxUs = its slowest one. Only now do they
    // leave the bound.
    void MarkWritten(std::size_t n, std::size_t bytes, std::uint64_t maxUs)
    {
        m_written += n;
        m_pendingBytes -= std::min(bytes, m_pendingBytes);
        m_window.lines += n;
        m_window.writeMaxUs = std::max(m_window.writeMaxUs, maxUs);
    }

    // Reads and resets the minute window.
    Window TakeWindow()
    {
        Window const w = m_window;
        m_window = Window();
        m_window.maxDepth = Pending();
        return w;
    }

    void Close() { m_closed = true; }
    bool Closed() const { return m_closed; }
    // Queued, not popped yet.
    std::size_t Depth() const { return m_count; }
    std::size_t Bytes() const { return m_bytes; }
    std::size_t MaxLines() const { return m_maxLines; }
    std::size_t Capacity() const { return m_ring.size(); }
    std::uint64_t Accepted() const { return m_accepted; }
    std::uint64_t Written() const { return m_written; }
    std::uint64_t DroppedTotal() const { return m_droppedTotal; }
    // Queued or popped but not written yet (what the bound covers, what a drain waits for, what a crash
    // loses).
    std::uint64_t Pending() const { return m_accepted - m_written; }
    std::size_t PendingBytes() const { return m_pendingBytes; }

private:
    struct Slot
    {
        T item;
        std::size_t bytes = 0;
    };

    // Doubles the ring (at most up to the bound): O(depth) under the sink lock, but only about log2(bound / 16)
    // times over the life of the sink (the ring never shrinks).
    void Grow()
    {
        std::size_t const size = std::min(std::max<std::size_t>(m_ring.size() * 2, 16), m_maxLines);
        std::vector<Slot> ring(size);
        for (std::size_t i = 0; i < m_count; ++i)
            ring[i] = std::move(m_ring[(m_head + i) % m_ring.size()]);
        m_ring.swap(ring);
        m_head = 0;
    }

    std::size_t m_maxLines;
    std::size_t m_maxBytes;
    std::vector<Slot> m_ring;
    std::size_t m_head = 0;
    std::size_t m_count = 0;
    std::size_t m_bytes = 0;          // queued
    std::size_t m_pendingBytes = 0;   // queued + popped, not written yet (the byte bound)
    bool m_closed = false;
    std::uint64_t m_accepted = 0;
    std::uint64_t m_written = 0;
    std::uint64_t m_droppedTotal = 0;
    Window m_window;
};

// ---- Lines of the sink itself ----

// Once a minute (world thread, performance log).
inline std::string FormatMinuteLine(Window const& w)
{
    std::string line;
    AppendF(line, "[LogSink] lines=%llu dropped=%llu max_depth=%llu write_max_us=%llu",
        (unsigned long long)w.lines, (unsigned long long)w.dropped, (unsigned long long)w.maxDepth,
        (unsigned long long)w.writeMaxUs);
    return line;
}

// Once when the sink stops (shutdown, reload to 0, exit). unwritten = lines still queued when the
// bounded drain gave up (lost unless the blocked writer gets them out later).
struct StopReport
{
    char const* reason = "";
    std::uint64_t written = 0;
    std::uint64_t dropped = 0;
    std::uint64_t unwritten = 0;
    bool drained = false;
    std::uint64_t drainMs = 0;
};

inline std::string FormatStopLine(StopReport const& r)
{
    std::string line;
    AppendF(line, "[LogSink] stopped reason=%s written=%llu dropped=%llu unwritten=%llu drained=%u drain_ms=%llu",
        r.reason, (unsigned long long)r.written, (unsigned long long)r.dropped, (unsigned long long)r.unwritten,
        r.drained ? 1u : 0u, (unsigned long long)r.drainMs);
    return line;
}
} // namespace log_sink
