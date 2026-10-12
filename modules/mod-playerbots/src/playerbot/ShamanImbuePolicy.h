#pragma once

// twow-repo#541: shaman weapon imbue per spec (owner 09.10.2026 on OB-50's research, relayed by OB-00;
// switch AiPlayerbot.Shaman.ImbueBySpec, default 0, "nach Abnahme an").
//
//   Spec          1-9        10-19        20-29                                30-60
//   Tank          Rockbiter  Rockbiter    Rockbiter                            Rockbiter
//   Enhancement   Rockbiter  Flametongue  Frostbrand solo / Flametongue group  Windfury
//   Elemental     Rockbiter  Flametongue  Flametongue                          Flametongue
//   Restoration   Rockbiter  Flametongue  Flametongue                          Flametongue
//
// The level columns are the trainer levels of the spells (Rockbiter 1, Flametongue 10, Frostbrand 20,
// Windfury 30), so the choice follows the spells the bot has learned rather than its level: a bot that has
// not trained a spell yet keeps the one before it. Shamans have no dual wield on Turtle (OB-50), so there
// is one choice for the main hand. Turtle has no Earthliving Weapon.

#include <string>

namespace ai::shaman_imbue
{
    enum class Spec { Tank, Enhancement, Elemental, Restoration };

    constexpr char const* Rockbiter = "rockbiter weapon";
    constexpr char const* Flametongue = "flametongue weapon";
    constexpr char const* Frostbrand = "frostbrand weapon";
    constexpr char const* Windfury = "windfury weapon";

    // Talent tab as AiFactory uses it for shamans: 0 elemental, 2 restoration, anything else enhancement.
    inline Spec SpecFor(bool tankStrategy, int talentTab)
    {
        if (tankStrategy)
            return Spec::Tank;
        if (talentTab == 0)
            return Spec::Elemental;
        if (talentTab == 2)
            return Spec::Restoration;
        return Spec::Enhancement;
    }

    constexpr char const* ActionName = "shaman weapon imbue";

    // The action a spec strategy binds to the "shaman weapon" trigger: the per-spec choice with the switch,
    // the strategy's old fixed imbue without it.
    inline char const* TriggerAction(bool bySpec, char const* oldAction)
    {
        return bySpec ? ActionName : oldAction;
    }

    // known(name) -> the bot has learned that imbue. Returns "" when it knows none of them.
    template <class Known>
    std::string Choose(Spec spec, bool inGroup, Known known)
    {
        if (spec == Spec::Enhancement)
        {
            if (known(Windfury))
                return Windfury;
            if (!inGroup && known(Frostbrand))
                return Frostbrand;
        }
        if (spec != Spec::Tank && known(Flametongue))
            return Flametongue;
        if (known(Rockbiter))
            return Rockbiter;
        return "";
    }
}
