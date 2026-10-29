#include "playerbot/ThreadCpuClock.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace ai::thread_cpu
{
std::uint64_t NowUs()
{
#if defined(_WIN32)
    FILETIME creation, exitTime, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exitTime, &kernel, &user))
        return 0;
    std::uint64_t const k = (std::uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
    std::uint64_t const u = (std::uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    return (k + u) / 10;   // 100 ns units
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return 0;
    return std::uint64_t(ts.tv_sec) * 1000000u + std::uint64_t(ts.tv_nsec) / 1000u;
#else
    return 0;
#endif
}
}
