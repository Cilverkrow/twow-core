// twow-repo#597 (TICK-01, Log.AsyncSink): pure parts of the asynchronous log sink - the bounded FIFO
// (ring bound, drop counting, FIFO order, drain accounting, minute window), the line writer (order across
// targets, per-line files closed, dropped lines close their files), the formatting helpers (the same
// bytes as the synchronous Log paths) and the [LogSink] lines. No server needed (src/shared/LogSinkPolicy.h).
#include "../src/shared/LogSinkPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using log_sink::BoundedFifo;
using log_sink::PushResult;

void EffectiveMaxLinesTest()
{
    Require(log_sink::EffectiveMaxLines(0) == 50000, "0 = built-in 50000");
    Require(log_sink::EffectiveMaxLines(-5) == 50000, "negative = built-in");
    Require(log_sink::EffectiveMaxLines(3) == 16, "clamped up to 16");
    Require(log_sink::EffectiveMaxLines(20000) == 20000, "taken as is");
    Require(log_sink::EffectiveMaxLines(5000000) == 1000000, "clamped down to 1000000");
}

void FifoOrderAndBoundTest()
{
    BoundedFifo<int> q(100, 1u << 20);
    Require(q.Capacity() == 0, "nothing allocated before the first line");
    for (int i = 0; i < 100; ++i)
    {
        int v = i;
        Require(q.TryPush(v, 10) == PushResult::Queued, "100 lines fit");
    }
    Require(q.Depth() == 100 && q.Capacity() == 100, "ring grown exactly to the bound, never above");
    int extra = 100;
    Require(q.TryPush(extra, 10) == PushResult::Dropped, "line 101 dropped");
    Require(extra == 100, "a dropped item stays with the caller");
    Require(q.DroppedTotal() == 1 && q.Accepted() == 100, "drop counted, not accepted");

    std::vector<int> out;
    Require(q.PopAll(out) == 100 && q.Depth() == 0 && q.Bytes() == 0, "pop takes everything");
    bool ordered = true;
    for (int i = 0; i < 100; ++i)
        ordered = ordered && out[i] == i;
    Require(ordered, "FIFO order");
    Require(q.Pending() == 100, "popped but not written is still pending");
    q.MarkWritten(out.size(), 7);
    Require(q.Pending() == 0 && q.Written() == 100, "written");

    // Many rounds of push and pop: the order holds and the ring stays at its bound.
    BoundedFifo<int> w(16, 1u << 20);
    std::vector<int> got;
    int next = 0;
    for (int round = 0; round < 5; ++round)
    {
        for (int i = 0; i < 11; ++i)
        {
            int v = next++;
            Require(w.TryPush(v, 1) == PushResult::Queued, "rounds: queued");
        }
        std::vector<int> part;
        w.PopAll(part);
        got.insert(got.end(), part.begin(), part.end());
        w.MarkWritten(part.size(), 0);
    }
    bool wrapOrdered = got.size() == 55;
    for (std::size_t i = 0; wrapOrdered && i < got.size(); ++i)
        wrapOrdered = got[i] == int(i);
    Require(wrapOrdered && w.Capacity() == 16, "order across many rounds, ring stays at its bound");
}

void ByteBoundTest()
{
    BoundedFifo<int> q(1000, 100);
    int a = 1, b = 2, c = 3, d = 4;
    Require(q.TryPush(a, 60) == PushResult::Queued, "60 of 100 bytes");
    Require(q.TryPush(b, 50) == PushResult::Dropped, "110 bytes: dropped");
    Require(q.TryPush(c, 40) == PushResult::Queued, "exactly 100 bytes: queued");
    Require(q.Bytes() == 100 && q.DroppedTotal() == 1, "byte bound counted");
    Require(q.TryPush(d, 101) == PushResult::Dropped, "a single line above the byte bound is dropped");
    std::vector<int> out;
    q.PopAll(out);
    Require(q.Bytes() == 0 && q.TryPush(b, 50) == PushResult::Queued, "bytes freed by the pop");
}

void CloseAndDrainTest()
{
    BoundedFifo<int> q(10, 1000);
    for (int i = 0; i < 3; ++i)
    {
        int v = i;
        q.TryPush(v, 1);
    }
    q.Close();
    int late = 9;
    Require(q.TryPush(late, 1) == PushResult::Closed && late == 9, "closed: the caller writes the line itself");
    Require(q.DroppedTotal() == 0, "a closed push is not a drop");
    Require(q.Pending() == 3, "the drain waits for 3 lines");
    std::vector<int> out;
    q.PopAll(out);
    q.MarkWritten(2, 0);
    Require(q.Pending() == 1, "one still unwritten: lost if the drain times out now");
    q.MarkWritten(1, 0);
    Require(q.Pending() == 0, "drained");
}

void WindowTest()
{
    BoundedFifo<int> q(4, 1000);
    for (int i = 0; i < 6; ++i)
    {
        int v = i;
        q.TryPush(v, 1);
    }
    std::vector<int> out;
    q.PopAll(out);
    q.MarkWritten(3, 120);
    q.MarkWritten(1, 80);
    log_sink::Window w = q.TakeWindow();
    Require(w.lines == 4 && w.dropped == 2 && w.maxDepth == 4 && w.writeMaxUs == 120, "minute window");
    int v = 7;
    q.TryPush(v, 1);
    q.TryPush(v, 1);
    log_sink::Window const reset = q.TakeWindow();
    Require(reset.lines == 0 && reset.dropped == 0 && reset.writeMaxUs == 0 && reset.maxDepth == 2, "window reset");
    log_sink::Window const stalled = q.TakeWindow();
    Require(stalled.maxDepth == 2, "a stalled queue shows its depth in the next window");
    Require(log_sink::FormatMinuteLine(w) == "[LogSink] lines=4 dropped=2 max_depth=4 write_max_us=120", "[LogSink] line");
}

void StopLineTest()
{
    log_sink::StopReport r;
    r.reason = "shutdown";
    r.written = 12345;
    r.dropped = 6;
    r.unwritten = 0;
    r.drained = true;
    r.drainMs = 3;
    Require(log_sink::FormatStopLine(r) == "[LogSink] stopped reason=shutdown written=12345 dropped=6 unwritten=0 drained=1 drain_ms=3",
        "[LogSink] stopped line");
}

void FormatTest()
{
    std::string s;
    log_sink::AppendF(s, "a=%d b=%s c=%05.1f", 7, "x", 3.25);
    Require(s == "a=7 b=x c=003.2" || s == "a=7 b=x c=003.3", "printf formatting");

    std::string big;
    std::string const longArg(5000, 'q');
    log_sink::AppendF(big, "<%s>", longArg.c_str());
    Require(big.size() == 5002 && big.front() == '<' && big.back() == '>', "lines longer than the stack buffer");

    std::string nul;
    log_sink::AppendF(nul, "x%cy", 0);
    Require(nul.size() == 3 && nul[1] == '\0', "embedded NUL kept like vfprintf");

    std::tm t = {};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 6;
    t.tm_hour = 7;
    t.tm_min = 8;
    t.tm_sec = 9;
    std::string clock;
    log_sink::AppendClock(clock, t);
    Require(clock == "07:08:09 ", "Log::outTime format");
    std::string stamp;
    log_sink::AppendStamp(stamp, t);
    Require(stamp == "2026-10-06 07:08:09 ", "Log::outTimestamp format");

    std::string color;
    log_sink::AppendAnsiColor(color, 1);  // RED
    log_sink::AppendAnsiColor(color, 7);  // GREY
    log_sink::AppendAnsiColor(color, 8);  // YELLOW, bold
    log_sink::AppendAnsiColor(color, 13); // LCYAN, bold
    log_sink::AppendAnsiColor(color, 15); // out of range: nothing
    log_sink::AppendAnsiReset(color);
    Require(color == "\x1b[31m\x1b[37m\x1b[38;1m\x1b[36;1m\x1b[0m", "Log::SetColor / ResetColor bytes");
}

std::string ReadAll(std::FILE* f)
{
    std::string text;
    std::rewind(f);
    char buf[256];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    return text;
}

void WriteLineTest()
{
    std::FILE* a = std::tmpfile();
    std::FILE* b = std::tmpfile();
    std::FILE* owned = std::tmpfile();
    if (!a || !b || !owned)
    {
        std::cout << "tmpfile() unavailable, line writer test skipped\n";
        return;
    }

    // Two lines over two targets, then one line whose second target is a per-line file.
    BoundedFifo<log_sink::Line> q(8, 1u << 20);
    for (int i = 0; i < 2; ++i)
    {
        log_sink::Line line;
        *line.Add(a) = "a" + std::to_string(i) + "\n";
        *line.Add(b) = "b" + std::to_string(i) + "\n";
        Require(line.Add(nullptr) == nullptr && line.count == 2, "an unset target adds nothing");
        Require(q.TryPush(line, line.Bytes()) == PushResult::Queued, "line queued");
    }
    std::vector<log_sink::Line> batch;
    q.PopAll(batch);
    for (log_sink::Line& line : batch)
        log_sink::WriteLine(line);
    Require(ReadAll(a) == "a0\na1\n" && ReadAll(b) == "b0\nb1\n", "targets written in queue order");

    log_sink::Line full;
    Require(full.Add(a) && full.Add(b) && full.Add(a) && !full.Add(b), "at most three targets");

    // A dropped line closes the per-line file it owns (no descriptor leak).
    BoundedFifo<log_sink::Line> one(1, 1u << 20);
    log_sink::Line first;
    *first.Add(a) = "x\n";
    Require(one.TryPush(first, first.Bytes()) == PushResult::Queued, "first fits");
    log_sink::Line second;
    *second.Add(owned, true) = "lost\n";
    Require(one.TryPush(second, second.Bytes()) == PushResult::Dropped, "second dropped");
    Require(second.count == 1 && second.seg[0].file == owned, "the dropped line is still the caller's");
    log_sink::CloseOwned(second);
    Require(second.seg[0].file == nullptr, "its per-line file closed");

    std::fclose(a);
    std::fclose(b);
}
}

int main()
{
    EffectiveMaxLinesTest();
    FifoOrderAndBoundTest();
    ByteBoundTest();
    CloseAndDrainTest();
    WindowTest();
    StopLineTest();
    FormatTest();
    WriteLineTest();
    std::cout << "log_sink_597_test passed\n";
    return 0;
}
