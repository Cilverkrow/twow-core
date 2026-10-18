#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/RestGatePolicy.h"

namespace ai
{
    // twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): true while this bot has an applied rest episode (RestGatePolicy.h) - no
    // pull, no travel (can move around), eat / drink up to the targets. One value per bot, on the bot thread; the episode state lives
    // here. Solo free bots only (no group, no real player, no real master, not in a battleground). In the cohort the switch does not apply
    // to (mode 2, cohort A) the same episodes are tracked and logged with applied=0 and the value stays false - the paired counterpart.
    class RestGateValue : public BoolCalculatedValue
    {
    public:
        RestGateValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "rest gate", 1) {}
        bool Calculate() override;
        // A value dump (debug command) shows the state without driving the episode.
        std::string Format() override { return ai::rest_gate::Resting(episode) ? "true" : "false"; }

        // The switch is on and this bot rests (the turn-in stall observation counts it as a pause, OB-10 review 18.10.2026).
        static bool Resting(PlayerbotAI* ai);
        // A food / drink cast failed while resting: [RestGateTotal] cast_fail= (the action waits CastRetryMs before the next try).
        static void NoteCastFail(PlayerbotAI* ai);
        // Bot thread, switch first: combat start (PlayerbotAI::OnCombatStarted) and death (PlayerbotAI::OnDeath) close an open episode on
        // the event - nothing reads the value in the combat or dead engines (review 18.10.2026).
        static void OnCombatStarted(PlayerbotAI* ai);
        static void OnDeath(PlayerbotAI* ai);

    private:
        void Finish(ai::rest_gate::Closed const& closed, ai::rest_gate::Reading const& r);
        void CloseOnEvent(ai::rest_gate::End end);

        ai::rest_gate::Episode episode;
    };
}
