#pragma once


#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    class AutoLearnSpellAction : public Action {
    public:
        AutoLearnSpellAction(PlayerbotAI* ai, std::string name = "auto learn spell") : Action(ai, name) {}
        
    public:
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

    private: 
        void LearnSpells(std::ostringstream* out);
        void LearnTrainerSpells(std::ostringstream* out);
        void LearnQuestSpells(std::ostringstream* out);
        void LearnDroppedSpells(std::ostringstream* out);
        void GetClassQuestItem(Quest const* quest, std::ostringstream* out);
        // #356: shaman totems that LearnQuestSpells misses (water, air).
        void GrantShamanTotems(std::ostringstream* out);
        bool IsClassGrantBot() const;
        // #357 O-12: route B auras of premade paths 7.1 / 7.3 (SpecAuraPolicy.h).
        void GrantSpecAuras();
        bool LearnSpell(uint32 spellId, std::ostringstream* out);
        bool LearnSpellFromSpell(uint32 spellId, std::ostringstream* out);
        bool IsValidSpell(uint32 spellId);
        bool IsTeachingSpellListedAsSpell(uint32 spellId);
    };
}
