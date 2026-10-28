
#include "playerbot/playerbot.h"
#include "TravelStrategy.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelRequestPolicy.h"
#include "playerbot/DirectTargetDestinations.h"

using namespace ai;

float TravelActionMultiplier::GetValue(Action* action)
{
    if (action == NULL) return 1.0f;

    bool hasTarget = AI_VALUE(bool, "travel target active");
    std::string name = action->getName();

    if (hasTarget && name.find("request") == 0)
    {
        if (name.find("guild meeting") != std::string::npos)
            return 1.0f;
        if (name.find("reagent vendor") != std::string::npos)
            return 1.0f;
        if (name.find("guild order") != std::string::npos)
            return 1.0f;

        return 0.0f;
    }

    return 1.0f;
}

void TravelStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TravelActionMultiplier(ai));
}

NextAction** TravelStrategy::GetDefaultNonCombatActions()
{
    return NextAction::array(0, new NextAction("travel", 1.0f), NULL);
}

void TravelStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    //The way these actions work:
    //One of these request target actions will trigger and start fetching destinations. 
    //While fetching no other travel action will be usefull except choose travel target.
    //Choose travel target will wait for the fetching to complete and then set a new target.
    //If no target was found the request action is marked as no targets found and can no longer trigger until reset.
    //The next request action will then trigger until a good target is found.
    //If no targets are found reset will clear all actions and start from the top.

    //All the standard choose travel target actions using either a specific val trigger or a generic purpose trigger.
    const std::vector<std::tuple<std::string, TravelDestinationPurpose, float>> PurposeActions =
    {        
        {"",TravelDestinationPurpose::AH, 6.95f},                                                //See isAllowed  90%
        {"",TravelDestinationPurpose::Vendor, 6.94f},                                                          // 90%
        {"",TravelDestinationPurpose::Repair, 6.93f},                                                          // 90%
        {"val::and::{should get money,can get mail,should get mail}", TravelDestinationPurpose::Mail, 6.79f},  //100%
        {"val::should get money", TravelDestinationPurpose::Grind, 6.77f},                                     // 90%
        {"",TravelDestinationPurpose::Mail, 6.6f},                                                             // 30%
        {"",TravelDestinationPurpose::GatherMining, 6.5f},                                                     // 90%/40% in group
        {"",TravelDestinationPurpose::GatherSkinning, 6.5f},                                                   // 90%/40% in group
        {"",TravelDestinationPurpose::GatherHerbalism, 6.5f},                                                  // 90%/40% in group
        {"",TravelDestinationPurpose::GatherFishing, 6.5f},                                                    // 90%/40% in group
        {"",TravelDestinationPurpose::Boss, 6.4f},                                                             // 50%
        // Grind was 6.27 here - dead last, one step below GenericRpg (6.28). Live data
        // (bot_intention.csv over a 7hr/20-bot run) confirmed this: GenericRpg got picked
        // over 2x as often as Grind (5.4% vs 2.5% of samples) whenever a bot had any
        // active travel purpose at all. Moved above Explore/GenericRpg so unconditional
        // grinding wins that priority race instead of losing to aimless wandering.
        {"",TravelDestinationPurpose::Grind, 6.35f},                                                          // 100%
        {"",TravelDestinationPurpose::Explore, 6.29f},                                                         // 10%
        {"",TravelDestinationPurpose::GenericRpg, 6.28f},                                                      // 50%
    };

    // twow-repo#541 (audit A18): with AiPlayerbot.Perf.TravelRequestGate the request triggers become
    // travel request::<condition> triggers - same condition, same event source - and do not read the
    // condition while the travel target is prepared or active (the request actions are useless then).
    bool const gateRequests = sPlayerbotAIConfig.perfTravelRequestGate;

    for (auto& [condition, purpose, relevance] : PurposeActions)
    {
        std::string trigger = condition.empty() ? "val::need travel purpose::" + std::to_string((uint32)purpose) : condition;
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName(trigger, gateRequests),
            NextAction::array(0, new NextAction("request travel target::" + std::to_string((uint32)purpose), relevance), NULL)));
    }

    // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.Station / Cooking / Dungeon / Raid, default 0): one row per class
    // built at boot and switched on at strategy init. Station 6.505: below trainer trade (6.51), above gathering (6.5)
    // and so above the quest request (6.3) - as narrow as gathering and yielding to open turn-ins like it (OB-10
    // 18.10.). Dungeon 6.299 / Raid 6.298 below the quest request, only without a running quest route backoff.
    if (ai::direct_targets::AnyBuilt())
    {
        const std::vector<std::pair<TravelDestinationPurpose, float>> DirectTargetActions =
        {
            {TravelDestinationPurpose::Station, 6.505f},
            {TravelDestinationPurpose::DungeonEntrance, 6.299f},
            {TravelDestinationPurpose::RaidEntrance, 6.298f},
        };

        for (auto& [purpose, relevance] : DirectTargetActions)
        {
            if (!ai::direct_targets::PurposeLive(purpose))
                continue;
            std::string trigger = "val::need travel purpose::" + std::to_string((uint32)purpose);
            triggers.push_back(new TriggerNode(
                travel_request::TriggerName(trigger, gateRequests),
                NextAction::array(0, new NextAction("request travel target::" + std::to_string((uint32)purpose), relevance), NULL)));
        }
    }

    //Specific named exceptions
    const std::vector<std::tuple<std::string, std::string, float>> StringActions =
    {
        {"val::should travel named::trainer class","request named travel target::trainer class", 6.89f},      // See isAllowed   25%
        //{"val::should travel named::trainer pet","request named travel target::trainer pet", 6.88f},        // 25%
        {"val::should travel named::trainer mount","request named travel target::trainer mount", 6.87f},      // 25%
        {"val::should travel named::mount","request named travel target::mount", 6.86f},                      // 25%
        {"val::should travel named::city","request named travel target::city", 6.85f},                        // 10%
        {"val::and::{has strategy::rpg quest,has focus travel target}","request quest travel target", 6.84f}, //100%
        {"val::should travel named::pvp","request named travel target::pvp", 6.83f},                          // 25%
        {"val::should travel named::guild meeting","request named travel target::guild meeting", 6.96f},      //100%
        {"val::should travel named::reagent vendor","request named travel target::reagent vendor", 6.955f},   //100%
        {"val::should travel named::guild order","request named travel target::guild order", 6.95f},          //100%
        {"val::and::{has strategy::rpg quest,should get money}", "request quest travel target", 6.78f},       // 90%
        {"val::not::travel target active","refresh travel target", 6.7f},                                     // 90%
        {"val::not::travel target active","choose group travel target", 6.65f},                               // 50%
        {"val::should travel named::trainer trade","request named travel target::trainer trade", 6.51f},      // 25%
        {"val::has strategy::rpg quest", "request quest travel target", 6.3f}                                 // 95%
    };

    for (auto& [trigger, action, relevance] : StringActions)
    {
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName(trigger, gateRequests && travel_request::IsRequestAction(action)),
            NextAction::array(0, new NextAction(action, relevance), NULL)));
    }

    // twow-repo#333 Berufe P1-b / P1-a (AiPlayerbot.ProfessionUse.ToolBuy / ProfessionTraining.Travel, default 0): gap
    // fillers of the quest-first backoff (CLI-Deep 25.10.2026) - below the quest request (6.3), which cannot act while the
    // backoff runs, above Explore (6.29) and GenericRpg (6.28). The tool first: a learned skill without its tool gathers
    // nothing. Rows only with the switch on; the value applies the cohort.
    if (sPlayerbotAIConfig.professionUseToolBuy)
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName("val::should travel named::tool vendor", gateRequests),
            NextAction::array(0, new NextAction("request named travel target::tool vendor", 6.297f), NULL)));
    if (sPlayerbotAIConfig.professionTrainingTravel)
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName("val::should travel named::trainer profession", gateRequests),
            NextAction::array(0, new NextAction("request named travel target::trainer profession", 6.296f), NULL)));
    // P1-n (AiPlayerbot.ProfessionTraining.NearRadius, OB-00 26.10.2026 after M1 FUTILE: the gap was too rare): a trainer of an
    // unlearned phase-one skill within the radius, no gap needed - above grind (6.35) and the quest request (6.3), below the
    // fixed targets; the radius bounds the detour, an open turn-in and a band walk still go first (the value checks them).
    if (sPlayerbotAIConfig.professionTrainingTravel && sPlayerbotAIConfig.professionTrainingNearRadius)
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName("val::should travel named::trainer profession near", gateRequests),
            NextAction::array(0, new NextAction("request named travel target::trainer profession near", 6.36f), NULL)));
    // P1-y (OB-00 28.10.2026 after M1r run 5: the gap tool trip came twice in 210 min): a vendor of the missing pick / knife within
    // the same radius, no gap needed - just above the near trainer trip (a learned skill without its tool gathers nothing), with
    // the same gates.
    if (sPlayerbotAIConfig.professionUseToolBuy && sPlayerbotAIConfig.professionTrainingNearRadius)
        triggers.push_back(new TriggerNode(
            travel_request::TriggerName("val::should travel named::tool vendor near", gateRequests),
            NextAction::array(0, new NextAction("request named travel target::tool vendor near", 6.361f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::not::travel target active",
        NextAction::array(0, new NextAction("choose travel target", 6.98f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::not::travel target active",
        NextAction::array(0, new NextAction("reset travel target", 6.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "has nearby quest taker",
        NextAction::array(0, new NextAction("reset travel target", 6.99f), NULL)));

    // #307: a roster bot on its own leaves a zone clearly above its level
    // before any other travel decision (the trigger holds the rules).
    triggers.push_back(new TriggerNode(
        "zone escape",
        NextAction::array(0, new NextAction("zone escape", 7.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "val::travel target ready",
        NextAction::array(0, new NextAction("check mount state", 1), new NextAction("move to travel target", 1), NULL)));

    triggers.push_back(new TriggerNode(
        "val::travel target traveling",
        NextAction::array(0, new NextAction("check mount state", 1), new NextAction("move to travel target", 1), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("travel", 1), NULL)));
}

void TravelOnceStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{   
    triggers.push_back(new TriggerNode(
        "val::travel target traveling",
        NextAction::array(0, new NextAction("check mount state", 1), new NextAction("move to travel target", 1), NULL)));
}