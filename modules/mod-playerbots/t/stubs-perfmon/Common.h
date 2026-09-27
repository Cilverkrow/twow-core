// Test double for the core's Common.h, for the perfmon collection suite only.
//
// src/playerbot/PerformanceMonitor.h includes "Common.h", which in a real build
// is src/shared/Common.h and pulls in the whole shared library's worth of
// platform headers. The header under test needs exactly the fixed-width
// typedefs and the standard containers its own declarations name.
//
// There is no shadowing here: src/playerbot has no Common.h of its own, and the
// suite is not given src/shared on its include path, so this is the only
// candidate. If the header under test starts needing more of Common.h, this
// file stops compiling, which is the signal.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef std::uint8_t  uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;
typedef std::uint64_t uint64;
typedef std::int8_t   int8;
typedef std::int16_t  int16;
typedef std::int32_t  int32;
typedef std::int64_t  int64;
