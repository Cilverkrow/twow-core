#pragma once

#include "PlayerbotAI.h"

class Player;

namespace ai
{
    class AiObjectContext;
    class ReactionEngine;
    class Engine;
}

using namespace ai;

class AiFactory
{
public:
    static AiObjectContext* createAiObjectContext(Player* player, PlayerbotAI* ai);
	static Engine* createCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
	static Engine* createNonCombatEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static Engine* createDeadEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static ReactionEngine* createReactionEngine(Player* player, PlayerbotAI* const facade, AiObjectContext* AiObjectContext);
    static void AddDefaultNonCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* nonCombatEngine);
    static void AddDefaultDeadStrategies(Player* player, PlayerbotAI* const facade, Engine* deadEngine);
    static void AddDefaultCombatStrategies(Player* player, PlayerbotAI* const facade, Engine* engine);
    static void AddDefaultReactionStrategies(Player* player, PlayerbotAI* const facade, ReactionEngine* reactionEngine);

public:
    // The spec tab every caller uses. twow-repo#602 R06a (AiPlayerbot.Rotation.SpecFromPath): from level 10 the tab of
    // the bot's premade path when the switch applies to it, else the talent point majority (GetPlayerMajorityTab).
    static int GetPlayerSpecTab(const Player* player);
    // The tab the strategy kit and the role offer follow. twow-repo#602 (AiPlayerbot.Rotation.TankKitFromLevel, TankKitPolicy.h): a
    // random bot on its tank tab below the level of its tank spells gets its DPS tab (warrior arms, paladin retribution), unless a tank
    // role is forced on it; otherwise the spec tab. Talent choice and chat keep GetPlayerSpecTab.
    static int GetPlayerKitTab(const Player* player);
    // The talent point majority (ties: the lower tab; below level 10 or without points: the class default).
    static int GetPlayerMajorityTab(const Player* player);
    static std::map<uint32, int32> GetPlayerSpecTabs(const Player* player);
    static BotRoles GetPlayerRoles(uint8 cls, uint8 specTab);
    static BotRoles GetPlayerRoles(const Player* player);
    // #357: the premade path the bot was given ("" = none).
    static std::string GetPremadePathName(const Player* player);
    // twow-repo#602 R06a: the tab of that path (AiPlayerbot.PremadeSpecTab), -1 = none.
    static int GetPremadePathTab(const Player* player);
};
