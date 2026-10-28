#pragma once

#include <chrono>
#include <cstdint>

// twow-repo#627/#628 (s7r 26.10.2026): the core's own share of a world-thread manager's group change. The accept action and
// the managers' leave paths time the core opcode alone (HandleGroupAcceptOpcode / HandleGroupDisbandOpcode) and leave it
// here; the manager reads it right after the call on the same (world) thread for its minute line (accept_core_us_max,
// leave_core_us_max). Whether a core join or leave alone costs >= 2 ms decides the budget question to the owner.
namespace ai::group_op_timing
{
inline thread_local std::uint32_t lastAcceptCoreUs = 0;
inline thread_local std::uint32_t lastLeaveCoreUs = 0;

inline std::uint32_t UsSince(std::chrono::steady_clock::time_point start)
{
    return std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}
}
