#pragma once

// twow-repo#578 (b) (owner 11.10.2026 "freigegeben", relayed by OB-00): a quest whose RewSpell is a teaching spell
// (LEARN_SPELL effect) and RewSpellCast = 0 - e.g. 50002/50004 Disguise 5670/5671, 60060/60061 planter 46063 - made
// bots store the teaching spell itself (AutoLearnSpellAction::LearnQuestSpells -> LearnSpell(RewSpell)); its taught
// spells never came. Switch AiPlayerbot.AutoLearnQuestTeachEffects (default 0): such a RewSpell is applied like the
// turn-in cast would - the bot learns its LEARN_SPELL targets - and retired quests ([CANCELLED] / [DEPRECATED] in
// the title, plus the known ids, e.g. 60011/60012 Piercing Light, 80388 Hardcore) teach nothing. The hard-coded
// priest list keeps working with the switch off and on.

#include <cstdint>
#include <string_view>

namespace ai::quest_teach
{
    constexpr std::uint32_t LearnSpellEffect = 36;   // SPELL_EFFECT_LEARN_SPELL

    inline bool HasLearnEffect(std::uint32_t effect0, std::uint32_t effect1, std::uint32_t effect2)
    {
        return effect0 == LearnSpellEffect || effect1 == LearnSpellEffect || effect2 == LearnSpellEffect;
    }

    inline bool IsRetiredQuest(std::uint32_t questId, std::string_view title)
    {
        return questId == 60011 || questId == 60012 || questId == 80388 ||
            title.find("[CANCELLED]") != std::string_view::npos || title.find("[DEPRECATED]") != std::string_view::npos;
    }

    enum class RewSpellMode { Skip, TeachEffects, LearnDirect };

    // How a bot takes a quest's RewSpell when RewSpellCast is 0.
    inline RewSpellMode ForRewSpell(bool switchOn, bool retired, bool listedTeachingSpell, bool hasLearnEffect)
    {
        if (switchOn && retired)
            return RewSpellMode::Skip;
        if (listedTeachingSpell || (switchOn && hasLearnEffect))
            return RewSpellMode::TeachEffects;
        return RewSpellMode::LearnDirect;
    }
}
