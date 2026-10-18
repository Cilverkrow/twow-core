#pragma once
#include "playerbot/strategy/Value.h"
#include "TargetValue.h"

namespace ai
{
   
    class CcTargetValue : public TargetValue, public Qualified
	{
	public:
        CcTargetValue(PlayerbotAI* ai, std::string name = "cc target") : TargetValue(ai, name), Qualified() {}

    public:
        Unit* Calculate() override;

    private:
        // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): solo CC bookkeeping of this bot and spell (one value per bot and
        // qualifier, not per creature): the last picked target ([EmergencyLadder] solo-cc line once per new pick) and the add under the
        // bot's own CC with the time it was first seen (cc_broken = the aura was gone again within CcBrokenBeforeMs).
        ObjectGuid soloCcPick;
        ObjectGuid soloCcHeld;
        uint32 soloCcHeldSinceMs = 0;
    };
}
