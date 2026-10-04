#pragma once
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/ServerFacade.h"

struct PetitionSummary;     // Guild/GuildMgr.h

namespace ai
{
    class TravelTarget;

    // twow-repo#485: guilds of persistent roster bots (AiPlayerbot.RosterGuild.*). One plan per
    // faction, shared by all map threads behind a std::mutex (GuildCreateActions.cpp). Its inputs are
    // copies taken under the GuildMgr locks and the roster faction from the player cache; no SQL.
    class RosterGuildPlan
    {
    public:
        // BotsPerGuild > 0, a persistent roster bot, no real master.
        static bool UsesRosterPath(PlayerbotAI* ai);
        // Faction below its guild target, foundings in flight counted.
        static bool MayFound(Player* bot);
        // Offer nearby: always in the stock path, below the target in the new path.
        static bool MayOfferNearby(PlayerbotAI* ai);
        // Purchase: target not covered by guilds + open bot charters, and an approved name free.
        static bool MayBuyCharter(Player* bot, char const*& reason);
        // Takes an approved name and counts the charter at once; "" = no purchase.
        static std::string ReserveCharter(Player* bot);
        // Below the target and a name available (the charter's own name or a free approved one).
        static bool MayTurnIn(Player* bot, std::string const& charterName);
        // Takes a founding slot and the guild name; false with reason when not allowed.
        static bool ReserveFounding(Player* bot, std::string const& charterName, std::string& name, uint32& target, char const*& reason);
        static void FinishFounding(Player* bot, std::string const& name, bool founded);
        // At most one line or retry per bot, key and interval (manual time).
        static bool IsDue(PlayerbotAI* ai, char const* key, uint32 intervalSeconds);
        // The charter item (5863) whose petition the bot owns, with a copy of that petition (and
        // whether accountId signed it); nullptr when there is none.
        static Item* OwnCharter(Player* bot, PetitionSummary& out, uint32 accountId = 0);

        // twow-repo#485 / #518: role fill (AiPlayerbot.RosterGuild.Tanks/Healers/Dps, the spread
        // switches, PlanFile; RosterGuildRolePolicy.h). On the roster path with a quota or a plan.
        static bool UsesRoleFill(PlayerbotAI* ai);
        // Stores the role of the bot's own talents (its own thread); returns the role that counts
        // (the plan's first) as roster_guild_role::Role.
        static uint8 ReportOwnRole(Player* bot);
        // The roster guild this roster bot is dealt to (plan guild or quota deal), 0 = none.
        static uint32 AssignedGuild(uint32 guidLow, Team team);
        // A bot charter: the owner's plan guild, and a free slot of the bot's role under the spread
        // rules; false with reason otherwise. Always true without quota and plan.
        static bool MaySignForRole(Player* bot, PetitionSummary const& offered, char const*& reason);
        // After a roster bot joined a guild: recount on the next call, trace.
        static void NoteJoined(Player* bot, uint32 guildId);
    };

    class BuyPetitionAction : public Action 
    {
    public:
        BuyPetitionAction(PlayerbotAI* ai) : Action(ai, "buy petition") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        static bool canBuyPetition(Player* bot);
    };

    class PetitionOfferAction : public Action 
    {
    public:
        PetitionOfferAction(PlayerbotAI* ai, std::string name = "petition offer") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return sPlayerbotAIConfig.randomBotFormGuild && !bot->GetGuildId(); };
    };

    class PetitionOfferNearbyAction : public PetitionOfferAction 
    {
    public:
        PetitionOfferNearbyAction(PlayerbotAI* ai) : PetitionOfferAction(ai, "petition offer nearby") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return sPlayerbotAIConfig.randomBotFormGuild && !bot->GetGuildId() && AI_VALUE2(uint32, "item count", chat->formatQItem(5863)) && AI_VALUE(uint8, "petition signs") < sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS) && RosterGuildPlan::MayOfferNearby(ai); };
    };

    class PetitionTurnInAction : public ChooseTravelTargetAction 
    {
    public:
        PetitionTurnInAction(PlayerbotAI* ai) : ChooseTravelTargetAction(ai, "turn in petition") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class BuyTabardAction : public ChooseTravelTargetAction 
    {
    public:
        BuyTabardAction(PlayerbotAI* ai) : ChooseTravelTargetAction(ai, "buy tabard") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
