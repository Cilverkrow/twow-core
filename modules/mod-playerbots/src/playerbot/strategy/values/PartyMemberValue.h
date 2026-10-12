#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    class FindPlayerPredicate
    {
    public:
        virtual ~FindPlayerPredicate() {}
        virtual bool Check(Unit*) = 0;
        // twow-repo#541 (audit A15): true only if Check() rejects every unit that is not
        // a Player in the asking bot's own group (Player::IsInGroup). Generic: false.
        virtual bool OnlyBotGroupMembers() const { return false; }
    };

    class SpellEntryPredicate
    {
    public:
        virtual bool Check(SpellEntry const*) = 0;
    };

    class PartyMemberValue : public UnitCalculatedValue
	{
	public:
        PartyMemberValue(PlayerbotAI* ai, std::string name = "party member") : UnitCalculatedValue(ai, name) {}

        // twow-repo#541 (audit A25, Perf.PartyTargetMemo, default 0): reuse the result inside the bot's
        // own DoNextAction pass until the next Execute. See PartyMemberValue.cpp.
        Unit* Get() override;
        void Set(Unit* unit) override;
        void Reset() override;

    public:
        bool IsTargetOfSpellCast(Player* target, SpellEntryPredicate &predicate);

    protected:
        Unit* FindPartyMember(FindPlayerPredicate &predicate, bool ignoreOutOfGroup = false, bool ignoreTanks = false);
        Unit* FindPartyMember(std::list<Player*>* party, FindPlayerPredicate &predicate, bool ignoreTanks);
        bool Check(Unit* player);
        // twow-repo#541 (audit A25): false for a Calculate that is randomised (soulstone D20 stagger);
        // such a value recomputes on every read as before, also with Perf.PartyTargetMemo = 1.
        virtual bool MemoAllowed() const { return true; }

    private:
        uint32 memoEpoch = 0;  // twow-repo#541 (audit A25): window epoch of the last computation, 0 = none
	};
}
