
#include "playerbot/playerbot.h"
#include "ShamanActions.h"
#include "playerbot/AiFactory.h"
#include "playerbot/ShamanImbuePolicy.h"

using namespace ai;

bool CastShamanWeaponImbueAction::ChooseImbue()
{
    shaman_imbue::Spec const spec = shaman_imbue::SpecFor(
        ai->HasStrategy("tank shaman", BotState::BOT_STATE_COMBAT), AiFactory::GetPlayerSpecTab(bot));
    std::string const imbue = shaman_imbue::Choose(spec, bot->GetGroup() != nullptr,
        [this](char const* name) { return AI_VALUE2(uint32, "spell id", name) != 0; });
    if (imbue.empty())
        return false;

    SetSpellName(imbue);
    return true;
}

bool CastShamanWeaponImbueAction::isUseful()
{
    return ChooseImbue() && CastEnchantItemAction::isUseful();
}

bool CastShamanWeaponImbueAction::isPossible()
{
    return ChooseImbue() && CastEnchantItemAction::isPossible();
}
