#include "CliInput.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int failures = 0;

void check(bool condition, const char* description)
{
    if (condition)
        std::printf("PASS %s\n", description);
    else
    {
        std::fprintf(stderr, "FAIL %s\n", description);
        ++failures;
    }
}

struct PipeInput
{
    PipeInput()
    {
        int descriptors[2];
        if (pipe(descriptors) != 0)
        {
            std::perror("pipe");
            std::exit(2);
        }
        readDescriptor = descriptors[0];
        writeDescriptor = descriptors[1];
        stream = fdopen(readDescriptor, "r");
        if (!stream)
        {
            std::perror("fdopen");
            std::exit(2);
        }
    }

    ~PipeInput()
    {
        if (stream)
            fclose(stream);
        if (writeDescriptor >= 0)
            close(writeDescriptor);
    }

    void Write(const char* text)
    {
        const std::size_t length = std::strlen(text);
        if (write(writeDescriptor, text, length) != static_cast<ssize_t>(length))
        {
            std::perror("write");
            std::exit(2);
        }
    }

    int readDescriptor = -1;
    int writeDescriptor = -1;
    FILE* stream = nullptr;
};

std::string readLine(CliInput& input, CliInputResult& result)
{
    char buffer[256] = {};
    result = input.ReadLine(buffer, sizeof(buffer), 500);
    if (result != CliInputResult::Line)
        return {};
    buffer[std::strcspn(buffer, "\r\n")] = '\0';
    return buffer;
}

void testSingleCommand()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("saveall\n");

    CliInputResult result;
    check(readLine(input, result) == "saveall", "single FIFO command");
}

void testTwoLinesOneWrite()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("saveall\nserver shutdown 0\n");

    CliInputResult first;
    CliInputResult second;
    check(readLine(input, first) == "saveall", "first line from one write");
    check(readLine(input, second) == "server shutdown 0", "second line from one write without a wake command");
}

void testTwoImmediateWrites()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("saveall\n");
    pipeInput.Write("server shutdown 0\n");

    CliInputResult first;
    CliInputResult second;
    check(readLine(input, first) == "saveall", "first immediate write");
    check(readLine(input, second) == "server shutdown 0", "second immediate write without a wake command");
}

void testFragmentedLine()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("server shut");

    const pid_t writer = fork();
    if (writer == 0)
    {
        usleep(20000);
        const char suffix[] = "down 0\n";
        _exit(write(pipeInput.writeDescriptor, suffix, sizeof(suffix) - 1) ==
                      static_cast<ssize_t>(sizeof(suffix) - 1) ? 0 : 1);
    }

    CliInputResult result;
    check(readLine(input, result) == "server shutdown 0", "complete line split across writes");

    int status = 0;
    check(waitpid(writer, &status, 0) == writer && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "fragment writer exit status");
}

void testTimeoutBlocks()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    char buffer[256] = {};

    const auto start = std::chrono::steady_clock::now();
    const CliInputResult result = input.ReadLine(buffer, sizeof(buffer), 100);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    check(result == CliInputResult::Timeout, "idle FIFO returns timeout");
    check(elapsed >= 80, "idle FIFO blocks instead of busy-waiting");
}

void signalHandler(int)
{
}

void testInterruptedSelect()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);

    struct sigaction action{};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, nullptr);

    const pid_t signaler = fork();
    if (signaler == 0)
    {
        usleep(20000);
        _exit(kill(getppid(), SIGUSR1) == 0 ? 0 : 1);
    }

    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 500) == CliInputResult::Interrupted,
          "interrupted select is retryable");

    int status = 0;
    check(waitpid(signaler, &status, 0) == signaler && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "signaler exit status");

    pipeInput.Write("saveall\n");
    CliInputResult result;
    check(readLine(input, result) == "saveall", "input continues after interrupted select");
}

void testEndOfFile()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    close(pipeInput.writeDescriptor);
    pipeInput.writeDescriptor = -1;

    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 100) == CliInputResult::EndOfFile,
          "closed FIFO reports EOF");
}

void timeoutHandler(int)
{
    _exit(124);
}

void testOpenFragment()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("server shut");
    char buffer[256] = {};
    for (int i = 0; i != 3; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        check(input.ReadLine(buffer, sizeof(buffer), 30) == CliInputResult::Timeout,
              "open fragment returns timeout without dispatch");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(elapsed >= std::chrono::milliseconds(20) && elapsed < std::chrono::milliseconds(500),
              "fragment wait is bounded and not busy-waiting");
    }
    pipeInput.Write("down 0\n");
    CliInputResult result;
    check(readLine(input, result) == "server shutdown 0", "fragment survives multiple timeouts");
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout,
          "completed fragment is delivered exactly once");
}

volatile sig_atomic_t stopRequested = 0;
void requestStop(int) { stopRequested = 1; }

void testStopDuringFragment()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("incomplete");
    stopRequested = 0;
    struct sigaction action{};
    action.sa_handler = requestStop;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, nullptr);
    const pid_t signaler = fork();
    if (signaler == 0)
    {
        usleep(20000);
        _exit(kill(getppid(), SIGUSR2) == 0 ? 0 : 1);
    }
    char buffer[256] = {};
    const auto start = std::chrono::steady_clock::now();
    while (!stopRequested)
    {
        const auto result = input.ReadLine(buffer, sizeof(buffer), 40);
        check(result == CliInputResult::Timeout || result == CliInputResult::Interrupted,
              "stop loop never dispatches a partial command");
    }
    check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500),
          "caller regains control for stop while writer remains open");
    int status = 0;
    check(waitpid(signaler, &status, 0) == signaler && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "stop signaler joined");
}

void testLineBoundaries()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    CliInputResult result;
    for (std::size_t length : {std::size_t(254), std::size_t(255)})
    {
        const std::string line(length, 'a');
        pipeInput.Write((line + "\n").c_str());
        check(readLine(input, result) == line, "LF payload at accepted boundary");
        pipeInput.Write((line + "\r\n").c_str());
        check(readLine(input, result) == line, "CRLF payload at accepted boundary");
    }
    pipeInput.Write("\n\r\nlast\r\n");
    check(readLine(input, result).empty() && result == CliInputResult::Line, "empty LF line");
    check(readLine(input, result).empty() && result == CliInputResult::Line, "empty CRLF line");
    check(readLine(input, result) == "last", "CRLF normalized");
}

void testOversizedLine()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    // A command-looking suffix belongs to the rejected line, not to the next one.
    const std::string oversized = std::string(255, 'x') + "server shutdown 0\n";
    pipeInput.Write((oversized + std::string(2048, 'x') + "\nsaveall\n").c_str());
    CliInputResult result;
    check(readLine(input, result) == "saveall", "reject whole oversized lines and command-like suffixes");
    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout,
          "no suffix or duplicate remains after oversized rejection");
    pipeInput.Write(std::string(256, 'x').c_str());
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout,
          "oversized open fragment times out while discarding");
    pipeInput.Write("server shutdown 0\nsaveall\n");
    check(readLine(input, result) == "saveall", "discard state survives timeout until newline");
}

void testInterruptedFragment()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("server shut");
    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout,
          "fragment consumed before signal");
    struct sigaction action{};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, nullptr);
    const pid_t signaler = fork();
    if (signaler == 0)
    {
        usleep(20000);
        _exit(kill(getppid(), SIGUSR1) == 0 ? 0 : 1);
    }
    check(input.ReadLine(buffer, sizeof(buffer), 500) == CliInputResult::Interrupted,
          "EINTR after consumed fragment is explicit");
    int status = 0;
    check(waitpid(signaler, &status, 0) == signaler && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "fragment signaler joined");
    pipeInput.Write("down 0\n");
    CliInputResult result;
    check(readLine(input, result) == "server shutdown 0", "EINTR preserves all previously consumed bytes");
}

void testPartialEof()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write("saveall\nserver shutdown 0");
    close(pipeInput.writeDescriptor);
    pipeInput.writeDescriptor = -1;
    CliInputResult result;
    check(readLine(input, result) == "saveall", "complete line before EOF delivered");
    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 100) == CliInputResult::EndOfFile,
          "unterminated EOF tail discarded rather than executed");
    check(input.ReadLine(buffer, sizeof(buffer), 100) == CliInputResult::EndOfFile,
          "EOF remains terminal");
}

void testInvalidLinesAndArguments()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    const char bytes[] = "invalid\0server shutdown 0\ninvalid\rserver shutdown 0\nsaveall\n";
    check(write(pipeInput.writeDescriptor, bytes, sizeof(bytes) - 1) == static_cast<ssize_t>(sizeof(bytes) - 1),
          "write embedded control-byte cases");
    CliInputResult result;
    check(readLine(input, result) == "saveall", "embedded NUL/CR reject the entire logical line");
    char buffer[256] = {};
    check(input.ReadLine(nullptr, sizeof(buffer), 20) == CliInputResult::Error, "null output rejected");
    check(input.ReadLine(buffer, 255, 20) == CliInputResult::Error, "undersized output rejected");
    check(input.ReadLine(buffer, sizeof(buffer), 0) == CliInputResult::Timeout, "zero budget returns immediately");
    CliInput invalid(nullptr);
    check(invalid.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Error, "null input rejected");
}

void testDescriptorLifetime()
{
    PipeInput pipeInput;
    const int flags = fcntl(pipeInput.readDescriptor, F_GETFL);
    {
        CliInput input(pipeInput.stream);
        check((fcntl(pipeInput.readDescriptor, F_GETFL) & O_NONBLOCK) != 0, "reads are actually nonblocking");
    }
    check(fcntl(pipeInput.readDescriptor, F_GETFL) == flags, "original descriptor flags restored");
    CliInput input(pipeInput.stream);
    fclose(pipeInput.stream);
    pipeInput.stream = nullptr;
    pipeInput.Write("unfinished");
    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout,
          "closing original stdin cannot invalidate the owned reader");
}

void testSplitCrLf()
{
    PipeInput pipeInput;
    CliInput input(pipeInput.stream);
    pipeInput.Write((std::string(255, 'x') + "\r").c_str());
    char buffer[256] = {};
    check(input.ReadLine(buffer, sizeof(buffer), 20) == CliInputResult::Timeout, "boundary CR survives timeout");
    pipeInput.Write("\nnext\n");
    CliInputResult result;
    check(readLine(input, result) == std::string(255, 'x'), "split boundary CRLF is accepted once");
    check(readLine(input, result) == "next", "valid line after split CRLF preserved");
}

void runIsolated(void (*test)(), const char* name)
{
    // Bound every scenario, including intentionally failing pre-fix reproductions.
    const pid_t child = fork();
    if (child == 0)
    {
        failures = 0;
        signal(SIGALRM, timeoutHandler);
        alarm(3);
        test();
        _exit(failures ? 1 : 0);
    }
    int status = 0;
    check(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          name);
}

void testChildExitAndNoDuplicate()
{
    int descriptors[2];
    if (pipe(descriptors) != 0)
    {
        std::perror("pipe");
        std::exit(2);
    }

    const pid_t child = fork();
    if (child == 0)
    {
        close(descriptors[1]);
        FILE* stream = fdopen(descriptors[0], "r");
        if (!stream)
            _exit(120);
        signal(SIGALRM, timeoutHandler);
        alarm(2);

        CliInput input(stream);
        CliInputResult result;
        if (readLine(input, result) != "saveall")
            _exit(121);
        if (readLine(input, result) != "server shutdown 0")
            _exit(122);

        char buffer[256] = {};
        if (input.ReadLine(buffer, sizeof(buffer), 50) != CliInputResult::Timeout)
            _exit(123);

        alarm(0);
        fclose(stream);
        _exit(0);
    }

    close(descriptors[0]);
    const char commands[] = "saveall\nserver shutdown 0\n";
    check(write(descriptors[1], commands, sizeof(commands) - 1) ==
              static_cast<ssize_t>(sizeof(commands) - 1),
          "parent writes shutdown command pair");

    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "child consumes each command once and exits zero without a third command");
    close(descriptors[1]);
}
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    testSingleCommand();
    testTwoLinesOneWrite();
    testTwoImmediateWrites();
    testFragmentedLine();
    testTimeoutBlocks();
    testInterruptedSelect();
    testEndOfFile();
    testChildExitAndNoDuplicate();
    runIsolated(testOpenFragment, "open-fragment scenario completes within outer deadline");
    runIsolated(testStopDuringFragment, "stop-during-fragment scenario completes within outer deadline");
    runIsolated(testLineBoundaries, "line-boundary scenario");
    runIsolated(testOversizedLine, "oversized-line scenario");
    runIsolated(testInterruptedFragment, "interrupted-fragment scenario");
    runIsolated(testPartialEof, "partial-EOF scenario");
    runIsolated(testInvalidLinesAndArguments, "invalid-input scenario");
    runIsolated(testDescriptorLifetime, "descriptor-lifetime scenario");
    runIsolated(testSplitCrLf, "split-CRLF scenario");

    if (failures != 0)
    {
        std::fprintf(stderr, "mangosd_cli_input_tests FAIL failures=%d\n", failures);
        return 1;
    }

    std::printf("mangosd_cli_input_tests PASS\n");
    return 0;
}
