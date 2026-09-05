#include "CliInput.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <signal.h>
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

    if (failures != 0)
    {
        std::fprintf(stderr, "mangosd_cli_input_tests FAIL failures=%d\n", failures);
        return 1;
    }

    std::printf("mangosd_cli_input_tests PASS\n");
    return 0;
}
