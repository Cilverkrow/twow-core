#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ai::formation_grid
{
// twow-repo#389 formations v2. Train 5 raid video: shapes built from outlines
// at the raid follow distance (5 yd) spread 27 bots over ~70 yd, and the one-
// ring circle collapsed into a blob. Here every shape is a *filled* grid of
// slots with its own spacing, capped to a maximum radius, so up to 40 bots
// stay orderly and compact. Slot 0 is the most prominent position (closest to
// the leader / front); the formation fills slots in role order.

struct Offset
{
    float forward; // along the leader's facing; negative is behind
    float side;    // to the leader's left (positive) or right (negative)
};

enum class Shape
{
    CIRCLE,      // full circle, leader in the middle, concentric rings
    REARGUARD,   // half ring behind the leader, rows of arcs ("Nachhut")
    VANGUARD,    // half ring in front of the leader ("Vorhut")
    WEDGE,       // filled V, leader at the tip
    TRIANGLE,    // filled triangle behind the leader, widest row first
    BLOCK,       // rows behind the leader
    COLUMN,      // 2 abreast (3 from 21 bots) behind the leader
    // #389 follow-up (owner 2026-09-28): the old outline formations rebuilt
    // on the grid, same names.
    LINE,        // a row beside the leader, more rows behind (10-11 wide)
    SHIELD,      // tanks in a row in front, everybody else in rows behind
    ARROW,       // role wedge: tanks at the tip in front, healers at the back
    DRAGONSLAYER,// raid: tanks 10 yd ahead, melee arcs in front, healer and
                 // ranged arcs behind (owner 2026-09-28)
};

constexpr float Pi = 3.14159265358979f;

inline float Distance(Offset const& o)
{
    return std::sqrt(o.forward * o.forward + o.side * o.side);
}

// Evenly spaced positions on [from, to] (centred when there is one).
inline std::vector<float> Spread(unsigned int count, float from, float to)
{
    std::vector<float> out;
    if (count == 0)
        return out;
    if (count == 1)
    {
        out.push_back((from + to) / 2.0f);
        return out;
    }
    for (unsigned int i = 0; i < count; ++i)
        out.push_back(from + (to - from) * float(i) / float(count - 1));
    return out;
}

// Centre-out order within a row: middle first, then alternating outwards.
inline std::vector<float> CentreOut(std::vector<float> row)
{
    std::sort(row.begin(), row.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); });
    return row;
}

inline std::vector<Offset> Circle(unsigned int count, float spacing)
{
    std::vector<Offset> out;
    for (unsigned int ring = 1; out.size() < count; ++ring)
    {
        float const radius = spacing * float(ring);
        unsigned int const capacity = std::max(1u, unsigned(2.0f * Pi * radius / spacing));
        unsigned int const here = std::min(capacity, count - unsigned(out.size()));
        // Start at the front and alternate left/right so a partial ring stays
        // balanced around the leader.
        for (unsigned int i = 0; i < here; ++i)
        {
            float const step = 2.0f * Pi / float(here);
            float const k = float((i + 1) / 2) * ((i % 2) ? 1.0f : -1.0f);
            float const angle = k * step + (ring % 2 ? 0.0f : step / 2.0f);
            out.push_back({ radius * std::cos(angle), radius * std::sin(angle) });
        }
    }
    return out;
}

inline std::vector<Offset> HalfRing(unsigned int count, float spacing, bool front)
{
    std::vector<Offset> out;
    for (unsigned int row = 2; out.size() < count; ++row)
    {
        float const radius = spacing * float(row);
        unsigned int const capacity = std::max(1u, unsigned(Pi * radius / spacing) + 1);
        unsigned int const here = std::min(capacity, count - unsigned(out.size()));
        // Centre first: straight behind (180 degrees) or straight ahead (0).
        for (float a : CentreOut(Spread(here, -Pi / 2.0f, Pi / 2.0f)))
        {
            float const angle = (front ? 0.0f : Pi) + a;
            out.push_back({ radius * std::cos(angle), radius * std::sin(angle) });
        }
    }
    return out;
}

inline std::vector<Offset> Wedge(unsigned int count, float spacing)
{
    std::vector<Offset> out;
    for (unsigned int row = 1; out.size() < count; ++row)
    {
        unsigned int const here = std::min(row + 1, count - unsigned(out.size()));
        float const half = spacing * float(row) * 0.7f;
        for (float s : CentreOut(Spread(here, -half, half)))
            out.push_back({ -spacing * float(row), s });
    }
    return out;
}

inline std::vector<Offset> Triangle(unsigned int count, float spacing)
{
    unsigned int rows = 1;
    while (rows * (rows + 1) / 2 < count)
        ++rows;
    std::vector<Offset> out;
    for (unsigned int row = 1; row <= rows && out.size() < count; ++row)
    {
        unsigned int const width = rows - row + 1;
        unsigned int const here = std::min(width, count - unsigned(out.size()));
        float const half = spacing * float(width - 1) / 2.0f;
        for (float s : CentreOut(Spread(here, -half, half)))
            out.push_back({ -spacing * float(row), s });
    }
    return out;
}

inline std::vector<Offset> Rows(unsigned int count, float spacing, unsigned int width)
{
    std::vector<Offset> out;
    for (unsigned int row = 1; out.size() < count; ++row)
    {
        unsigned int const here = std::min(width, count - unsigned(out.size()));
        float const half = spacing * float(width - 1) / 2.0f;
        std::vector<float> row_pos = Spread(width, -half, half);
        row_pos = CentreOut(row_pos);
        row_pos.resize(here);
        for (float s : row_pos)
            out.push_back({ -spacing * float(row), s });
    }
    return out;
}

inline unsigned int BlockWidth(unsigned int count)
{
    unsigned int width = 1;
    while (width * width < count)
        ++width;
    return std::min(8u, std::max(3u, width));
}

inline unsigned int ColumnWidth(unsigned int count)
{
    // Owner 2026-09-27 (#389): 4 abreast from 30 bots keeps 40 at ~10 rows.
    return count >= 30 ? 4u : count > 20 ? 3u : 2u;
}

constexpr unsigned int LineWidth = 10;   // bots per side-by-side row
constexpr unsigned int ShieldWidth = 8;  // tanks per front row

// Line: the first row is level with the leader, 5 bots on each side; the
// next rows are behind, 11 wide (the gap is only needed beside the leader).
inline std::vector<Offset> Line(unsigned int count, float spacing)
{
    std::vector<Offset> out;
    for (unsigned int i = 1; out.size() < count && i <= LineWidth / 2; ++i)
    {
        out.push_back({ 0.0f, spacing * float(i) });
        if (out.size() < count)
            out.push_back({ 0.0f, -spacing * float(i) });
    }
    for (unsigned int row = 1; out.size() < count; ++row)
    {
        unsigned int const here = std::min(LineWidth + 1, count - unsigned(out.size()));
        float const half = spacing * float(LineWidth) / 2.0f;
        std::vector<float> positions = CentreOut(Spread(LineWidth + 1, -half, half));
        positions.resize(here);
        for (float s : positions)
            out.push_back({ -spacing * float(row), s });
    }
    return out;
}

// Shield: the first `tanks` slots form rows in front of the leader, the rest
// stand in block rows behind.
inline std::vector<Offset> Shield(unsigned int count, unsigned int tanks, float spacing)
{
    std::vector<Offset> out;
    tanks = std::min(tanks, count);
    for (unsigned int row = 0; out.size() < tanks; ++row)
    {
        unsigned int const here = std::min(ShieldWidth, tanks - unsigned(out.size()));
        float const half = spacing * float(here - 1) / 2.0f;
        for (float s : CentreOut(Spread(here, -half, half)))
            out.push_back({ spacing * (1.5f + float(row)), s });
    }
    unsigned int const rest = count - tanks;
    if (rest)
        for (Offset const& o : Rows(rest, spacing, BlockWidth(rest)))
            out.push_back(o);
    return out;
}

// Arrow: the wedge in role order (tanks, melee, ranged, healers), moved
// forward so the rows holding the tanks are in front of the leader and the
// leader stands inside the arrow.
inline std::vector<Offset> Arrow(unsigned int count, unsigned int tanks, float spacing)
{
    if (count == 0)
        return {};
    // One slot more than needed: the one that lands on the leader is dropped.
    std::vector<Offset> out = Wedge(count + 1, spacing);
    float deepestTank = spacing;
    for (unsigned int i = 0; i < tanks && i < count; ++i)
        deepestTank = std::max(deepestTank, -out[i].forward);
    float const shift = deepestTank + spacing;
    for (Offset& o : out)
        o.forward += shift;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (Distance(out[i]) < 1.0f)
        {
            out.erase(out.begin() + i);
            break;
        }
    }
    out.resize(count);
    return out;
}

// Owner 2026-09-27 (#389): around the leader at most 15 yd, other shapes may
// extend up to 30 yd in total.
inline float MaxExtentFor(Shape shape, float circleMax, float otherMax)
{
    return shape == Shape::CIRCLE ? circleMax : otherMax;
}

// Group members per role, in the order the formation fills its slots.
struct RoleCounts
{
    unsigned int tanks = 0, melee = 0, ranged = 0, heals = 0;
    unsigned int Total() const { return tanks + melee + ranged + heals; }
};

constexpr float ArcHalfAngle = 5.0f * Pi / 12.0f;  // 75 degrees each side

// `count` slots on half-circle rows ahead of (`front`) or behind the leader,
// first row at firstRadius, one spacing further per row. One row while the
// bots fit at full spacing, else two; more only when two rows would put
// neighbours closer than 3/4 spacing. Rows span +-75 degrees so the front
// and back arcs never meet beside the leader.
inline std::vector<Offset> Arcs(unsigned int count, float firstRadius, float spacing, bool front)
{
    std::vector<Offset> out;
    if (count == 0)
        return out;
    auto capacity = [&](float radius, float gap) { return unsigned(2.0f * ArcHalfAngle * radius / gap) + 1; };

    unsigned int rows = count > capacity(firstRadius, spacing) ? 2 : 1;
    for (;;)
    {
        unsigned int total = 0;
        for (unsigned int i = 0; i < rows; ++i)
            total += capacity(firstRadius + spacing * float(i), 0.75f * spacing);
        if (total >= count)
            break;
        ++rows;
    }

    // Share the bots among the rows in proportion to their length.
    float radiusSum = 0.0f;
    for (unsigned int i = 0; i < rows; ++i)
        radiusSum += firstRadius + spacing * float(i);
    std::vector<unsigned int> perRow(rows, 0);
    unsigned int left = count;
    for (unsigned int i = 0; i < rows; ++i)
    {
        float const radius = firstRadius + spacing * float(i);
        unsigned int here = i + 1 == rows ? left
            : std::min(left, unsigned(std::floor(float(count) * radius / radiusSum + 0.5f)));
        here = std::min(here, capacity(radius, 0.75f * spacing));
        perRow[i] = here;
        left -= here;
    }
    for (unsigned int i = 0; left && i < rows; ++i)
    {
        unsigned int const room = capacity(firstRadius + spacing * float(i), 0.75f * spacing) - perRow[i];
        unsigned int const add = std::min(room, left);
        perRow[i] += add;
        left -= add;
    }

    for (unsigned int i = 0; i < rows; ++i)
    {
        float const radius = firstRadius + spacing * float(i);
        for (float a : CentreOut(Spread(perRow[i], -ArcHalfAngle, ArcHalfAngle)))
        {
            float const forward = radius * std::cos(a);
            out.push_back({ front ? forward : -forward, radius * std::sin(a) });
        }
    }
    return out;
}

constexpr unsigned int DragonslayerTankRow = 5;

// Dragonslayer (owner 2026-09-28): tanks in rows of up to five 10 yd ahead
// (more tanks line up behind them), melee on one or two half-circle rows in
// front of the leader, healers on one or two rows behind him, ranged on one
// or two rows behind the healers. Slots come in role order: tanks, melee,
// ranged, healers.
inline std::vector<Offset> Dragonslayer(RoleCounts const& roles, float spacing, float tankDistance = 10.0f)
{
    std::vector<Offset> tanks;
    for (unsigned int row = 0; tanks.size() < roles.tanks; ++row)
    {
        unsigned int const here = std::min(DragonslayerTankRow, roles.tanks - unsigned(tanks.size()));
        float const half = spacing * float(here - 1) / 2.0f;
        for (float s : CentreOut(Spread(here, -half, half)))
            tanks.push_back({ tankDistance - spacing * float(row), s });
    }

    float const firstRow = 1.5f * spacing;
    std::vector<Offset> const melee = Arcs(roles.melee, firstRow, spacing, true);
    std::vector<Offset> const heals = Arcs(roles.heals, firstRow, spacing, false);
    float healDepth = 0.0f;
    for (Offset const& o : heals)
        healDepth = std::max(healDepth, Distance(o));
    float const rangedFirst = heals.empty() ? firstRow : healDepth + firstRow;
    std::vector<Offset> const ranged = Arcs(roles.ranged, rangedFirst, spacing, false);

    std::vector<Offset> out = tanks;
    out.insert(out.end(), melee.begin(), melee.end());
    out.insert(out.end(), ranged.begin(), ranged.end());
    out.insert(out.end(), heals.begin(), heals.end());
    return out;
}

inline void Cap(std::vector<Offset>& out, float maxRadius)
{
    float far = 0.0f;
    for (Offset const& o : out)
        far = std::max(far, Distance(o));
    if (maxRadius > 0.0f && far > maxRadius)
    {
        float const scale = maxRadius / far;
        for (Offset& o : out)
        {
            o.forward *= scale;
            o.side *= scale;
        }
    }
}

// All slots for `count` followers, compressed so that no slot lies further
// than maxRadius from the leader (0 = no cap). `leadCount` is the number of
// tanks at the start of the role order (SHIELD, ARROW; DRAGONSLAYER treats
// the rest as melee here - use SlotsForRoles for the real split).
inline std::vector<Offset> Slots(Shape shape, unsigned int count, float spacing, float maxRadius, unsigned int leadCount = 0)
{
    std::vector<Offset> out;
    switch (shape)
    {
        case Shape::DRAGONSLAYER:
        {
            RoleCounts roles;
            roles.tanks = std::min(leadCount, count);
            roles.melee = count - roles.tanks;
            out = Dragonslayer(roles, spacing);
            break;
        }
        case Shape::CIRCLE:    out = Circle(count, spacing); break;
        case Shape::REARGUARD: out = HalfRing(count, spacing, false); break;
        case Shape::VANGUARD:  out = HalfRing(count, spacing, true); break;
        case Shape::WEDGE:     out = Wedge(count, spacing); break;
        case Shape::TRIANGLE:  out = Triangle(count, spacing); break;
        case Shape::BLOCK:     out = Rows(count, spacing, BlockWidth(count)); break;
        case Shape::COLUMN:    out = Rows(count, spacing, ColumnWidth(count)); break;
        case Shape::LINE:      out = Line(count, spacing); break;
        case Shape::SHIELD:    out = Shield(count, leadCount, spacing); break;
        case Shape::ARROW:     out = Arrow(count, leadCount, spacing); break;
    }
    Cap(out, maxRadius);
    return out;
}

// Slots for a group split by role (the order GridFormation fills them).
inline std::vector<Offset> SlotsForRoles(Shape shape, RoleCounts const& roles, float spacing, float maxRadius)
{
    if (shape != Shape::DRAGONSLAYER)
        return Slots(shape, roles.Total(), spacing, maxRadius, roles.tanks);
    std::vector<Offset> out = Dragonslayer(roles, spacing);
    Cap(out, maxRadius);
    return out;
}
}
