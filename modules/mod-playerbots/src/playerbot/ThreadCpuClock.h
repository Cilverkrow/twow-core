#pragma once

#include <cstdint>

// twow-repo#627/#628 (s7r 26.10.2026): stall control for the world-thread managers. s7r saw single values of 7-10 ms in
// unrelated parts (setup, release, scan) while the host ran at 84.9 GB commit. Wall time minus this thread's CPU time over
// one manager update is the time the thread did not run: preemption, page faults that block, lock waits, file I/O. The
// minute lines report its maximum (stall_us_max), so a slow update can be told apart from slow code.
namespace ai::thread_cpu
{
// This thread's CPU time in us; 0 where the platform gives none (then stall_us_max reads as the wall time). Out of line:
// the platform headers (windows.h) stay in ThreadCpuClock.cpp.
std::uint64_t NowUs();

// The part of a wall interval the thread was not on a CPU; pure (CPU time above the wall time - clock granularity - is 0).
inline std::uint32_t StallUs(std::uint32_t wallUs, std::uint64_t cpuStartUs, std::uint64_t cpuEndUs)
{
    std::uint64_t const cpu = cpuEndUs >= cpuStartUs ? cpuEndUs - cpuStartUs : 0;
    return cpu >= wallUs ? 0u : std::uint32_t(wallUs - cpu);
}
}
