#pragma once

#include "playerbot/strategy/Action.h"

namespace ai
{
    class GuildAcceptAction : public Action {
    public:
        GuildAcceptAction(PlayerbotAI* ai) : Action(ai, "guild accept") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }
    };

    // twow-repo#485 (owner decision 5, poaching): may invitee, a guild member, take the inviter's
    // guild invitation (targetGuildId) or charter (targetGuildId 0, charterOfInviter: the inviter
    // owns it)? nullptr = yes, else the reason (GuildPoachPolicy.h). Reads copies only - GuildMgr
    // summaries, the guid-to-guild map, the player cache, the config - so the core may ask on the
    // inviter's map thread (PlayerScript::CanSwitchGuild). path names the caller in the trace; a
    // refusal of a roster bot is traced at most once a minute per bot.
    class RosterGuildPoach
    {
    public:
        static char const* Refusal(Player* inviter, ObjectGuid const& invitee, uint32 targetGuildId, bool charterOfInviter, char const* path);
        // No ping-pong on the charter path (guild_poach::KeepsPoachedCharter): the petition id the
        // bot keeps its signature on within the poaching cooldown, 0 = none (it may sign, join or
        // buy as usual). offeredCharter: the charter offered now, empty for an invitation or a
        // purchase. Copies only (GuildMgr petition summary, switch stamp, config).
        static uint32 KeptCharter(ObjectGuid const& bot, ObjectGuid const& offeredCharter);
    };
}
