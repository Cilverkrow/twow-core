#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::spirit_healer
{
// Owner order 27.10.2026 (via OB-00, AiPlayerbot.Revive.SpiritHealerFull, code default 1, 0 = rollback): a bot revived by a
// spirit healer gets no Resurrection Sickness and comes back at full health and mana (rage 0, energy full, as the core's
// ResurrectPlayer does at 100 %). Before: half health and mana, the sickness unless the bot has the repair cheat. Only the
// bot's own spirit healer revive (ReviveFromCorpseAction); a real player's spirit healer, a corpse run and every other revive
// are unchanged, and so is the 25 % durability loss.
// Pure rules, no game types.

inline bool FullApplies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

inline float RestorePercent(bool full)
{
    return full ? 1.0f : 0.5f;
}

inline bool ApplySickness(bool full, bool repairCheat)
{
    return !full && !repairCheat;
}
}
