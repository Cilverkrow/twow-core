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

        // CLI-GRP 30.10.2026 (OB-00 method decision, test harness only): the premade path's kit for a dungeon-clear test
        // seat. A test seat is a pool bot outside the persistent roster, so LearnSpells' roster gate gives it nothing;
        // this runs the same GrantSpecAuras (incl. what later PRs manage there) without that gate. False = no path kit.
        bool GrantSpecKitForTestSeat();

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

    // The only caller is the dungeon-clear test claim (DcTestRunJob); the world keeps the roster gate in LearnSpells.
    bool GrantSpecKitFor(PlayerbotAI* ai);
}
