#pragma once
#include "playerbot/strategy/Value.h"
#include "PartyMemberValue.h"

namespace ai
{
    class PartyMemberToSoulstone : public PartyMemberValue
    {
    public:
        PartyMemberToSoulstone(PlayerbotAI* ai, std::string name = "party member to soulstone") : PartyMemberValue(ai,name) {}

    protected:
        virtual Unit* Calculate() override;
        // twow-repo#541 (audit A25): Calculate rolls a D20 per computation when another warlock is in the
        // group; keeping that roll for a whole pass would change who gets the soulstone (member instead of
        // the self-cast fallback) and how often. Kept out of the Perf.PartyTargetMemo memo.
        bool MemoAllowed() const override { return false; }
    };
}
