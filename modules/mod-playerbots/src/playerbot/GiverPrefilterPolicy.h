#pragma once

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3 planner S5 F1 (twow-repo#726; F1-ENTWURF-v0.2, OB-60 pre-review, OB-10 OK 0c2f2727; AiPlayerbot.Planner.GiverPrefilter,
// default 0): the giver prefilter skips a giver whose distance can not be within the fetch radius, before TravelMgr computes it.
// The lower bound per map square of a giver comes from the original distance functions (WorldSquare.h SqDistanceLowerBound: the
// 2D box distance via sqOutDistance, 0 inside the box, the portal distance of sqMapTransDistances); this header only takes the
// minimum, adds and compares, so it can not differ from the original by an ulp (no distance formula, no multiplication here).
// A giver it skips is one the original drops too (lb <= the squared distance, sqrt monotone); every other giver runs the
// original checks unchanged, so the list and its order are the same. The header names no game type:
// t/giver_prefilter_policy_tests.cpp compiles it standalone.
namespace ai::giver_prefilter
{
// The key follows the shared cohort parity rule (CohortPolicy.h): 0 off (default), 1 all, 2 cohort B (odd guid), 3 cohort A.
// Any other value is 0 with one error line at load.
inline bool ValidMode(std::uint32_t value)
{
    return value <= 3;
}

// The lower bound of one destination: the minimum over its terms (map squares; FLT_MAX = none, as the original).
inline void TakeMin(float& acc, float term)
{
    if (term < acc)
        acc = term;
}

// One portal term of a map square on another map: the squared transfer distance of sqMapTransDistances plus the square's own
// distance bound to the portal point - the original adds the square's squared distance to the portal (>= that bound) in the
// same order (distance + x).
inline float PortalTerm(float transferSq, float squareToPortalSq)
{
    return transferSq + squareToPortalSq;
}

// True when a destination with this lower bound can not pass "maxDistance > 0 && DistanceTo > maxDistance" (TravelMgr.cpp).
// DistanceTo is the sqrt of the squared distance on a float; float sqrt is correctly rounded (so is double sqrt rounded to
// float), hence monotone and the same value for the same input: lb <= sq gives sqrt(lb) <= sqrt(sq), and a skipped giver fails
// the original test too. FLT_MAX (no reachable square) is skipped, as the original range test drops it.
inline bool Excludes(float lowerBoundSq, float maxDistance)
{
    return maxDistance > 0 && float(std::sqrt(lowerBoundSq)) > maxDistance;
}

// The per-fetch counts of the prefilter (TravelMgr.h: the prefilter switch points at them for one planner giver fetch; a non-null
// pointer is the switch): pre = givers skipped, xmap = givers with a map square on another map, inbox = givers whose map square
// on the bot's map holds the bot in its box. Counted after the start-zone check.
struct Counts
{
    std::uint32_t pre = 0;
    std::uint32_t xmap = 0;
    std::uint32_t inbox = 0;
};

// The [PlanPrefilter] verify line (AiPlayerbot.Planner.GiverPrefilterVerify = N: every N-th prefiltered giver fetch also runs the
// original list and compares the full vectors). first_idx = first index that differs, -1 when equal (length difference: the
// shorter length).
constexpr std::size_t kVerifyBuf = 256;

inline std::size_t FormatVerifyLine(char* buf, std::size_t n, std::uint32_t guid, std::uint32_t nOrig, std::uint32_t nPre,
    long long firstIdx, Counts const& c)
{
    if (!buf || n == 0)
        return 0;
    int const w = std::snprintf(buf, n, "[PlanPrefilter] verify bot=%u n_orig=%u n_pre=%u mismatch=%u first_idx=%lld pre=%u "
        "xmap=%u inbox=%u", guid, nOrig, nPre, firstIdx >= 0 ? 1u : 0u, firstIdx, c.pre, c.xmap, c.inbox);
    if (w < 0)
        return 0;
    return std::size_t(w) < n ? std::size_t(w) : n - 1;
}

// The first index at which two lists differ, or -1 when they are equal (same length, same element per index).
template <class List>
long long FirstDiff(List const& a, List const& b)
{
    std::size_t const m = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < m; ++i)
        if (a[i] != b[i])
            return (long long)i;
    return a.size() == b.size() ? -1 : (long long)m;
}
}
