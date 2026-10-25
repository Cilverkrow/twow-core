#pragma once

#include "AmmoStockPolicy.h"
#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_set>
#include <vector>
#include "Config/Config.h"
#include "Talentspec.h"
#include "SharedDefines.h"
#include "SystemConfig.h"

class Player;
class PlayerbotMgr;
class ChatHandler;

#if PLATFORM == PLATFORM_WINDOWS
inline std::string _D_AIPLAYERBOT_CONFIG = "aiplayerbot.conf";
#else
inline std::string _D_AIPLAYERBOT_CONFIG = SYSCONFDIR "aiplayerbot.conf";
#endif

enum class BotCheatMask : uint32
{
    none = 0,
    taxi = 1 << 0,
    gold = 1 << 1,
    health = 1 << 2,
    mana = 1 << 3,
    power = 1 << 4,
    item = 1 << 5,
    cooldown = 1 << 6,
    repair = 1 << 7,
    movespeed = 1 << 8,
    attackspeed = 1 << 9,
    breath = 1 << 10,
    glyph = 1 << 11,
    quest = 1 << 12,    
    maxMask = 1 << 13
};

enum class BotAutoLogin : uint32
{
    DISABLED = 0,
    LOGIN_ALL_WITH_MASTER = 1,
    LOGIN_ONLY_ALWAYS_ACTIVE = 2
};

enum class BotSelfBotLevel : uint32
{
    DISABLED = 0,
    GM_ONLY = 1,
    ACTIVE_BY_COMMAND = 2,
    ALWAYS_ALLOWED = 3,
    ACTIVE_BY_LOGIN = 4, 
    ALWAYS_ACTIVE = 5
};

enum class BotAlwaysOnline : uint32
{
    DISABLED = 0,
    ACTIVE = 1,
    DISABLED_BY_COMMAND = 2
};

enum class BotLoginCriteriaType : uint8
{
    RACECLASS = 0,
    LEVEL = 1,
    RANGE_TO_PLAYER = 2,
    MAX_LOGIN_CRITERIA = 3
};

#define MAX_GEAR_PROGRESSION_LEVEL 6

class ConfigAccess
{
private:
    std::string m_filename;
    std::string m_envVarPrefix;
    std::unordered_map<std::string, std::string> m_entries; // keys are converted to lower case.  values cannot be.

public:
    std::vector<std::string> GetValues(const std::string& name) const;
    std::mutex m_configLock;
};

struct ParsedUrl {
    std::string hostname;
    std::string path;
    int port;
    bool https;
};

//GlyphPrioritySpecMap[specId][level] = {{glyphItemId, prereqTalentSpell}};
using GlyphPriority = std::pair<uint32, uint32>;
using GlyphPriorityList = std::vector<GlyphPriority>;
using GlyphPriorityLevelMap = std::unordered_map<uint32, GlyphPriorityList>;
using GlyphPrioritySpecMap = std::unordered_map<uint32, GlyphPriorityLevelMap>;

class PlayerbotAIConfig
{
public:
    PlayerbotAIConfig();
    static PlayerbotAIConfig& instance()
    {
        static PlayerbotAIConfig instance;
        return instance;
    }

public:
    bool Initialize();
    bool IsInRandomAccountList(uint32 id);
    bool IsFreeAltBot(uint32 guid);
    bool IsFreeAltBot(Player* player) {return IsFreeAltBot(player->GetGUIDLow());}
    bool IsInRandomQuestItemList(uint32 id);
	bool IsInPvpProhibitedZone(uint32 id);

    bool enabled;
    // mod-playerbots caps how many bots one account may .add; the dungeon-clear
    // test runner reads it to size its parties. Zero keeps that gate open, the
    // same default mod-playerbots ships.
    uint32 maxAddedBots = 0;
    // mod-playerbots rest threshold: mana percent above which a bot counts as
    // rested. Their shipped default.
    uint32 highMana = 65;
    // mod-playerbots gear ceiling for auto-equip; 0 is their shipped default
    // and means unlimited, which is also the only behaviour this tree has.
    uint32 autoGearScoreLimit = 0;
    uint32 autoGearQualityLimit = 0;  // like autoGearScoreLimit: 0 = no cap (mod-dungeon-clear reads it for its test sidecar)
    bool allowGuildBots;
    bool allowMultiAccountAltBots;
    uint32 globalCoolDown, reactDelay, maxWaitForMove, expireActionTime, dispelAuraDuration, passiveDelay, repeatDelay,
        errorDelay, rpgDelay, sitDelay, returnDelay, lootDelay;
    float sightDistance, spellDistance, reactDistance, grindDistance, lootDistance, groupMemberLootDistance, groupMemberLootDistanceWithActiveMaster,
        gatheringDistance, groupMemberGatheringDistance, groupMemberGatheringDistanceWithActiveMaster, shootDistance,
        fleeDistance, tooCloseDistance, meleeDistance, followDistance, raidFollowDistance, wanderMinDistance, wanderMaxDistance, whisperDistance, contactDistance,
        aoeRadius, rpgDistance, targetPosRecalcDistance, farDistance, healDistance, healDistanceBg, aggroDistance, proximityDistance, maxFreeMoveDistance, freeMoveDelay, walkDistance;
    // twow-repo#485 (#471): a roster skinner on its own takes every item from a corpse it can
    // skin, so leftover junk no longer blocks skinning (core: TARGET_NOT_LOOTED).
    // Off by default - corpse loot rules stay unchanged; skin loot is always kept (#485).
    bool professionUseClearCorpseForSkinning = false;
    // #276: bound for any graveyard other than the one nearest the corpse
    // (alternate after repeated deaths, or near the travel target).
    float maxAlternateGraveyardDistance = 2500.0f;
    uint32 criticalHealth, lowHealth, mediumHealth, almostFullHealth;
    uint32 lowMana, mediumMana;

    uint32 openGoSpell;
    bool randomBotAutologin;
    BotAutoLogin botAutologin;
    std::string randomBotMapsAsString;
    std::vector<uint32> randomBotMaps;
    std::list<uint32> randomBotQuestItems;
    // Audit A31 (twow-repo#563): IsInRandomAccountList runs on every region thread (~7 times per UpdateAI)
    // and appends on a cache miss, while the world thread iterates the list. Access only through the methods
    // below: the lock is AiPlayerbot.X3a.ContextLock, the set is a lookup index (the list keeps its order and
    // contents exactly as before), and the LoginDatabase query runs outside the lock.
    std::list<uint32> randomBotAccounts;
    std::unordered_set<uint32> randomBotAccountIndex;
    std::unordered_set<uint32> nonRandomBotAccounts;
    mutable std::shared_mutex randomBotAccountsMutex;
    void AddRandomBotAccount(uint32 id);
    std::vector<uint32> RandomBotAccountsSnapshot() const;
    size_t RandomBotAccountCount() const;
    std::list<uint32> randomBotSpellIds;
    std::list<uint32> randomBotQuestIds;
    std::list<uint32> immuneSpellIds;
    std::list<std::pair<uint32, uint32>> freeAltBots;
    std::list<std::string> toggleAlwaysOnlineAccounts;
    std::list<std::string> toggleAlwaysOnlineChars;
    bool enableRandomTeleports;
    bool enableMinimalMove;
    uint32 randomBotTeleportDistance;
    bool randomBotTeleportNearPlayer;
    uint32 transportTeleportType;
    uint32 randomBotTeleportNearPlayerMaxAmount;
    float randomBotTeleportNearPlayerMaxAmountRadius;
    uint32 randomBotTeleportMinInterval, randomBotTeleportMaxInterval;
    uint32 randomGearMaxLevel;
    uint32 randomGearMaxDiff;
    bool randomGearUpgradeEnabled;
    bool randomGearTabards;
    bool randomGearTabardsReplaceGuild;
    bool randomGearTabardsUnobtainable;
    float randomGearTabardsChance;
    std::list<uint32> randomGearBlacklist;
    std::list<uint32> randomGearWhitelist;
    bool randomGearProgression;
    float randomGearLoweringChance;
    bool rollBadItemsWithPlayer;
    // #341: need/greed/pass by role and profession for roster bots on their own.
    bool lootRollRoleAware = false;
    bool lootRollTrace = false;
    // twow-repo#363: roster bots up to a level cap buy vendor gear upgrades.
    bool vendorGearEnabled = false;
    uint32 vendorGearMaxLevel = 30;
    uint32 vendorGearMaxSpendPercent = 50;
    uint32 vendorGearReserveCopper = 0;
    uint32 vendorGearCooldownSeconds = 600;
    bool vendorGearTrace = false;
    // OB-10 train 6 / twow-repo#363: ammo and thrown weapon stock as item counts.
    // Empty / 0 = class default (hunter 8 stacks, otherwise 2 stacks).
    std::vector<ai::ammo_stock::Tier> hunterAmmoTiers;
    bool hunterAmmoFillQuiver = false;
    uint32 thrownMaxCount = 0;
    float randomBotMaxLevelChance;
    float randomBotRaceVariantChance;
    float randomBotRpgChance;
    float usePotionChance;
    float attackEmoteChance;
    bool randomBotAutoCreate;
    uint32 minRandomBots, maxRandomBots;
    uint32 randomBotUpdateInterval, randomBotCountChangeMinInterval, randomBotCountChangeMaxInterval;
    bool randomBotTimedLogout, randomBotTimedOffline;
    uint32 minRandomBotInWorldTime, maxRandomBotInWorldTime;
    uint32 minRandomBotRandomizeTime, maxRandomBotRandomizeTime;
    uint32 minRandomBotChangeStrategyTime, maxRandomBotChangeStrategyTime;
    uint32 minRandomBotReviveTime, maxRandomBotReviveTime;
    uint32 minRandomBotPvpTime, maxRandomBotPvpTime;
    uint32 randomBotsMaxLoginsPerInterval;
    uint32 randomBotsPerInterval;
    uint32 minRandomBotsPriceChangeInterval, maxRandomBotsPriceChangeInterval;
    //Auction house settings
    bool shouldQueryAHListingsOutsideOfAH;
    std::list<uint32> ahOverVendorItemIds;
    std::list<uint32> vendorOverAHItemIds;
    bool botCheckAllAuctionListings;
    bool botsSaveEpics;
    //
    bool randomBotJoinLfg;
    bool logRandomBotJoinLfg;
    bool randomBotJoinBG;
    bool randomBotAutoJoinBG;
    // twow-repo#517 stage a: a real player queues for WSG/AB/AV, persistent-roster bots fill it (RandomPlayerbotMgr::CheckBgFill).
    // All default off; restart-only. bgEnable is the master switch of every AiPlayerbot.BG.* stage.
    bool bgEnable = false;
    bool bgPlayerFill = false;
    uint32 bgMaxConcurrent = 0;          // 0 = no bot battleground at all (fail closed)
    uint32 bgCheckIntervalSeconds = 0;   // 0 = 30 s, minimum 10 s (ai::bgfill::CheckIntervalMs)
    bool bgTrace = false;                // [BGFill] lines
    // twow-repo#517 stage b: a waiting real player gets into a running battleground with fill bots (pair option, then
    // slot release with a reservation), and a started-for-fill battleground without a real player ends. Restart-only.
    bool bgSlotRelease = false;
    uint32 bgSlotReleaseWaitSeconds = 0; // 0 = 60 s, minimum 15 s (ai::bgfill::SlotReleaseWaitMs)
    uint32 bgBotOnlyGraceSeconds = 0;    // 0 = 120 s, minimum 60 s (ai::bgfill::BotOnlyGraceMs)
    // Follow-up D (OB-20 16.10.): the bot-only cleanup has its own switch, on by default but inert without PlayerFill.
    bool bgBotOnlyCleanup = true;
    // twow-repo#517 stage c (owner contract, decision C0 16.10.): announced bot-own battlegrounds and the honor rate for
    // bots in a battleground without a real player. Restart-only; neutral defaults (no bot-own battleground, factor 1).
    bool bgAnnounce = false;
    uint32 bgAnnounceIntervalMinutes = 0;    // 0 = no bot-own battlegrounds (ai::bgfill::AnnounceIntervalMs)
    uint32 bgMaxBotBGPerBracket = 0;         // 0 = 1 per level range (ai::bgfill::MaxOwnPerBand)
    float bgBotOnlyHonorRate = 1.0f;         // 1 = neutral; <= 0 or above 1 counts as 1 (ai::bgfill::BotOnlyHonorRate)
    uint32 bgAnnounceMode = 0;               // 0 chat, 1 server message, 2 chat with server fallback (ai::bgfill::AnnounceMode)
    uint32 bgAnnounceDelaySeconds = 0;       // 0 = 60 s, minimum 20 s (ai::bgfill::AnnounceDelayMs)
    uint32 bgAnnounceBotCooldownMinutes = 0; // 0 = 60 min (ai::bgfill::AnnounceCooldownMs)
    uint32 bgAnnounceMaxPerHour = 0;         // 0 = no announcement line at all (fail closed)
    bool bgAnnounceShowLevelRange = false;   // owner Q2: each line names the bracket's level range (ai::bgfill::ExpandAnnounceText)
    // Parsed text pools [team: 0 alliance, 1 horde][0 = generic, else the queue type: 1 AV, 2 WSG, 3 AB]; an empty key
    // takes the built-in pool (ai::bgfill::BuiltInAnnouncePool).
    std::vector<std::string> bgAnnounceText[2][4];
    uint32 randomBotBracketCount;
    bool randomBotLoginAtStartup;
    uint32 randomBotTeleLevel;
    bool logInGroupOnly, logValuesPerTick;
    bool fleeingEnabled;
    bool summonAtInnkeepersEnabled;
    std::string combatStrategies, nonCombatStrategies, reactStrategies, deadStrategies;
    std::string randomBotCombatStrategies, randomBotNonCombatStrategies, randomBotReactStrategies, randomBotDeadStrategies;
    uint32 randomBotMinLevel, randomBotMaxLevel;
    float randomChangeMultiplier;
    std::string premadeLevelSpec[MAX_CLASSES][10][91]; //lvl 10 - 100
    uint32 classRaceProbabilityTotal;
    uint32 classRaceProbability[MAX_CLASSES][MAX_RACES];
    bool useFixedClassRaceCounts;
    using ClassRacePair = std::pair<uint8, uint8>;
    std::map<ClassRacePair, uint32> fixedClassRaceCounts;
    uint32 levelProbability[PLAYER_STRONG_MAX_LEVEL + 1];   // see levelBucket in PlayerbotLoginMgr.h
    ClassSpecs classSpecs[MAX_CLASSES];
    GlyphPrioritySpecMap glyphPriorityMap[MAX_CLASSES];
    bool gearProgressionSystemEnabled;
    uint32 gearProgressionSystemItemLevels[MAX_GEAR_PROGRESSION_LEVEL][2];
    int32 gearProgressionSystemItems[MAX_GEAR_PROGRESSION_LEVEL][MAX_CLASSES][4][SLOT_EMPTY];
    std::string commandPrefix, commandSeparator;
    std::string randomBotAccountPrefix;
    // Table (optionally schema-qualified) backing the playerbot event store.
    // Defaults to the historical inline name in the character database;
    // see PlayerbotDatabaseContract.h for how it is used.
    std::string eventStoreTable;
    // Character names that stay online and are never teleported away.
    // Resolved to guids by RandomPlayerbotMgr, which has the database.
    std::list<std::string> pinnedBotNames;

    // Bots per team a battleground may fill while no real player is queuing for
    // that bracket, keyed by BattleGroundTypeId. Deliberately below the
    // template maximum so a player who queues later drops into the running
    // match rather than starting a second one. Empty means no cap.
    std::map<uint32, uint32> bgBotTeamCap;

    // -1 when the type is not listed at all, otherwise the configured number.
    // Zero is meaningful: it switches the battleground off for bots entirely,
    // which is what a map disabled by the client patch needs.
    // How many battleground instances of one type and bracket bots may keep
    // running at once while nobody real is waiting. Was hardcoded to 1, which
    // took Warsong from 400 matches a day down to 12.
    uint32 bgMaxInstancesPerBracket;
    int32 GetBgBotTeamCap(uint32 bgTypeId) const
    {
        auto it = bgBotTeamCap.find(bgTypeId);
        return it == bgBotTeamCap.end() ? -1 : (int32)it->second;
    }
    uint32 randomBotAccountCount;
    bool deleteRandomBotAccounts;
    uint32 randomBotGuildCount;
    bool deleteRandomBotGuilds;
    uint32 randomBotArenaTeamCount;
    bool deleteRandomBotArenaTeams;
    std::list<uint32> randomBotArenaTeams;
	bool RandombotsWalkingRPG;
	bool RandombotsWalkingRPGInDoors;
    bool boostFollow;
    bool turnInRpg;
    bool globalSoundEffects;
    bool shareTargets;
    // twow-repo#541 (R2b): a bot without group and master skips the shared-targets lookup.
    bool soloOwnAttackersOnly;
    std::list<uint32> randomBotGuilds;
	std::list<uint32> pvpProhibitedZoneIds;
    bool enableGreet;
    bool randomBotShowHelmet;
    bool randomBotShowCloak;
    bool disableRandomLevels;
    bool instantRandomize;
    bool gearscorecheck;
    int32 levelCheck;
	bool randomBotPreQuests;
    float playerbotsXPrate;
    bool disableBotOptimizations;
    bool disableActivityPriorities;
    bool forceActiveWhenNearPlayer;
    bool limitCombatActivity;
    bool guildOrderAlwaysActive;
    uint32 botActiveAlone;
    uint32 diffWithPlayer;
    uint32 diffEmpty;
    uint32 minEnchantingBotLevel;
    uint32 randombotStartingLevel;
    bool randomBotSayWithoutMaster;
    bool randomBotInvitePlayer;
    bool randomBotGroupNearby;
    // #276: seconds a dead bot defers to an active real-player master before it
    // releases, runs to its corpse or falls back to a spirit healer. 0 = unbounded.
    uint32 deadWaitForRealMasterSeconds = 180;
    uint32 deadWaitForRealMasterInstanceSeconds = 600;
    bool randomBotRaidNearby;
    bool randomBotGuildNearby;
    bool randomBotFormGuild;
    // twow-repo#485: guilds of persistent roster bots (GuildCreateActions.cpp, RosterGuildPolicy.h).
    // 0 = off (default), the stock petition path stays. N > 0 (needs randomBotFormGuild): per faction
    // ceil(roster bots / N) bot guilds, approved names only, recounted every SnapshotSeconds (default
    // 60, used as 10-3600). Defaults are set in Initialize() only, like the neighbours, so that
    // playerbot_config_key_usage still sees whether the members are read.
    uint32 rosterGuildBotsPerGuild;
    std::string rosterGuildNamesAlliance;
    std::string rosterGuildNamesHorde;
    uint32 rosterGuildSnapshotSeconds;
    // twow-repo#485 (owner 04.10.): roster bots in a guild keep "<Class> <Tank|tree> iLvl <n>" in
    // their public guild note. 0 = off (default, notes untouched); checked at most every
    // NoteRefreshSeconds per bot (default 86400 = 24 h, used as 60-604800), written only on a change.
    bool rosterGuildNote;
    uint32 rosterGuildNoteRefreshSeconds;
    // twow-repo#485 / #518: fill roster guilds by role (RosterGuildRolePolicy.h, GuildCreateActions.cpp).
    // Tanks/Healers/Dps per guild; defaults are the owner's 7/10/28 (04.10.), active only with
    // BotsPerGuild > 0; 0/0/0 = no role quota. HealerClassMin (default on), TankClassMix (owner mix)
    // and RarePairs/RarePairMaxShare (owner: about 2.5 %) are the owner rules; TankClassSpread and
    // RareComboSpread (default off) are the generic spread caps.
    uint32 rosterGuildTanks;
    uint32 rosterGuildHealers;
    uint32 rosterGuildDps;
    bool rosterGuildHealerClassMin;
    bool rosterGuildTankClassSpread;
    bool rosterGuildRareComboSpread;
    std::string rosterGuildTankClassMix;
    std::string rosterGuildRarePairs;
    float rosterGuildRarePairMaxShare;
    // OB-40's guilds.tsv (ordinal guid faction guild role class race); "" (default) = no plan. Read
    // only in Initialize() (startup, config reload), never on a tick. The lines are published as a
    // whole (std::atomic_store) and read through RosterGuildPlanLines() (std::atomic_load), so a
    // reload never changes lines a map thread is parsing. nullptr = no PlanFile; empty = set but
    // unreadable (twow-repo#518: rejected, fail-closed).
    std::string rosterGuildPlanFile;
    std::shared_ptr<const std::vector<std::string>> rosterGuildPlanLines;
    std::shared_ptr<const std::vector<std::string>> RosterGuildPlanLines() const { return std::atomic_load(&rosterGuildPlanLines); }
    // twow-repo#485 (owner decision 5, poaching): a roster bot in a bot guild takes a real player's
    // guild invitation or charter and switches (GuildPoachPolicy.h). 0 = off (default: "already in
    // a guild" as today); at most one switch per bot and PoachCooldownSeconds (default 86400, used
    // as 3600-2592000).
    bool rosterGuildAllowPoaching;
    uint32 rosterGuildPoachCooldownSeconds;
    uint32 rosterGuildMinLevel;  // owner 05.10.2026: bot guilds only from this level, 0 = off
    bool rosterGuildFoundTravel;  // hotfix 9.1 (#485): founding trip to a guild master + charter offered to online plan members
    // twow-repo#485 (owner decision 9): roster bots on their own use bandages, healing and mana
    // potions from their bags (ConsumablesPolicy.h). UseReal 0 = off (default, legacy: cast
    // without an item under the item cheat, sold). Pct 100 = no gate beyond the trigger;
    // KeepStacks used as at least 2; Buy 0 = no purchase.
    bool rosterConsumablesUseReal;
    uint32 rosterConsumablesHealingPotionPct;
    uint32 rosterConsumablesManaPotionPct;
    uint32 rosterConsumablesKeepStacks;
    bool rosterConsumablesBuy;
    bool rosterConsumablesTrace;
    uint32 rosterConsumablesTraceCooldownSeconds;
    bool randomBotRandomPassword;
    bool inviteChat;
    bool botsSilent;
    // Opt-in diagnostic logging: when true, [BOT] log lines and per-bot action
    // log files (logs/bots/<name>_acc<id>_<timestamp>.log) are emitted. Default
    // off so production servers don't pay disk I/O / branch overhead.
    bool enableActionLog;
    std::vector<uint32> actionLogGuids;  // twow-repo#601 / #614: AiPlayerbot.ActionLog.Guids, sorted; empty = every bot
    bool grindGreyByLevel = false;  // twow-repo#601 / #614: AiPlayerbot.Grind.GreyByLevel - the grind "not xp" skip (#421 roster rule and the old 50-in-51 skip of all bots) by mob level, not XP::Gain
    uint32 grindQuestFirstOrder = 0;  // twow-repo#601 decision order (owner 20.10.2026): AiPlayerbot.Grind.QuestFirstOrder - "attack anything" of quest-first roster bots only on quest objectives, attackers and mobs in the way; 0 off, 1 all, 2 cohort B (odd low guid)
    // Filename (relative to LogsDir) for the bot subsystem log. When set,
    // all sLog calls from bot .cpp files are redirected there instead of
    // writing to the main server log. Default: "bots.log". Empty = disabled.
    std::string botLogFile;
    bool enableOffSpecStrategies;
    bool useWanderAsDefaultFollowStrategy;
    std::string defaultFormation;

    uint32 guildMaxBotLimit;

    bool enableBroadcasts;
    uint32 broadcastChanceMaxValue;

    uint32 broadcastToGuildGlobalChance;
    uint32 broadcastToWorldGlobalChance;
    uint32 broadcastToGeneralGlobalChance;
    uint32 broadcastToTradeGlobalChance;
    uint32 broadcastToLFGGlobalChance;
    uint32 broadcastToLocalDefenseGlobalChance;
    uint32 broadcastToWorldDefenseGlobalChance;
    uint32 broadcastToGuildRecruitmentGlobalChance;
    uint32 broadcastToSayGlobalChance;
    uint32 broadcastToYellGlobalChance;

    uint32 broadcastChanceLootingItemPoor;
    uint32 broadcastChanceLootingItemNormal;
    uint32 broadcastChanceLootingItemUncommon;
    uint32 broadcastChanceLootingItemRare;
    uint32 broadcastChanceLootingItemEpic;
    uint32 broadcastChanceLootingItemLegendary;
    uint32 broadcastChanceLootingItemArtifact;

    uint32 broadcastChanceQuestAccepted;
    uint32 broadcastChanceQuestUpdateObjectiveCompleted;
    uint32 broadcastChanceQuestUpdateObjectiveProgress;
    uint32 broadcastChanceQuestUpdateFailedTimer;
    uint32 broadcastChanceQuestUpdateComplete;
    uint32 broadcastChanceQuestTurnedIn;

    uint32 broadcastChanceKillNormal;
    uint32 broadcastChanceKillElite;
    uint32 broadcastChanceKillRareelite;
    uint32 broadcastChanceKillWorldboss;
    uint32 broadcastChanceKillRare;
    uint32 broadcastChanceKillUnknown;
    uint32 broadcastChanceKillPet;
    uint32 broadcastChanceKillPlayer;

    uint32 broadcastChanceLevelupGeneric;
    uint32 broadcastChanceLevelupTenX;
    uint32 broadcastChanceLevelupMaxLevel;

    uint32 broadcastChanceSuggestInstance;
    uint32 broadcastChanceSuggestQuest;
    uint32 broadcastChanceSuggestGrindMaterials;
    uint32 broadcastChanceSuggestGrindReputation;
    uint32 broadcastChanceSuggestSell;
    uint32 broadcastChanceSuggestSomething;

    uint32 broadcastChanceSuggestSomethingToxic;

    uint32 broadcastChanceSuggestToxicLinks;
    std::string toxicLinksPrefix;
    uint32 toxicLinksRepliesChance;

    uint32 broadcastChanceSuggestThunderfury;
    uint32 thunderfuryRepliesChance;

    uint32 broadcastChanceGuildManagement;

    uint32 guildRepliesRate;

    uint32 botAcceptDuelMinimumLevel;

    bool talentsInPublicNote;
    bool nonGmFreeSummon;
    uint32 rosterControlGmMinSecurity;
    uint32 rosterSummonCooldownSeconds;
    bool rosterListEnabled;
    uint32 rosterListMinIntervalMs;
    uint32 rosterListPageSize;
    uint32 rosterListCacheMs;

    BotSelfBotLevel selfBotLevel;
    uint32 iterationsPerTick;

    std::string autoPickReward;
    bool autoEquipUpgradeLoot;
    // #308: log one BASIC [ITEM EQUIP] line per bagged green+ weapon/armor decision.
    bool equipDiagnosticsTrace = false;
    // #308: with the auction house off, bots never mark items for it (no AH
    // usage, no AH sell/buy visits); such items go to the vendor instead.
    bool auctionHouseEnabled = true;
    bool syncQuestWithPlayer;
    bool syncQuestForPlayer;
    std::string autoTrainSpells;
    // Disabled by default: only the dedicated persistent-roster profession
    // action reads these values. Generic trainer behaviour is unchanged.
    bool professionTrainingFreeForPersistentRoster = false;
    uint32 professionTrainingStartLevel = 1;
    float professionTrainingLocalTrainerRadius = 120.0f;
    bool professionTrainingTrace = false;
    uint32 professionTrainingTraceCooldownSeconds = 300;
    // twow-repo#333 Berufe P1-a / P1-b (ProfessionTravelPolicy.h): gap-filler trips of a roster bot to the trainer of
    // a phase-one profession and to the vendor of its missing gathering tool; 0 off, 1 all, 2 cohort B (odd guid).
    uint32 professionTrainingTravel = 0;
    uint32 professionUseToolBuy = 0;
    // #333: roster bots use their professions while levelling.
    float professionUseGatherDistance = 0.0f;     // 0 = GatheringDistance
    bool professionUseCraft = false;
    uint32 professionUseCraftIntervalSeconds = 300;
    bool professionUseTrace = false;
    uint32 professionUseTraceCooldownSeconds = 300;
    // twow-repo#485: craft with real reagents from the bags (the item cheat
    // made every recipe look craftable). false = legacy "craft random item".
    bool professionUseRealReagents = false;
    uint32 professionUseCraftFailBackoffSeconds = 1800;
    // twow-repo#485: roster bots keep the materials of their recipes and buy
    // these vendor reagents (item ids). false / empty = legacy.
    bool professionUseKeepCraftMaterials = false;
    uint32 professionUseReagentKeepStacks = 1;
    // Hotfix 8.27: quest log rotation of stuck roster bots (8.17), tunable by reload config.
    bool questRotateEnabled = true;
    uint32 questRotateIdleSeconds = 2400;
    uint32 questRotateGraceSeconds = 1800;
    // twow-repo#524: wool rule - tailoring below WoolTierSkill keeps every linen, then first aid
    // only uses linen above FirstAidClothReserve.
    uint32 professionUseWoolTierSkill = 75;
    uint32 professionUseFirstAidClothReserve = 20;
    // twow-repo#540: start-phase spread of journeys (jitter after login, long-move budget after start).
    uint32 startupTravelJitterSeconds = 300;
    uint32 startupTravelWindowSeconds = 900;
    uint32 startupTravelMaxLongMovesPerSlot = 4;
    std::set<uint32> professionUseVendorReagents;
    std::string autoPickTalents;
    bool autoLearnTrainerSpells;
    bool autoLearnQuestSpells;
    bool autoLearnQuestTeachEffects = false;  // twow-repo#578 (b): a teaching RewSpell teaches its LEARN_SPELL targets
    bool autoLearnDroppedSpells;
    bool autoLearnTimingTrace = false;   // #351: [AutoLearn] timing per level-up/login
    // #356: roster bots get class quest tools the quest path misses + [ClassGrant] log.
    bool classGrantEnabled = false;
    bool autoDoQuests;
    bool syncLevelWithPlayers;
    uint32 syncLevelMaxAbove, syncLevelNoPlayer;
    bool syncAltLevelToMaster;
    uint32 tweakValue; //Debugging config
    float respawnModNeutral, respawnModHostile;
    uint32 respawnModThreshold, respawnModMax;
    bool respawnModForPlayerBots, respawnModForInstances;

    bool randomBotLoginWithPlayer;
    bool asyncBotLogin, preloadHolders;
    // Fail-closed, versioned GUID roster. Default is false so the legacy
    // random-bot behavior remains the complete disabled-feature path.
    bool persistentActiveRosterEnabled = false;
    bool parkHideFromBots = false;  // twow-repo#541/#551: parked bots invisible to other bots (core Player flag)
    bool buffRealPlayerStyle = false;  // twow-repo#588 (owner 14.10): buff group, then guild, strangers only once per cooldown
    uint32 buffStrangerCooldownSeconds = 900;
    uint32 lootRoleWeights = 0;       // twow-repo#308 LOOT-ROLE-01: 1 = weight scale from the premade path from L1, quest reward by gain; 2 = and the owner stat table v2
    bool lootFeralFixedSpec = true;   // twow-repo#308 (owner 26.09.): bear/cat scale by the premade path, not a coin per check
    bool equipKeepWeaponsUnlessUpgrade = false;   // twow-repo#308 EQUIP-FLIP-01: hands come off only for a hand-item candidate in the bags
    bool parkAnyLevelInn = false;  // twow-repo#551 (owner 09.10): park in the nearest inn of the faction with room, any level band
    bool parkSleepGrid = false;  // twow-repo#541: parked bots do not activate the cells around them (core Player flag)
    bool parkCellSourceStats = false;  // twow-repo#541 P2 D0: [CellSource] minute line - continent cells by source (real players / bots / active non-players)
    bool parkCellSourceKinds = false;  // twow-repo#541 P2 D5: [CellSourceKinds] minute line + [CellSourceTop] every 10 min - active non-players by kind (needs CellSourceStats)
    uint32 parkMaxPerInn = 0;   // twow-repo#551 (owner 10.10): parked bots per inn, 0 = ai::park::SpotCapacity (25) as before
    uint32 parkMaxPerCity = 0;  // twow-repo#551 (owner 10.10): parked bots per capital zone (inns + city spots), 0 = no limit
    bool parkQuiet = false;  // twow-repo#551 (owner 11.10): parked bots drop greet/emote/say/ai chat, answer whispers only
    uint32 parkQuietIdleEmoteSeconds = 600;  // twow-repo#551: one idle emote per this many seconds while quiet (0 = none)
    bool parkPersist = false;  // twow-repo#551 (owner 11.10): park state in the bot event store, restored after a restart
    bool parkSeats = false;  // twow-repo#551 (owner 11.10): parked bots sit on inn chairs, dismount, keep 3 yd around the innkeeper
    bool worldBotsTrace = false;  // twow-repo#541: [WorldBots] per-minute world-thread cost line
    bool activityTrace = false;  // twow-repo#601 R01 L1: AiPlayerbot.ActivityTrace - [BotActivity] per roster bot and hour, [BotActivityMin]
    bool botUpdateTrace = false;  // twow-repo#541: [BotUpdate] per-minute distribution of PlayerbotAI::UpdateAI wall time
    bool botUpdateTraceTick = false;  // twow-repo#597 (TICK-01): [BotUpdateSlow] ends with " tick=<world loop> region=<map>.<inst>"
    bool worldBotsHookTrace = false;  // twow-repo#597 (TICK-01): [WorldBots] ends with hook_ms= remote_ms= ahbot_ms= dungeonclear_ms=
    bool botUpdateTraceTriggers = false;  // twow-repo#596 D: with BotUpdateTrace, the TIME entry names the 3 slowest trigger checks of a pass >= 20 ms
    bool gatherFishingSkillCheck = false;  // twow-repo#596 / P3-b review: fishing destinations need the zone's fishing skill (shadowed variable fixed)
    bool perfBindAreaFromHomebind = false;  // twow-repo#596 D: home-bind area from the core stored bind area, no terrain lookup at a far position
    uint32 aiDelayJitterPct = 0;  // twow-repo#541: +-pct spread of the AI delays out of combat, start offset, parked spread (0 = off)
    bool checkValuesSkipSearches = false;  // twow-repo#541: "check values" without its six forced value searches
    bool initPetCache = false;  // twow-repo#541: "initialize pet" with a static tameable list, cached pet DB answer, cooldown
    bool shimIdSets = false;  // twow-repo#510: creature/GO scans walk the template id sets instead of the fixed caps (boot-time mode)
    bool shimFactionIdSet = false;  // twow-repo#510 A2: the two faction loops walk the reputation list instead of ids below 100
    bool x3cInboxWrites = false;  // twow-repo#563 (X3c): value writes into other bots go through their inbox
    bool perfSkipBotUpdatePackets = false;  // twow-repo#541 (owner 11.10.): core builds no SMSG_UPDATE_OBJECT/DESTROY for bot sessions
    bool warnUnknownNames = false;  // twow-repo#568 (c): once-per-name [UnknownName] line for trigger/action/strategy names without a creator
    bool packetTrace = false;  // twow-repo#541 (owner 11.10.): [BotPackets] packets per receiver / map / class, measurement only
    bool nearestSkipHiddenBots = false;  // twow-repo#541 (owner 11.10. "1b"): nearest*/possible-targets values skip parked bots hidden from bots
    uint32 botSaveIntervalSeconds = 0;  // twow-repo#541 A47: bots save themselves every N s (0 = core autosave as before)
    uint32 botSaveMaxPerTick = 1;       // A47: own bot saves per map thread and world tick (0 = no limit)
    bool perfFleeMemo = false;  // twow-repo#541 (audit A01): FleeManager reuses a candidate angle's result within one search (0 = off)
    bool findCorpseLazySpot = false;  // twow-repo#541 (audit A02): find corpse skips the safe revive spot in the two branches that never use it (0 = off)
    bool nearestUnitsAcceptFirst = false;  // twow-repo#541 (audit A03): nearest npcs / friendly players / stealthed units run their pure filter before the LOS raycast; friendly players visit the world container only (0 = off)
    bool perfGatherLootFastPath = false;  // twow-repo#541 (audit A04+A05): "add gathering loot" skill reject before the LOS raycast, corpse list only with skinning (0 = off)
    uint32 perfAttackersLazyChecks = 0;  // twow-repo#541 (audit A06): AttackersValue::IsValid level 1 = area lookup only where read (enemy players, pets), 2 = plus the bot's own not-fighting candidates rejected first (0 = off)
    bool perfActionNodeOwnerCache = false;  // twow-repo#541 (audit A08): Engine::CreateActionNode on the bot's own AI tick asks only the strategy that answered a base action name first (0 = off)
    bool perfBgMasterCacheRef = false;  // twow-repo#541 (audit A09): "rpg bg type" / "bg masters" read the battlemaster cache by const reference with find(), no deep copy (0 = off)
    bool perfTravelInfoReuse = false;  // twow-repo#541 (audit A10): a travel choice builds one PlayerTravelInfo, reuses a known IsActive answer and checks the cheap hub conditions first; GetDestinations looks one asked entry up by key (0 = off)
    bool perfLogActionFastPath = false;  // twow-repo#541 (audit A11): no action-log lookup while no file can be written, PerfMon action/trigger keys only with PerfMon on (0 = off)
    uint32 perfActionBudget = 0;  // twow-repo#541 (owner 11.10.): expensive actions per map thread and tick, 0 off / 1 max 3 / 2 max 1 / 3 spread (ActionBudgetPolicy.h)
    std::vector<std::string> perfActionBudgetActions;  // the expensive actions (empty config = ai::action_budget::DefaultExpensiveActions)
    bool perfActionBudgetStartExempt = false;  // twow-repo#591: bots without a travel target choose one without budget
    uint32 perfActionBudgetLimit = 0;          // twow-repo#591: per-window limit, 0 = the mode's (3 / 1)
    bool botUpdateTraceTailFix = false;  // twow-repo#541 (audit A11): engine last-action trail keeps the newest 512 characters, cut at an entry (0 = off)
    bool perfTrainableSpellsPrecheck = false;  // twow-repo#541 (audit A12): "trainable spells" skips a spell that fails the core's own skill RED check before the trainer state walk, roster state read once per call (0 = off)
    bool perfRacialsSkipUnbuildable = false;  // twow-repo#541 (audit A13): racials strategy does not register gift of the naaru / mana tap / arcane torrent (no action in this vanilla build); NOT strictly neutral, engine queue path in rare ticks (0 = off)
    bool chaseSkipHazardPathWhenNoHazards = false;  // twow-repo#541 (audit A14): ChaseTo skips the hazard path / LOS checks while the bot's hazards list is empty; same chase decisions, only the detail log line differs (0 = off)
    bool perfGiveItemGroupOnlyScan = false;  // twow-repo#541 (audit A15): give water / food / item value of a bot without group skips the out-of-group classification and list scan; same result, disclosed: no GetPet()/eventCache side effects on foreign players (0 = off)
    bool perfStaticSpellLists = false;  // twow-repo#541 (audit A16): CanCastSpell / CheckSpellTargetAlignment take the neutral and out-of-control spell ids from constant arrays instead of a per-call std::list; same ids, same answers (0 = off)
    bool petDeadNoWorldPetFalse = false;  // twow-repo#541 (audit A07): hunter without a world pet never counts as having a dead pet (no character_pet SELECT, no doomed Revive Pet)
    bool canCastSpellChecksPower = false;  // twow-repo#541 (audit A33): CanCastSpell (unit target, spellbook) also checks the mana/rage/energy/health cost (0 = off)
    bool castSpellRefreshOnLearn = false;  // twow-repo#541 (audit A27): spell actions re-read spell id/range when the bot learned spells, levelled, spent talent points or (pet spell actions) got another pet (0 = off)
    bool rotationCastTrace = false;  // twow-repo#602 ROT-01 D0: [RotCast]/[RotCastTotal]/[RotCastUnknown]/[RotRole]/[RotPull] cast diagnostics, measurement only (0 = off)
    uint32 rotationSpecFromPath = 0;  // twow-repo#602 ROT-01 R06a: spec tab from the premade path from level 10, cohort mode 0 off / 1 all / 2 cohort B
    uint32 rotationEmergencyLadder = 0;  // twow-repo#602 ROT-01 X3: emergency ladder for solo free bots below critical health, cohort mode 0 off / 1 all / 2 cohort B
    uint32 rotationEmergencyLadderFightTargetHp = 20;  // twow-repo#602 X3 (F2): fight on instead of fleeing when the single target is at or below this HP %
    std::vector<std::string> rotationEmergencyLadderSoloCcSpells;  // twow-repo#602 X3 (F3): spells allowed for solo CC on an add (lower case)
    uint32 rotationRestGate = 0;  // twow-repo#602 ROT-01 X1: rest before the next pull for solo free bots, cohort mode 0 off / 1 all / 2 cohort B
    uint32 rotationRestGateStartHp = 70;  // twow-repo#602 X1 (E2/E4): a rest episode starts below this HP % ...
    uint32 rotationRestGateStartMana = 40;  // ... or below this mana % (mana pool only)
    uint32 rotationRestGateTargetHp = 90;  // ... and ends at or above this HP %
    uint32 rotationRestGateTargetMana = 85;  // ... and this mana %
    uint32 rotationRestGateMaxRestSeconds = 120;  // ... or after this many seconds (then no new episode for a minute)
    uint32 rotationCastLosGate = 0;  // twow-repo#602 (hunter LoS 25.10.): a LINE_OF_SIGHT verdict of CheckCast makes the cast IMPOSSIBLE, 8.2-unreachable targets are dropped and skipped (CastLosGatePolicy.h); with Rotation.ReachLosClose; 0 off, 1 all, 2 cohort B
    bool perfCombatIdleYield = false;  // twow-repo#541 (audit A17): a combat AI pass without an action while the bot auto-attacks its current target waits 3x ReactDelay (0 = off)
    uint32 castWaitPreparing = 0;  // twow-repo#602 (L1-b): hold the internal AI until the real end of the bot's own PREPARING cast; 0 off, 1 all, 2 cohort B
    uint32 rotationMeleeFleeBreak = 0;  // twow-repo#602 (death analysis 21.10.): "outnumbered" stays quiet for a melee bot while its flee loop holds; 0 off, 1 all, 2 cohort B
    bool rotationMeleeFleeBreakRanged = false;  // twow-repo#602 (step 2, 21.10.): the same hold for ranged bots (cohort from Rotation.MeleeFleeBreak); kite triggers untouched
    uint32 rotationFleeAway = 0;  // twow-repo#602 (c) (hunter + adds analysis 25.10.): flee chase angle relative to the enemy's facing, a kept chase (1 s), hunter flee only in the dead zone while the pet holds the enemy (FleeAwayPolicy.h); 0 off, 1 all, 2 cohort B
    uint32 rotationReachLosClose = 0;  // twow-repo#594 (Linalella, 23.10.): a hostile reach target out of sight is chased to half the distance until in sight, never inside the floor (ReachLosPolicy.h); 0 off, 1 all, 2 cohort B (odd guid)
    uint32 rotationReachPlayerMaster = 0;  // twow-repo#630 GRP-REACH-01: the reach progress check (hotfix 8.2) also for a bot with a real player as master, own target only, 30 s ignore; 0 off, 1 all, 2 cohort B (odd guid)
    bool perfTravelRequestGate = false;  // twow-repo#541 (audit A18): travel request triggers skip their condition while the travel target is prepared or active (0 = off)
    bool perfPartyBuffKnownSpellGate = false;  // twow-repo#541 (audit A22): five party buff/cure triggers skip their party scan while the bot knows no rank of the action's spell (0 = off)
    bool perfLazyEngineInit = false;  // twow-repo#541 (audit A21): a strategy change rebuilds an engine that is not running (idle combat/non-combat, dead) only when it is activated (0 = off)
    bool perfAttackersSingleList = false;  // twow-repo#541 (audit A23): "has attackers" and "possible attack targets::1" use the full "attackers" list, no second attackers::1 pass (0 = off)
    bool perfPartyTargetMemo = false;  // twow-repo#541 (audit A25): party-target values reuse their result inside one DoNextAction pass until the next Execute (0 = off)
    bool possibleTargetsSharedSearch = false;  // twow-repo#541 (audit A32): unqualified "possible targets" variants of a bot without a group filter its own sight-range snapshot again instead of a second grid search (0 = off)
    bool x3bPublishedTargets = false;  // twow-repo#563 (X3b): other bots read published targets, not our values
    bool x3bPublishedConditions = false;  // twow-repo#563 (X3b site 2): group conditions answered by each member itself
    bool dietRogueCombatBoostInCombat = false;  // twow-repo#568 (diet D03): combat rogue boost registers adrenaline rush / blade flurry in the combat engine, not the non-combat engine where BoostTrigger is never active (0 = off)
    bool dietRogueNoMutilate = false;  // twow-repo#568 (diet D15): assassination builders map to backstab (behind) / sinister strike (front) instead of the non-existent Mutilate (0 = off)
    bool dietWarriorNoHeroicThrow = false;  // twow-repo#568 (diet D16): protection warriors push charge / taunt / taunt on snare target without the non-existent Heroic Throw step (0 = off)
    bool dietArmsSnareHamstring = false;  // twow-repo#568 (diet D17): arms cc 'hamstring' trigger pushes hamstring instead of piercing howl -> mocking blow (taunt) -> hamstring (0 = off)
    bool dietWarriorNoDevastate = false;  // twow-repo#568 (diet D36): protection 'sunder armor' trigger pushes sunder armor instead of the unlearnable devastate (0 = off)
    bool dietWarriorDisarmGate = false;  // twow-repo#568 (diet D37): 'disarm' trigger also requires a known, ready Disarm and Defensive Stance (0 = off)
    bool dietWarriorBerserkerStanceGate = false;  // twow-repo#568 (diet D38): stance rules (owner 11.10.2026): the 'berserker stance' trigger requires the spell, arms does not register it, protection has no Defensive Tactics swap, Berserker Rage has no stance prerequisite (0 = off)
    bool dietWarriorNoInterceptAndRage = false;  // twow-repo#568 (diet D39): protection does not register the never-firing 'intercept and rage' nodes (sub-trigger "enemy is out of melee" has no creator) (0 = off)
    bool dietWarriorDedupBloodrage = false;  // twow-repo#568 (diet D40): 'bloodrage' trigger requires a known, ready Bloodrage; protection drops its duplicate bloodrage / concussion blow nodes (0 = off)
    bool dietPriestRacials = false;  // twow-repo#568 (D20): priest race spells (touch of weakness, shadowguard, hex of weakness, devouring plague, elune's grace, desperate prayer, starshards) only for races that can learn them; the four race triggers wait for a known spell id (0 = off)
    bool dietPriestSelfBuffKnown = false;  // twow-repo#568 (D22): inner fire / shadowform / inner focus triggers wait for a known spell id, inner focus also for its cooldown (0 = off)
    bool dietShadowPriestDeadNodes = false;  // twow-repo#568 (D32): shadow priest strategy does not register "vampiric touch" (passive talent in Turtle) and "shadowfiend" (no such spell in Turtle 1.18) (0 = off)
    bool dietWarlockCcKnown = false;  // twow-repo#568 (D23): warlock cc pushes death coil (snare) and howl of terror only while the spell is known (0 = off)
    bool dietWarlockTalentKnown = false;  // twow-repo#568 (D24): warlock talent spells siphon life (also on attacker), conflagrate, shadowburn (pve + pvp) and dark pact fire only while the spell is known (0 = off)
    bool dietWarlockAmplifyCurse = false;  // twow-repo#568 (D25): warlock boost does not register "amplify curse" (only "[Deprecated] Amplify Curse" exists in Turtle 1.18) (0 = off)
    bool dietMageArmorKnown = false;  // twow-repo#568 (D33): "mage armor" trigger of a mage without mage armor asks for the best known armor (ice armor, else frost armor) (0 = off)
    bool dietMageTalentBoostKnown = false;  // twow-repo#568 (D34): arcane power / presence of mind / combustion / ice barrier triggers wait for the known and ready spell (0 = off)
    bool dietMageBlastWave = false;  // twow-repo#568 (D35): fire cc pushes blast wave only while it is known ("blast wave" trigger), and the blast wave node drops its frost nova alternative (vanilla build) (0 = off)
    bool dietMageInvisibility = false;  // twow-repo#568 (D53): mage raid strategy does not register "invisibility" on high threat / multiple attackers (not learnable in Turtle 1.18) (0 = off)
    bool dietLevelingDruidWrath = false;  // twow-repo#568 D04: leveling druid Wrath node uses the registered trigger "enemy out of melee" (0 = off, node stays dead)
    bool dietShockCooldownGate = false;  // twow-repo#568 D26: shaman "shock" trigger off while the shared shock category cooldown runs (0 = off)
    bool dietFireNovaTotem = false;  // twow-repo#568 D27: shamans cast "fire nova totem" where vanilla pushed the uncastable "fire nova" (0 = off)
    bool dietNoWrathOfAirTotem = false;  // twow-repo#568 D28: vanilla ele/resto shamans skip the nonexistent "wrath of air totem" node and alternative (0 = off)
    bool dietShamanVanillaNames = false;  // twow-repo#568 D29: vanilla shamans push healing wave / searing totem / flametongue weapon instead of riptide / totem of wrath / earthliving; dead resto factory keys dropped (0 = off)
    bool dietInnervateFix = false;  // twow-repo#568 D30: Innervate trigger/action use the real mana percent and accept the solo druid itself (0 = off)
    bool dietRetRepentanceKey = false;  // twow-repo#568 D31: Ret "repentance or shield" node registered under its pushed name (0 = off, factory key typo)
    bool dietExorcismCreatureType = false;  // twow-repo#568 D41: paladin "exorcism" trigger only for targets of Exorcism's creature types (undead/demon) (0 = off)
    bool dietProtSealDirect = false;  // twow-repo#568 D42: vanilla Prot "seal" pushes seal of righteousness directly, seal trigger skips the dead vengeance name (0 = off)
    bool dietFeralNoMangle = false;  // twow-repo#568 D43: vanilla feral nodes push claw / maul instead of the nonexistent mangle (cat)/(bear) (0 = off)
    bool dietFeralNoCatSwipe = false;  // twow-repo#568 D44: vanilla DPS-feral AoE strategy skips the nonexistent "swipe (cat)" node (0 = off)
    bool dietDruidCooldownGates = false;  // twow-repo#568 D45: druid Enrage / Nature's Grasp triggers quiet while the spell is unknown or cooling down (0 = off)
    bool dietLevelingDruidKnownSpells = false;  // twow-repo#568 D46: leveling druid Rejuvenation/Moonfire triggers only once the spell is known (0 = off)
    bool dietHunterBestSting = false;  // twow-repo#568 (D10): one 'no stings usable' node per vanilla spec sting strategy (manual stings keep 'no stings'), it fires only when viper or serpent sting can be used, serpent sting also on mana targets while viper sting is unknown (0 = off)
    bool dietHunterTalentShotsKnown = false;  // twow-repo#568 (D19): deterrence / scatter shot nodes on own triggers that wait for the known, ready talent spell; scatter shot aims at the closest attacker targeting the bot (0 = off)
    bool dietHunterTranqKnown = false;  // twow-repo#568 (D47): the hunter reaction "dispel enrage" -> tranquilizing shot node uses a trigger that waits for the known, ready spell before the aura scan (0 = off)
    bool dietLootRollPending = false;  // twow-repo#568 (D48): the roll strategy pushes "auto loot roll" from "loot roll pending" (every 5 s, only with a group and an open roll) instead of "very often" (0 = off)
    bool perfEventTriggerSkip = false;  // twow-repo#568 (D63): ProcessTriggers skips the Check() of chat-command / world-packet triggers whose event flag is unset; a set flag is handled on the same pass as before (0 = off)
    bool dietHunterAspectGate = false;  // twow-repo#568 (D65): vanilla "aspect of the hawk" trigger fires only while the bot knows Aspect of the Hawk (same "spell id" value its cast checks) (0 = off)
    bool dietHunterRapidFireGate = false;  // twow-repo#568 (D66): vanilla "rapid fire" trigger fires only while Rapid Fire is known and off cooldown, the node drops the "readiness" alternative; both unless the bot knows Readiness (0 = off)
    bool dietWanderSoloGuard = false;  // twow-repo#568 (D67): "wander near" without a follow target fires only on a FOLLOW motion, the only state in which its "stop follow" is useful (0 = off)
    bool dietRpgDeadTriggers = false;  // twow-repo#568 (D68): "rpg ai chat" (LLMEnabled 0), "rpg wander" (no real master) and "rpg quest use" (entry not 190767) answer from their first IsActive condition before the rpg pre-check values (0 = off)
    bool dietDuelTriggerGate = false;  // twow-repo#568 (D69): the duel strategy pushes "attack duel opponent" from "duel no attackers" (only with a duel) instead of "no attackers"; read at engine build (0 = off)
    bool perfPartialPathReuse = false;  // twow-repo#541 (L1): MoveTo uses the path last built for an unchanged destination again, without a rebuild, for up to perfPartialPathReuseMs while its end is not reached (0 = off)
    uint32 perfPartialPathReuseMs = 10000;  // twow-repo#541 (L1): age limit of such a path in ms, clamped to 1000..60000
    uint32 perfRouteFailCooldownSeconds = 0;  // twow-repo#541 (L1b): a node route search without a route blocks the same bot and target cell for that many seconds, clamped to 0..300 (0 = off)
    bool perfRouteTimeTrace = false;  // twow-repo#592 (PATH-02): phase times of every node route build, [RouteTime] once a minute, appended to M:route / [RouteFailDetail]; dispatch_* times the MovePoint path search in DispatchMovement (0 = off, no clock read)
    bool perfDispatchPrecomputedPath = false;  // twow-repo#592 (PATH-02 A20): DispatchMovement hands the built path to MovePoint instead of MovePoint(MOVE_PATHFINDING) + MovePath (0 = as before)
    uint32 perfRoutePrecheck = 0;  // twow-repo#592 (PATH-02): end point pre-check of node route searches, 0 off, 1 shadow (count only), 2 skip the pair loop when no polygon lies within reach of the target, 3 + start check, 4 + direct attempt; clamped 0..4
    uint32 perfRouteFailEarlySkip = 0;  // twow-repo#592 (PATH-02): with perfRouteFailCooldownSeconds, a search that repeats a stored route failure (start and exact target within 1 yd, direct attempt far from its goal) is left out before the direct attempt; 0 off, 1 shadow (count, early_wrong), 2 active; clamped 0..2
    bool perfPathGenUnitless = false;  // twow-repo#592 (PATH-02): in-server node generator and the GetFishLocations route part with unitless pathfinding, only with PathFinder.Unitless = 1, Terrain.Preload.Continents = 1 and complete tiles (0 = a generation that builds node paths is skipped, [PathGen] skipped; one without runs as before)
    bool ahBotRequireConfig = false;  // twow-repo#597 (TICK-01): AhBot::Update returns at once (no thread, no mail query, no line) unless ahbot.conf was loaded and AhBot.Enabled is on (0 = as before: schedules on every tick without a config)
    bool shamanImbueBySpec = false;  // twow-repo#541: shaman weapon imbue per spec/level (ShamanImbuePolicy.h)
    // V1 administrative Apply is accepted only during this explicit
    // no-admission maintenance state and always returns RESTART_REQUIRED.
    bool persistentActiveRosterMaintenanceMode = false;
    // #391: roster login waves after an L1 reset (0 = everyone at once).
    uint32 persistentActiveRosterLoginWaveSize = 0;
    uint32 persistentActiveRosterLoginWaveIntervalSeconds = 300;
    // Disabled by default. When enabled, only the GUIDs admitted by the
    // persistent roster use the quest-first progression policy.
    bool questFirstProgressionEnabled = false;
    // Hotfix 8.9 (twow-repo#474): bot Say/Yell/Party/Guild chat. A queued chat packet was never
    // processed, so bots were silent; on, they speak through the player's chat handler.
    // Off by default - whether bots chat is the owner's decision.
    bool botChatDirect = false;
    uint32 questFirstProgressionAutonomousLogSoftLimit = 16;
    uint32 questFirstProgressionRejectBelowLevelDelta = 4;
    uint32 questFirstProgressionRetireBelowLevelDelta = 6;
    // Autonomous roster offers stay close to the character's actual level.
    // This deliberately does not apply to an explicit player catch-up quest.
    uint32 questFirstProgressionMaxAboveLevelDelta = 1;
    // #335: quest giver/objective locations may lie up to this many levels
    // above the bot (negative = old grind gate).
    int32 questFirstProgressionQuestAreaLevelMargin = 5;
    // #307: any travel destination a roster bot died at this often is skipped
    // for this bot for the cooldown (0 = off).
    uint32 destinationDeathsMax = 2;
    uint32 destinationDeathsCooldownSeconds = 3600;
    // #405: quest objectives whose work phase ran out Max times are skipped.
    uint32 questWorkTimeoutsMax = 0;
    uint32 questWorkTimeoutsCooldownSeconds = 3600;
    uint32 questLoopMaxFailures = 0;             // twow-repo#581 QL-1: failures per bot x quest that skip its objectives (0 = off)
    uint32 questLoopWindowSeconds = 1800;
    uint32 questLoopCooldownSeconds = 1800;
    uint32 questLoopInactiveCooldownSeconds = 0; // twow-repo#581 QL-2: minimum cooldown after an inactive objective (0 = off)
    // twow-repo#485: game-object quest givers and takers spawned only by a script
    // (gameobject.spawntimesecsmin < 0, e.g. GO 270 of quest 310) are no travel targets.
    // false = old behaviour.
    bool questFirstProgressionSkipScriptOnlyQuestTakers = false;
    // G4: evacuate a bot that died DeathLoop.MaxDeaths times within WindowSeconds
    // and Radius yards (0 = off); not reset by XP like "death count".
    uint32 deathLoopMaxDeaths = 0;
    // #357 O-12: route B talent auras for shamans on premade path 7.1 / 7.3.
    bool specAuraEnabled = false;
    // #367 / twow-repo#409 stage 2: classes whose SpecAura talents are real Talent.dbc talents
    // now (same spell IDs). Their auras are neither granted nor removed and cost no reserve.
    std::vector<uint32> specAuraTalentClasses;
    uint32 deathLoopWindowSeconds = 900;
    float deathLoopRadius = 150.0f;
    bool deathLoopEscape = false;   // twow-repo#541 A39 (owner 10.10.): hostile-area death = loop; never evacuate to a hostile homebind
    // #307: a roster bot on its own leaves a zone clearly above its level.
    bool zoneEscapeEnabled = false;
    uint32 zoneEscapeCooldownSeconds = 600;
    uint32 zoneEscapeRetrySeconds = 60;
    // #307: area id -> level for areas missing in ai_playerbot_zone_level.
    std::map<uint32, int32> areaLevelOverrides;
    std::map<uint32, uint32> areaTeamOverrides;  // twow-repo#575 (PATH-01 wave 2): area id -> AreaTable team (2 alliance, 4 horde), empty = off
    // #307: shared danger map. Deaths of all bots (never real players) pooled
    // per cell; roster bots skip destinations and routes through cells where
    // several bots died to mobs clearly above their own level.
    bool dangerMapEnabled = false;
    float dangerMapCellSize = 100.0f;
    uint32 dangerMapWindowSeconds = 7200;
    uint32 dangerMapMinDeaths = 3;
    uint32 dangerMapLevelMargin = 3;
    uint32 dangerMapLineSamples = 20;
    bool dangerMapCountEnvironmentDeaths = false;  // hotfix 8.35 (#544)
    // #307: grind cap for low-level roster bots and per-bot avoidance of killers.
    uint32 grindCapLowLevelBelow = 0;     // 0 = off
    int32 grindCapLowLevelMargin = 2;
    // #421: rescue teleport after this many minutes without quest progress.
    uint32 questRescueIdleMinutes = 30;
    bool questRescueKeepRequestWhenFull = false;  // twow-repo#587: a pending rescue request stays with the bot while the global limit is used up
    uint32 grindAvoidMaxDeaths = 0;       // 0 = off
    uint32 grindAvoidWindowSeconds = 3600;
    uint32 grindAvoidSeconds = 3600;
    // Radius around the bot within which an active quest destination is a
    // local hub candidate. It is spatial, not a hard-coded zone allowlist.
    float questFirstProgressionLocalHubRadius = 1200.0f;
    // Low-volume route-decision diagnostics for persistent quest-first bots.
    // Disabled by default and emitted only by the quest travel request path.
    bool questFirstProgressionTraceTravelDecisions = false;
    // #303: follow-state snapshot for bots of a real-player master ([FollowDiag]),
    // rate-limited per bot. Diagnostic gate before the far-follow fix.
    bool followDiagnostics = false;
    // #365: bot-bot groups (design: twow-repo docs/design/bot-groups.md).
    // Step 1 only reports: Enabled changes no behaviour yet, and MaxBots and
    // LevelWindow are what the [BotGroup] diagnostic classifies a group against.
    bool botGroupsEnabled = false;
    uint32 botGroupsMaxBots = 3;
    uint32 botGroupsLevelWindow = 3;
    bool botGroupsDiagnostics = false;
    // #365 step 2: ad-hoc quest groups.
    uint32 botGroupsAdhocScanIntervalSeconds = 10;
    float botGroupsAdhocRadius = 50.0f;
    uint32 botGroupsAdhocPairCooldownSeconds = 600;
    // twow-repo#628: ad-hoc groups form and leave in the world-thread manager (AdhocGroupWorld.h).
    bool botGroupsAdhocWorldThread = false;
    // twow-repo#627 S7 help groups (QuestGroupPolicy.h, QuestGroupWorld.cpp); Enable: 0 off, 1 all, 2 cohort B, 3 cohort A.
    uint32 questGroupsEnable = 0;
    uint32 questGroupsScanSeconds = 60;
    uint32 questGroupsMaxSize = 5;
    uint32 questGroupsMaxSizeDrop = 3;
    uint32 questGroupsLevelWindow = 3;
    float questGroupsRecruitRadius = 800.0f;
    uint32 questGroupsRendezvousMinutes = 10;
    uint32 questGroupsNoProgressMinutes = 15;
    uint32 questGroupsTurnInWaitMinutes = 5;
    uint32 questGroupsMaxQuestMinutes = 60;
    uint32 questGroupsMaxAttempts = 3;
    uint32 questGroupsRetryCooldownMinutes = 30;
    bool questGroupsSuggestedPlayers = true;
    // #303 part 2: a bot does not walk more than this far to a real-player
    // master (0 = no limit); beyond it the bot waits and asks to be summoned.
    float farFollowMaxWalkDistance = 400.0f;
    // #389 formations v2: slot spacing and caps (owner 2026-09-27).
    float formationSpacing = 2.0f;
    float formationCircleMaxRadius = 15.0f;
    float formationMaxExtent = 30.0f;
    // A completed persistent-roster turn-in is progress-observed rather than
    // bounded by the generic distance-derived travel wall clock.
    uint32 questFirstProgressionTurnInStallSeconds = 300;
    uint32 questFirstProgressionTurnInRouteCooldownSeconds = 120;
    // #329: roster quest objectives use observed progress instead of the
    // generic travel budget while travelling.
    bool questFirstProgressionProgressAwareObjectives = true;
    uint32 questFirstProgressionTurnInMaxDeathsOnRoute = 2;
    uint32 questFirstProgressionGrindFallbackFailures = 0;  // hotfix 8.32 (#544), 0 = off
    uint32 questFirstProgressionGrindFallbackMinutes = 0;   // twow-repo#601: the 8.32 fallback holds N min after the last empty search, 0 = for good
    // twow-repo#601 S1 "latch semantics" (OB-10 17.10.2026), all false = today's behaviour, restart-only
    // (aiplayerbot.conf.dist.in): failure classes + taker pre-validation + grind yield + pre-loop filters.
    bool questFirstProgressionFailureClasses = false;
    // twow-repo#627 S7 hook (OB-10 24.10.2026, own key: FailureClasses = 1 is in the v46.6 bundle): with FailureClasses, an empty
    // offer while every open quest waits for S7 help is group_needed - no grind-fallback count, no park. Default off.
    bool questFirstProgressionFailureClassesGroupNeeded = false;
    // reset of the grind-fallback counter, the route backoff and the search stage on rescue teleport, level-up,
    // login and battleground return; XP alone no longer resets the stage.
    bool questFirstProgressionResetOnProgressEvents = false;
    // "parked twice -> abandon" for a structural reason that cannot heal by itself (no destination, other continent).
    bool questFirstProgressionAbandonStructuralParked = false;
    // S1 review (OB-10 to decide): 0 = a transient rejection never counts toward the grind fallback; N = the N-th
    // transient rejection in a row counts once (acts only with FailureClasses).
    uint32 questFirstProgressionTransientCountsAfter = 0;
    // twow-repo#601 S3b (OB-10 rev. 2, Q-B2; restart-only): the local quest hub pre-loop of SetBestTarget also asks the
    // main loop's post-IsActive deferrals (battleground quest, enemy home zone, route danger, danger map); acts only with
    // FailureClasses. false = the pre-loop as before.
    bool questFirstProgressionHubPreloopSameFilters = false;
    // twow-repo#601 GrindMode (owner 17.10.2026, restart-only): 0 = today's behaviour; 1 = reserved for the S2/S4 gap
    // filler, acts like 0 until then; 2 = no grind as a substitute activity for quest-first roster bots on their own
    // below the max level, they stand idle and log [QuestIdle] (QuestGrindModePolicy.h). Other values act like 0.
    uint32 questFirstProgressionGrindMode = 0;
    // twow-repo#601 R01 v4 cohort mode (rule in ai::r01_cohort): for which bots the S1 / S3 key groups act - 0 or 1 = all
    // (default), 2 = only cohort B (odd low guid), 3 = only cohort A (even); other values = none. Restart-only.
    uint32 questFirstProgressionS1Cohort = 0;
    uint32 questFirstProgressionS3Cohort = 0;
    // out of line in PlayerbotAIConfig.cpp: the cohort headers stay out of the botpch chain (rot_cast_trace contract)
    bool S1For(uint32 guidLow) const;
    bool S3For(uint32 guidLow) const;
    // twow-repo#601 S5a (OB-00 D9 19.10.2026, restart-only for the ladder): band step to the next quest hub of
    // hub-ladder.tsv. 0 = off (default), 1 = all bots, 2 = only odd low guids (cohort B), 3 = only even low guids
    // (cohort A); any other value acts like 0. QuestBandPolicy.h.
    uint32 questFirstProgressionBandLadder = 0;
    // Path of hub-ladder.tsv; "" = ladder off. Read once at boot behind the switch, fail closed (QuestBand.cpp).
    std::string questFirstProgressionBandLadderFile;
    // twow-repo#601 S5a F1 (OB-00 23.10.2026, candidate v46.6 root cause): the band step is also due after this many quest
    // searches in a row without a route, whatever their class; only a found route or a band step resets the streak, a park
    // never does. 0 = off (default: only the EmptyOffer rule), 1..10; other values act like 0.
    uint32 questFirstProgressionBandStepAfterEmptySearches = 0;
    // twow-repo#601 S5a F2: when every rung of the bot's band lies in the zone it stands in, the next band is a candidate
    // from this many levels below its min on (instead of 2). 0 = off (default), 1..10; other values act like 0.
    uint32 questFirstProgressionBandLadderSameZoneNext = 0;
    // twow-repo#601 S5a F4 (OB-00 24.10.2026, owner D1 "rumstehen ist keine Option"): from this many quest searches in a row
    // without a route on (the F1 streak), a search that does not step walks toward the rung the step would take until the
    // route backoff ends. 0 = off (default), 1..10; other values act like 0.
    uint32 questFirstProgressionBandWalkAfterEmptySearches = 0;
    // The band streak is counted when F1 or F4 reads it.
    bool BandStreakOn() const { return questFirstProgressionBandStepAfterEmptySearches || questFirstProgressionBandWalkAfterEmptySearches; }
    // twow-repo#627 S7 hook (OB-10 24.10.2026): a held group quest the bot has waited on for help this many minutes (S7 queue age)
    // counts as a free slot for the band step's log gate; the quest stays in the log. 0 = off (default), 1..240.
    uint32 questFirstProgressionBandQueuedFreeMinutes = 0;
    // twow-repo#601 S3a (feasibility at accept time, OB-10 / OB-00 18.10.2026; QuestFeasibilityPolicy.h), all off = today's
    // behaviour, restart-only: master switch, the two quest id lists (parsed, max 512 ids each) and the solo group rule.
    bool questFeasibilityEnable = false;
    std::set<uint32> questFeasibilityBlocklist;
    std::set<uint32> questFeasibilityBlocklistWhileNoCustomStarts;  // only while customRaceStartZones = false
    bool questFeasibilitySoloSkipGroupQuests = false;  // Type 1 / 81 / 62 or SuggestedPlayers >= 2, bot not in a group
    // twow-repo#594 / P3 (owner 15.10., direct travel targets): master switch, P3-0 trace, fresh bots (level <= FreshMaxLevel)
    bool directTargetsEnabled = false;
    bool directTargetsTraceInactive = false;
    uint32 directTargetsFreshMaxLevel = 5;
    uint32 directTargetsFreshBackoffSeconds = 0;  // 0 = the 8.1 backoff
    uint32 directTargetsFreshGrindFailures = 0;   // 0 = QuestFirstProgression.GrindFallbackFailures
    // twow-repo#594 / P3-b (direct targets, all default 0, built at boot only, each behind directTargetsEnabled): anvil /
    // forge, cooking fire, fishing-hole shore points, dungeon and raid entrances (DirectTargetPolicy.h)
    bool directTargetsStation = false;
    bool directTargetsCooking = false;
    bool directTargetsFishing = false;
    bool directTargetsDungeon = false;
    bool directTargetsRaid = false;
    bool rosterFarMove = false;  // hotfix 8.33 (#544): cheap movement without a player nearby, 0 = off
    float rosterFarMoveArriveBackYards = 60.0f;  // hotfix 8.36 (#544): end the far move this far before the target, 0 = at it
    bool rosterFarMoveWatchRoute = true;  // hotfix 8.36: walk when a real player is near the route
    // twow-repo#595 P4 S0 + B1 (shortcuts by player proximity): all 0 = today's behaviour (ShortcutPolicy.h)
    uint32 shortcutNearScope = 0;  // 0 = off (ShortcutNear answers Unknown), 1 = radius, 2 = same zone or radius
    float shortcutNearRadius = 0.0f;  // yards, 0 = map visibility + ReactDistance
    std::vector<uint32> shortcutTestWatcherGuids;  // TEST ONLY: these bot guids count as real players in the snapshot
    bool shortcutTrace = false;  // [Shortcut] counter lines once a minute, measurement only
    bool shortcutGroupLoopFix = false;  // B1: the legacy random-teleport group loop skips real members, uses the member AI
    bool pathRouteFailDetail = false;  // twow-repo#575 (PATH-01 #1): [RouteFailDetail] line per failed node route (rate-limited), 0 = off
    uint32 pathRouteFailDetailLinesPerMinute = 30;  // PATH-01 #1: detail lines per minute (all failures are counted), clamped 0..600
    bool pathHonestRouteFail = false;  // twow-repo#575 (PATH-01 #3): an empty node route makes MoveTo2 fail instead of reporting a collapsed path, 0 = off
    bool pathPartialNoop = false;  // twow-repo#575 (QFO 41747): a partial path whose end the bot already stands at makes MoveTo2 fail, 0 = off
    float pathPartialNoopReach = 2.0f;  // yards bot -> path end that count as "already there", clamped 0.5..10
    float pathPartialNoopGap = 5.0f;  // yards path end -> target above which the path counts as partial, clamped 1..100
    bool pathStartZoneExactBounds = false;  // twow-repo#575 (PATH-01 #4): Blackstone / Thalassian exclusion uses the zone boxes, 0 = old boxes
    bool pathRpgTargetSnap = false;  // twow-repo#575 (PATH-01 #5): rpg moves go to the navmesh-snapped point around the target, 0 = off
    bool pathPosStuck = false;  // twow-repo#594 (OB-00 23.10.): position guard - a travel target the bot gets no closer to parks, then local unstuck, then home bind, 0 = off
    uint32 pathPosStuckMinutes = 10;  // minutes without net progress to the travel target (fights count in), clamped 2..120
    float pathPosStuckProgress = 5.0f;  // yards closer that count as progress and restart the window, clamped 1..50
    uint32 pathPosStuckEpisodeMinutes = 30;  // an episode within this many minutes of the previous one escalates the series, clamped 5..240
    uint32 pathPosStuckRateMinutes = 30;  // at most one unstuck / home bind per bot in this many minutes, clamped 5..240
    bool pathTaxiTrace = false;  // twow-repo#575 (PATH-01 b): [TaxiUse] / [TransportWait] lines and a minute summary of bot flights and ship waits, 0 = off
    bool pathTransportEdgeTrace = false;  // twow-repo#342 (S1): [TransportWait] state= lines per transport edge episode (attempt / board / arrive / fail) + summary, 0 = off
    bool pathTransportDeckProbe = false;  // twow-repo#342 (S3b-0): once per dock wait 8 fixed vessel deck probes, one [TransportDeck] line (30/min) + [TransportBoard] summary, 0 = off
    uint32 pathTransportWaitTimeoutMin = 15;  // twow-repo#342 (S3b-0): minutes at a dock / aboard before the S1 wait_timeout / overrun, 1-240 (<= 0 -> 15)
    bool pathTaxiCancelForm = false;  // twow-repo#575 (PATH-01 b): cancel a disallowed mount form (travel form, Ghost Wolf) before a flight, 0 = off
    bool pathRescueSkipCustomStarts = false;  // twow-repo#575 (PATH-01 E6): the quest rescue sends only goblins / high elves to their start zones, 0 = off
    bool customRaceStartZones = false;  // twow-repo#575 (PATH-01 wave 0): goblins / high elves quest in their real start zones (StartZonePolicy.h), 0 = off
    bool revivePreferCorpseRun = false;  // hotfix 8.34 (#544): corpse run before the spirit-healer shortcuts
    uint32 questFirstProgressionTurnInDeathRouteCooldownSeconds = 3600;
    // twow-repo#485: a finished quest whose turn-in failed TurnInParkFailures times
    // within TurnInParkWindowSeconds is parked for TurnInParkSeconds (0 = off, the
    // old behaviour; at most 255). On, it also changes every bot's travel retries and
    // finished-quest count (aiplayerbot.conf.dist.in).
    uint32 questFirstProgressionTurnInParkFailures = 0;
    uint32 questFirstProgressionTurnInParkWindowSeconds = 3600;
    uint32 questFirstProgressionTurnInParkSeconds = 3600;
    // twow-repo#485: true = a turn-in-only request whose takers a route danger deferral
    // took (cross map, zone level, death cluster) counts as no route; false = it does not
    // (critic B1.3).
    bool questFirstProgressionTurnInParkCountsRouteDanger = false;
    // #307: quest targets on another continent wait until this level (0 = off).
    uint32 questFirstProgressionMinLevelForCrossMapQuestRoute = 10;
    bool zoneExitTurnInStallCounts = false;  // twow-repo#587 Z4: stage-1 turn-in recoveries count as turn-in failures
    // twow-repo#601 S3c: a TRAVEL stall inside TurnInNearRadius yards of the taker parks the turn-in, and a second work
    // timeout of the same turn-in does too (OB-10 rev. 2). The radius only acts with the switch; 0 = no near zone.
    bool zoneExitTurnInTravelStall = false;
    float zoneExitTurnInNearRadius = 60.0f;
    // twow-repo#587 (ZONE-01): quest rescue on the right continent, stranded bots ask for it, no BG quests as targets.
    bool zoneExitRescueHomeContinent = false;
    uint32 zoneExitStrandedRescueAfter = 0;
    bool zoneExitSkipBattlegroundQuests = false;
    // twow-repo#485: true = only a route to the other continent counts for the
    // rule above; the Deeprun Tram counts as the Eastern Kingdoms and an instance
    // as the continent of its entrance. false = any other map counts (old behaviour).
    bool questFirstProgressionCrossMapContinentsOnly = false;
    uint32 freeRoomForNonSpareBots;
    uint32 loginBotsNearPlayerRange;
    std::vector<std::string> defaultLoginCriteria;
    std::vector<std::vector<std::string>> loginCriteria;

    bool jumpInBg;
    bool jumpWithPlayer;
    bool jumpFollow;
    bool jumpChase;
    bool useKnockback;
    float jumpNoCombatChance;
    float jumpMeleeInCombatChance;
    float jumpRandomChance;
    float jumpInPlaceChance;
    float jumpBackwardChance;
    float jumpHeightLimit;
    float jumpVSpeed;
    float jumpHSpeed;

    std::mutex m_logMtx;

    std::list<std::string> allowedLogFiles;
    std::list<std::string> debugFilter;

    std::unordered_map <std::string, std::pair<FILE*, bool>> logFiles;

    uint32 botCheatMask = 0;
    uint32 rndBotCheatMask = 0;

    std::vector<std::string> BotCheatMaskName = { "taxi", "gold", "health", "mana", "power", "item", "cooldown", "repair", "movespeed", "attackspeed", "breath", "glyph", "quest", "maxMask" };

    struct worldBuff{
        uint32 spellId;
        uint32 factionId = 0;
        uint32 classId = 0;
        uint32 specId = 0;
        uint32 minLevel = 0;
        uint32 maxLevel = 0;
        uint32 eventId = 0;
    };

    std::vector<worldBuff> worldBuffs;

    int commandServerPort;
    bool perfMonEnabled;
    bool bExplicitDbStoreSave = false;

    //LM BEGIN
    std::string llmApiEndpoint, llmApiKey, llmApiJson, llmPrePrompt, llmPreRpgPrompt, llmPrompt, llmPostPrompt, llmResponseStartPattern, llmResponseEndPattern, llmResponseDeletePattern, llmResponseSplitPattern;
    uint32 llmEnabled, llmContextLength, llmBotToBotChatChance, llmGenerationTimeout, llmMaxSimultaniousGenerations, llmRpgAIChatChance;
    uint32 llmDialogueMsPerCharacter, llmDialogueMaxDelayMs;
    bool llmGlobalContext;
    ParsedUrl llmEndPointUrl;
    std::set<uint32> llmBlockedReplyChannels;
    //LM END

    uint32 EatDrinkMinDistance = 5;
    uint32 EatDrinkMaxDistance = 1000;

    std::string GetValue(std::string name);
    void SetValue(std::string name, std::string value);

    void loadFreeAltBotAccounts();

    std::string GetTimestampStr();

    bool hasLog(std::string fileName) { return std::find(allowedLogFiles.begin(), allowedLogFiles.end(), fileName) != allowedLogFiles.end(); };
    bool openLog(std::string fileName, char const* mode = "a", bool haslog = false);
    bool isLogOpen(std::string fileName) { auto it = logFiles.find(fileName); return it != logFiles.end() && it->second.second;}
    // Writes the line verbatim. Nearly every caller here hands over text it has
    // already assembled, and a percent sign anywhere in it - a bot name, a mob
    // name, an item name - used to be read as a conversion specifier. glibc
    // printed nonsense; the Microsoft runtime aborted the server.
    void log(std::string fileName, const char* line);

    // The formatting variant, for the callers that actually pass arguments.
#if defined(__GNUC__) || defined(__clang__)
    void logf(std::string fileName, const char* format, ...)
        __attribute__((format(printf, 3, 4)));
#else
    void logf(std::string fileName, const char* format, ...);
#endif

    void logEvent(PlayerbotAI* ai, std::string eventName, std::string info1 = "", std::string info2 = "");
    void logEvent(PlayerbotAI* ai, std::string eventName, ObjectGuid guid, std::string info2);

    bool CanLogAction(PlayerbotAI* ai, std::string actionName, bool isExecute, std::string lastActionName);

private:
    void LoadTalentSpecs();
    void LoadLLMDefaultPrompts(const std::string& fileName);

    Config config;
};

#define sPlayerbotAIConfig MaNGOS::Singleton<PlayerbotAIConfig>::Instance()

