/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcTankForm.h"

#include "Log.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "UnitDefines.h"

namespace
{
    // Both bear forms, best first. Dire Bear supersedes Bear outright (same
    // form family, strictly better stats), so a druid that has trained it is
    // never shifted into the lesser one — mirroring stock's "dire bear form"
    // ActionNode, whose alternative is "bear form".
    constexpr char const* kDireBear = "dire bear form";
    constexpr char const* kBear     = "bear form";
}

namespace DcTankForm
{
    bool IsBearTank(PlayerbotAI* botAI)
    {
        if (!botAI)
            return false;

        Player* bot = botAI->GetBot();
        if (!bot || bot->getClass() != CLASS_DRUID)
            return false;

        // A TANK-type combat strategy IS the definition of "fights as a bear" for
        // a druid - AiFactory hands a feral-tank druid "tank feral" (the bear
        // rotation and the combat-only shift trigger this gate pre-empts). A
        // cat/balance/resto druid holding the DC drive is left alone: shifting it
        // to bear would break its rotation outright.
        //
        // CLI-GRP 29.10.2026 (g2/CAUSE-DRUIDE-FERAL-SITZ): this asked HasStrategy
        // for the name "bear", a NAME the engine never holds - it keys strategies
        // by getName(), and the "bear" creator builds the same class as "tank
        // feral", whose getName() is "tank feral". So the gate was always false
        // and the druid tank pulled in caster form since the port (22.08.); the
        // feral seat was pulled from the test pool on 29.08. for exactly that
        // pattern. Ask the type, as PlayerbotAI::IsTank does.
        if (!botAI->ContainsStrategy(ai::STRATEGY_TYPE_TANK))
            return false;

        return botAI->HasSpell(kDireBear) || botAI->HasSpell(kBear);
    }

    bool EnsureBearForm(PlayerbotAI* botAI)
    {
        if (!IsBearTank(botAI))
            return false;

        Player* bot = botAI->GetBot();
        if (!bot->IsAlive() || bot->IsBeingTeleported())
            return false;

        ShapeshiftForm const form = bot->GetShapeshiftForm();
        if (form == FORM_BEAR || form == FORM_DIREBEAR)
            return false;   // already armed — the common case, and free

        // Shifting BETWEEN forms is the one case that needs a step. Stock's own
        // "dire bear form" ActionNode carries /*P*/ { "caster form" } for this,
        // and RemoveShapeshift is synchronous (GetShapeshiftForm reads FORM_NONE
        // immediately after — the same property DcFormGate relies on), so the
        // cast below still goes out THIS tick. A DC tank is normally formless
        // here, so this branch is the rare one (travel form off a mount, cat
        // form left over from a spec that later took the tank seat).
        if (form != FORM_NONE)
            botAI->RemoveShapeshift();

        char const* const spell = botAI->HasSpell(kDireBear) ? kDireBear : kBear;

        // CanCastSpell first so the ordinary blocked cases — mid-GCD, out of
        // mana, silenced — cost a cheap predicate instead of a failed cast
        // attempt and a log line every tick of the approach.
        if (!botAI->CanCastSpell(spell, bot))
            return false;
        if (!botAI->CastSpell(spell, bot))
            return false;

        LOG_INFO("playerbots.dungeonclear",
                 "[dungeon-clear] {} shifting to {} before the pull", bot->GetName(),
                 spell);
        return true;
    }
}
