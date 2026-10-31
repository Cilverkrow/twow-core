#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// twow-repo#601 cand4 lever (ii) (CLI-PATH 31.10.2026, OB-00 order 29.10.; evidence far-travel-stuck/levelguard/DESIGN.md):
// AiPlayerbot.RosterFarMove.LevelGuard defers a travel destination at the target choice (ChooseTravelTargetAction,
// PostActiveDeferralOf) when a creature spawn hostile to the bot's side stands within Radius yd (2D) of the chosen point and
// its template level_max is at least bot level + Margin. The main loop then takes another destination - the objective is
// reselected, not just the teleport declined. cand4r-B 12-16Z (+3 / 50 yd): 22.5 % of B's arrival deaths <= 60 s caught, the
// rule fires at 4.0 % of B's XP-kill sites. Spawn level, not killer level: the killer is not known at the choice.
// The spawns come from the world data once at startup (BuildGrid, only with the switch on); a query reads the cells around the
// point, no object grid and no DB. Pure (std only), so far_move_policy_tests and the source contract pin it without game headers.
namespace ai::level_guard
{
    // FACTION_MASK_PLAYER / _ALLIANCE / _HORDE (DBCEnums.h): a spawn is hostile to a bot when its faction template's
    // hostile mask has the player bit or the bot's side bit (FactionTemplateEntry::IsHostileTo without the enemy lists).
    constexpr uint32_t MaskPlayer = 1;
    constexpr uint32_t MaskAlliance = 2;
    constexpr uint32_t MaskHorde = 4;

    constexpr float DefaultRadius = 50.0f;
    constexpr uint32_t DefaultMargin = 3;
    constexpr float CellSize = 50.0f;
    constexpr float MapHalf = 17066.67f;      // world coordinates run from -MapHalf to +MapHalf
    constexpr uint32_t LogsPerChoice = 3;     // [FarMoveGuard] result=skip lines per target choice (the rest is counted)

    inline bool HostileTo(uint32_t hostileMask, uint32_t sideMask)
    {
        return (hostileMask & (MaskPlayer | sideMask)) != 0;
    }

    inline bool Fires(uint32_t spawnLevelMax, uint32_t botLevel, uint32_t margin)
    {
        return spawnLevelMax >= botLevel + margin;
    }

    // OB-00 31.10.: quest givers / takers are not guarded (a turn-in is progress; a deferred taker would count as "no route"
    // for #485 / #807). purposeBits = the destination purpose, exemptMask = QuestGiver | QuestTaker (TravelValues.h).
    inline bool ExemptPurpose(uint32_t purposeBits, uint32_t exemptMask) { return (purposeBits & exemptMask) != 0; }

    // Config clamps: an unusable value falls back to the default (the A/B kit then still runs the documented rule).
    inline float ClampRadius(float radius) { return (radius >= 10.0f && radius <= 100.0f) ? radius : DefaultRadius; }
    inline uint32_t ClampMargin(uint32_t margin) { return (margin >= 1 && margin <= 10) ? margin : DefaultMargin; }

    struct Spawn
    {
        float x = 0.0f;
        float y = 0.0f;
        uint32_t entry = 0;
        uint8_t levelMax = 0;
        uint8_t rank = 0;
        uint8_t hostileMask = 0;
    };

    // The answer for one point: the firing spawn with the highest level (nearest of equal ones); without a firing spawn the
    // hostile spawn with the highest level, for the log. hostile = hostile spawns within the radius.
    struct Hit
    {
        bool checked = false;   // the guard looked at this point (set by PostActiveDeferralOf, counted by its main loop)
        bool fired = false;
        uint32_t entry = 0;
        uint32_t level = 0;
        uint32_t rank = 0;
        float dist = 0.0f;
        uint32_t hostile = 0;
    };

    class Grid
    {
    public:
        void Add(uint32_t map, Spawn const& spawn)
        {
            cells_[Key(map, Cell(spawn.x), Cell(spawn.y))].push_back(spawn);
            ++count_;
        }

        uint32_t Count() const { return count_; }

        Hit Query(uint32_t map, float x, float y, float radius, uint32_t botLevel, uint32_t margin, uint32_t sideMask) const
        {
            Hit hit;
            int32_t const cx0 = Cell(x - radius), cx1 = Cell(x + radius);
            int32_t const cy0 = Cell(y - radius), cy1 = Cell(y + radius);
            for (int32_t cx = cx0; cx <= cx1; ++cx)
                for (int32_t cy = cy0; cy <= cy1; ++cy)
                {
                    auto const itr = cells_.find(Key(map, cx, cy));
                    if (itr == cells_.end())
                        continue;
                    for (Spawn const& s : itr->second)
                    {
                        if (!HostileTo(s.hostileMask, sideMask))
                            continue;
                        float const d = std::sqrt((s.x - x) * (s.x - x) + (s.y - y) * (s.y - y));
                        if (d > radius)
                            continue;
                        ++hit.hostile;
                        bool const fires = Fires(s.levelMax, botLevel, margin);
                        // a firing spawn beats a non-firing one; then the higher level; then the nearer one
                        bool const better = (fires && !hit.fired) ||
                            (fires == hit.fired && (s.levelMax > hit.level || (s.levelMax == hit.level && (hit.entry == 0 || d < hit.dist))));
                        if (better)
                        {
                            hit.fired = hit.fired || fires;
                            hit.entry = s.entry;
                            hit.level = s.levelMax;
                            hit.rank = s.rank;
                            hit.dist = d;
                        }
                    }
                }
            return hit;
        }

        static int32_t Cell(float v) { return int32_t(std::floor((v + MapHalf) / CellSize)); }

    private:
        static uint64_t Key(uint32_t map, int32_t cx, int32_t cy)
        {
            return (uint64_t(map) << 40) | (uint64_t(uint32_t(cx) & 0xFFFFF) << 20) | uint64_t(uint32_t(cy) & 0xFFFFF);
        }

        std::unordered_map<uint64_t, std::vector<Spawn>> cells_;
        uint32_t count_ = 0;
    };

    // [FarMove] minute line (with the switch on): guard_checked / guard_skip / guard_skip_b, read and reset there.
    struct Counters
    {
        std::atomic<uint32_t> checked{ 0 };
        std::atomic<uint32_t> skip{ 0 };
        std::atomic<uint32_t> skipB{ 0 };
    };

    // Defined in ChooseTravelTargetAction.cpp (game side): the shared grid, built once by BuildGrid (PlayerbotAIConfig::Initialize,
    // only with the switch on); Ready() is false until then and the guard does nothing.
    Counters& SharedCounters();
    Grid const& SharedGrid();
    bool Ready();
    void BuildGrid();
}
