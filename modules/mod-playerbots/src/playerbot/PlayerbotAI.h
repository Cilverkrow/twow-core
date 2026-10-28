#pragma once
#include "PlayerbotMgr.h"
#include "playerbot/ValueEvictPolicy.h"
#include "playerbot/BotPacketInbox.h"
#include "playerbot/X3bSnapshotPolicy.h"
#include "PlayerbotAIBase.h"
#include "strategy/AiObjectContext.h"
#include "strategy/ReactionEngine.h"
#include "strategy/ExternalEventHelper.h"
#include "ChatFilter.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "BotState.h"
#include "PlayerTalentSpec.h"
#include <stack>
#include "strategy/IterateItemsMask.h"
#include "RandomPlayerbotMgr.h"

#include "playerbot/BotSlots.h"
#include "playerbot/DeathLoopPolicy.h"
#include "playerbot/DeathAddsPolicy.h"
#include "playerbot/CastWaitPolicy.h"
#include "playerbot/TeleportCastPolicy.h"
#include "playerbot/BotSavePolicy.h"
#include "playerbot/StallGuardPolicy.h"
#include <atomic>
#include <functional>
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/QuestFeasibilityPolicy.h"
#include "playerbot/BotActivityPolicy.h"
#include "playerbot/FarMovePolicy.h"
#include "playerbot/GatherPurposePolicy.h"
#include "playerbot/StuckCombatPolicy.h"
#include "playerbot/FishingPolicy.h"
#include "playerbot/ItemUseTrace.h"
#include "playerbot/DeathSeriesPolicy.h"
#include "playerbot/TankPathDiagPolicy.h"
#include "playerbot/UnreachablePolicy.h"
#include "playerbot/GroupAiRefresh.h"   // twow-repo#627 S7 StagedOps: RequestQuestForce
#include "playerbot/GroupBuffPolicy.h"
class Player;
class PlayerbotMgr;
class ChatHandler;
class Channel;

using namespace ai;

bool IsAlliance(uint8 race);

class PlayerbotChatHandler: protected ChatHandler
{
public:
    explicit PlayerbotChatHandler(Player* pMasterPlayer) : ChatHandler(pMasterPlayer->GetSession()) {}
    void sysmessage(std::string str) { SendSysMessage(str.c_str()); }
    uint32 extractQuestId(std::string str);
    uint32 extractSpellId(std::string str)
    {
        char* source = (char*)str.c_str();
        return ExtractSpellIdFromLink(&source);
    }
};

class ChannelAcces
{
public:    
    struct PlayerInfo
    {
        ObjectGuid player;
        uint8 flags;       
    };

    typedef std::map<ObjectGuid, PlayerInfo> PlayerList;

    bool IsOn(ObjectGuid who) const { return m_players.find(who) != m_players.end(); }    
    std::string                 m_name;
    std::string                 m_password;
    ObjectGuid                  m_ownerGuid;
    PlayerList                  m_players;
    GuidSet                     m_banned;
    const ChatChannelsEntry* m_entry = nullptr;
    bool                        m_announcements = false;
    bool                        m_moderation = false;
    uint8                       m_flags = 0x00;
    // Custom features:
    bool                        m_static = false;
    bool                        m_realmzone = false;
};

namespace ai
{
    class WorldPosition;
    class GuidPosition;
    class IterateItemsVisitor;
    class FindItemVisitor;

	class MinValueCalculator {
	public:
		MinValueCalculator(float def = 0.0f) {
			param = NULL;
			minValue = def;
		}

	public:
		void probe(float value, void* p) {
			if (!param || minValue >= value) {
				minValue = value;
				param = p;
			}
		}

	public:
		void* param;
		float minValue;
	};
};

enum ImportantAreaId
{
    CITY = 3459
};

enum ChatChannelId
{
    GENERAL = 1,
    TRADE = 2,
    LOCAL_DEFENSE = 22,
    WORLD_DEFENSE = 23,
#ifdef MANGOSBOT_ZERO
    //Yes, for 1.12 it is 24
    LOOKING_FOR_GROUP = 24,
#else
    LOOKING_FOR_GROUP = 26,
#endif
    GUILD_RECRUITMENT = 25,
};

enum ChatChannelSource
{
    SRC_GUILD,
    SRC_WORLD,
    SRC_GENERAL,
    SRC_TRADE,
    SRC_LOOKING_FOR_GROUP,
    SRC_LOCAL_DEFENSE,
    SRC_WORLD_DEFENSE,
    SRC_GUILD_RECRUITMENT,

    SRC_SAY,
    SRC_WHISPER,
    SRC_EMOTE,
    SRC_TEXT_EMOTE,
    SRC_YELL,

    SRC_PARTY,
    SRC_RAID,

    SRC_UNDEFINED
};

enum HealingItemDisplayId
{
   HEALTHSTONE_DISPLAYID = 8026,
   MAJOR_HEALING_POTION = 24152,
   WHIPPER_ROOT_TUBER = 21974,
   NIGHT_DRAGON_BREATH = 21975,
   LIMITED_INVULNERABILITY_POTION = 24213,
   GREATER_DREAMLESS_SLEEP_POTION = 17403,
   SUPERIOR_HEALING_POTION = 15714,
   CRYSTAL_RESTORE = 2516,
   DREAMLESS_SLEEP_POTION = 17403,
   GREATER_HEALING_POTION = 15713,
   HEALING_POTION = 15712,
   LESSER_HEALING_POTION = 15711,
   DISCOLORED_HEALING_POTION = 15736,
   MINOR_HEALING_POTION = 15710,
   VOLATILE_HEALING_POTION = 24212,
   SUPER_HEALING_POTION = 37807,
   CRYSTAL_HEALING_POTION = 47132,
   FEL_REGENERATION_POTION = 37864,
   MAJOR_DREAMLESS_SLEEP_POTION = 37845,
};

enum RoguePoisonDisplayId
{
   DEADLY_POISON_DISPLAYID = 13707,
   CRIPPLING_POISON_DISPLAYID = 13708,
   CRIPPLING_POISON_DISPLAYID_II = 2947,
   MIND_POISON_DISPLAYID = 13709,
   INSTANT_POISON_DISPLAYID = 13710,
#ifdef MANGOSBOT_ZERO
   WOUND_POISON_DISPLAYID = 13708
#else
   WOUND_POISON_DISPLAYID = 37278
#endif
};

enum SharpeningStoneDisplayId
{
   ROUGH_SHARPENING_DISPLAYID = 24673,
   COARSE_SHARPENING_DISPLAYID = 24674,
   HEAVY_SHARPENING_DISPLAYID = 24675,
   SOLID_SHARPENING_DISPLAYID = 24676,
   DENSE_SHARPENING_DISPLAYID = 24677,
   CONSECRATED_SHARPENING_DISPLAYID = 24674,    // will not be used because bot can not know if it will face undead targets
   ELEMENTAL_SHARPENING_DISPLAYID = 21072,
   FEL_SHARPENING_DISPLAYID = 39192,
   ADAMANTITE_SHARPENING_DISPLAYID = 39193,
};

enum WeightStoneDisplayId
{
   ROUGH_WEIGHTSTONE_DISPLAYID = 24683,
   COARSE_WEIGHTSTONE_DISPLAYID = 24684,
   HEAVY_WEIGHTSTONE_DISPLAYID = 24685,
   SOLID_WEIGHTSTONE_DISPLAYID = 24686,
   DENSE_WEIGHTSTONE_DISPLAYID = 24687,
   FEL_WEIGHTSTONE_DISPLAYID = 39548,
   ADAMANTITE_WEIGHTSTONE_DISPLAYID = 39549,
};

#ifdef MANGOSBOT_ZERO
// m_zero
enum WizardOilDisplayId
{
    MINOR_WIZARD_OIL = 33194,
    LESSER_WIZARD_OIL = 33450,
    BRILLIANT_WIZARD_OIL = 33452,
    WIZARD_OIL = 33451,
    SUPERIOR_WIZARD_OIL = 47904,
    /// Blessed Wizard Oil = 26865,//scourge inv
};
// m_zero
enum ManaOilDisplayId
{
    MINOR_MANA_OIL = 33453,
    LESSER_MANA_OIL = 33454,
    BRILLIANT_MANA_OIL = 33455,
    SUPERIOR_MANA_OIL = 36862,
};
#else
enum WizardOilDisplayId
{
   MINOR_WIZARD_OIL     = 9731,
   LESSER_WIZARD_OIL    = 47903,
   BRILLIANT_WIZARD_OIL = 47901,
   WIZARD_OIL           = 47905,
   SUPERIOR_WIZARD_OIL  = 47904,
  /// Blessed Wizard Oil = 26865,//scourge inv
};

enum ManaOilDisplayId
{
   MINOR_MANA_OIL       = 34492,
   LESSER_MANA_OIL      = 47902,
   BRILLIANT_MANA_OIL   = 41488,
   SUPERIOR_MANA_OIL    = 36862,
};
#endif

enum ShieldWardDisplayId
{
   LESSER_WARD_OFSHIELDING = 38759,
   GREATER_WARD_OFSHIELDING = 38760,
};

enum class BotTypeNumber : uint8
{
    ACTIVITY_TYPE_NUMBER = 1,
    GROUPER_TYPE_NUMBER = 2,
    GUILDER_TYPE_NUMBER = 3,
    CHATFILTER_NUMBER = 4 ,
    DUMMY_ATTACK_NUMBER = 5, 
    RPG_PHASE_NUMBER = 6,
    RPG_STYLE_NUMBER = 7,
    WORLD_PVP_LOCATION = 8
};

enum class GrouperType : uint8
{
    SOLO = 0,
    MEMBER = 1,
    LEADER_2 = 2,
    LEADER_3 = 3,
    LEADER_4 = 4,
    LEADER_5 = 5,
#ifndef MANGOSBOT_ZERO
    RAIDER_10 = 10,
    RAIDER_MAX = 25
#else
    RAIDER_20 = 20,
    RAIDER_MAX = 40
#endif
};

enum class GuilderType : uint8
{
    SOLO = 0,
    TINY = 30,
    SMALL = 50,
    MEDIUM = 70,
    LARGE = 120,
    MASSIVE = 250
};

enum class ActivePiorityType : uint8
{
    IS_REAL_PLAYER = 0,
    HAS_REAL_PLAYER_MASTER = 1,
    IN_GROUP_WITH_REAL_PLAYER,
    IS_RUNNING_TEST,
    IN_BATTLEGROUND,
    IN_INSTANCE,
    VISIBLE_FOR_PLAYER,
    IS_ALWAYS_ACTIVE,
    IN_COMBAT,
    IN_BG_QUEUE,
    IN_LFG,
    NEARBY_PLAYER,
    PLAYER_FRIEND,
    PLAYER_GUILD,
    NO_PATH,
    IN_ACTIVE_AREA,
    IN_ACTIVE_MAP,
    IN_INACTIVE_MAP,
    IN_EMPTY_SERVER,
    MAX_TYPE
};

enum ActivityType
{
    GRIND_ACTIVITY = 1,
    RPG_ACTIVITY = 2,
    TRAVEL_ACTIVITY = 3,
    OUT_OF_PARTY_ACTIVITY = 4,
    PACKET_ACTIVITY = 5,
    DETAILED_MOVE_ACTIVITY = 6,
    PARTY_ACTIVITY = 7,
    REACT_ACTIVITY = 8,
    ALL_ACTIVITY = 9,
    MAX_ACTIVITY_TYPE
};

class PacketHandlingHelper
{
public:
    void AddHandler(uint16 opcode, std::string handler, bool shouldDelay = false);
    void Handle(ExternalEventHelper &helper);
    void AddPacket(const WorldPacket& packet);
    // twow-repo#563: handlers are only added in the PlayerbotAI constructor, so any thread may read.
    bool HasHandler(uint16 opcode) const { return handlers.find(opcode) != handlers.end(); }

private:
    std::map<uint16, std::string> handlers;
    std::map<uint16, bool> delay;
    // Penqle WorldPacket is move-only; stack of values fails copy-assign.
    // Use unique_ptr to keep the stack copyable-by-value-of-element.
    std::stack<std::unique_ptr<WorldPacket>> queue;
    std::mutex m_botPacketMutex;

public:
    // Hotfix 8.1: queued packets for [MemStores].
    size_t QueueSize() { std::lock_guard<std::mutex> lock(m_botPacketMutex); return queue.size(); }
};

class ChatCommandHolder
{
public:
    ChatCommandHolder(std::string command, Player* owner = NULL, uint32 type = CHAT_MSG_WHISPER, time_t time = 0) : command(command), owner(owner), type(type), time(time) {}
    ChatCommandHolder(ChatCommandHolder const& other)
    {
        this->command = other.command;
        this->owner = other.owner;
        this->type = other.type;
        this->time = other.time;
    }

public:
    std::string GetCommand() { return command; }
    Player* GetOwner() { return owner; }
    uint32 GetType() { return type; }
    time_t GetTime() { return time; }

private:
    std::string command;
    Player* owner;
    uint32 type;
    time_t time;
};

class PlayerbotAI : public PlayerbotAIBase
{
public:
	PlayerbotAI();
	PlayerbotAI(Player* bot);
	virtual ~PlayerbotAI();

    virtual void UpdateAI(uint32 elapsed, bool minimal = false);

    void HandleCommands();
    // Audit A29 (twow-repo#563): chat reaches HandleCommand on the AsyncPackets thread while the bot's
    // own UpdateAI drains the queue; every access goes through chatCommandsMutex.
    void QueueChatCommand(ChatCommandHolder const& cmd);
private:
    void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
    bool AllowDelayJitter() const override;
    // twow-repo#541 (audit A17): idle auto-attack combat pass -> longer wait (AiPlayerbot.Perf.CombatIdleYield).
    void StretchCombatIdle();
    // twow-repo#602 ROT-01 3c-B: idle out-of-mana combat pass -> wait DelayMs (AiPlayerbot.Rotation.OomBackoff).
    void BackoffOutOfMana(bool yieldSetDelay, bool minimal);
public:    
    static std::string BotStateToString(BotState state);
    std::string GetDefaultMovementStrategy();
    void EnsureDefaultMovementStrategy(Player* requester = nullptr);
	std::string HandleRemoteCommand(std::string command);
    void HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, const uint32 lang = LANG_UNIVERSAL);
    void QueueChatResponse(uint32 msgType, ObjectGuid guid1, ObjectGuid guid2, std::string message, std::string chanName, std::string name, bool noDelay = false);
	void HandleBotOutgoingPacket(const WorldPacket& packet);
    // twow-repo#563 (X1): any thread may queue a packet for this bot; UpdateAI handles it on the bot's thread.
    void QueueBotOutgoingPacket(const WorldPacket& packet);
    bool WantsBotOutgoingPacket(uint16 opcode) const;
    void HandleQueuedBotPackets();
    // twow-repo#541 A47 (AiPlayerbot.BotSave.IntervalSeconds): own save schedule on the bot's map thread.
    void UpdateBotSave();
    // twow-repo#563 (X3c, AiPlayerbot.X3c.InboxWrites): a value write another bot wants on this bot's context;
    // applied by this bot in its own UpdateAI (same drain as the packets).
    void QueueContextWrite(std::function<void(ai::AiObjectContext*)> write);
    // For the callers: queues on the target's AI, or counts the write as dropped when the target has none.
    static void QueueContextWriteTo(Player* target, std::function<void(ai::AiObjectContext*)> write);
    // twow-repo#563 (X3b variant 2, AiPlayerbot.X3b.PublishedTargets): the owner publishes, other bots read a
    // copy of the pointer - never this bot's Value objects. The mutex only guards the pointer swap.
    typedef ai::x3b::PublishedTargets<ObjectGuid> PublishedTargets;
    void PublishTargets(std::shared_ptr<PublishedTargets const> snapshot);
    std::shared_ptr<PublishedTargets const> GetPublishedTargets() const;
    // X3b site 2 (AiPlayerbot.X3b.PublishedConditions): conditions other bots ask about this bot, answered by
    // this bot on its own thread (HandleQueuedBotPackets).
    ai::x3b::ConditionBoard& GetConditionBoard() { return conditionBoard; }
    // X3b site 3 (same switch): "follow" or "wander" in the non-combat engine, published by this bot on its own
    // thread for GroupReadyValue of the others (true until the first publish: the group waits, as for a follower).
    bool PublishedFollowing() const { return publishedFollowing.load(std::memory_order_relaxed); }
    void HandleMasterIncomingPacket(const WorldPacket& packet);
    void HandleMasterOutgoingPacket(const WorldPacket& packet);
	void HandleTeleportAck();
    void ChangeEngine(BotState type);
    void DoNextAction(bool minimal = false);
    bool CanDoSpecificAction(const std::string& name, bool isUseful = true, bool isPossible = true);
    virtual bool DoSpecificAction(const std::string& name, Event event = Event(), bool silent = false);
    void ChangeStrategy(const std::string& name, BotState type);
    void PrintStrategies(Player* requester, BotState type);
    void ClearStrategies(BotState type);
    std::list<std::string_view> GetStrategies(BotState type);
    bool ContainsStrategy(StrategyType type);
    bool HasStrategy(const std::string& name, BotState type);
    template<class T>
    T* GetStrategy(const std::string& name, BotState type);
    BotState GetState() { return currentState; };
    void ResetStrategies(bool autoLoad = true);
    void ReInitCurrentEngine();
    void Reset(bool full = false);
    static bool IsTank(Player* player, bool inGroup = true);
    static bool IsHeal(Player* player, bool inGroup = true);
    bool IsRanged(Player* player, bool inGroup = true);
    bool IsMelee(Player* player, bool inGroup = true);
    Creature* GetCreature(ObjectGuid guid) const;
    Creature* GetAnyTypeCreature(ObjectGuid guid) const;
    Unit* GetUnit(ObjectGuid guid);
    static Unit* GetUnit(CreatureDataPair const* creatureDataPair);
    GameObject* GetGameObject(ObjectGuid guid);
    static GameObject* GetGameObject(GameObjectDataPair const* gameObjectDataPair);
    WorldObject* GetWorldObject(ObjectGuid guid);
    std::vector<Player*> GetPlayersInGroup();
    // Hotfix 8.15: reason goes to the [QuestDrop] line (the QuestDropped CSV event keeps its format).
    void DropQuest(uint32 questId, char const* reason = "other");
    std::vector<const Quest*> GetAllCurrentQuests();
    std::vector<const Quest*> GetCurrentIncompleteQuests();
    std::set<uint32> GetAllCurrentQuestIds();
    std::set<uint32> GetCurrentIncompleteQuestIds();
    const Quest* GetCurrentIncompleteQuestWithId(uint32 questId);
    bool HasCurrentIncompleteQuestWithId(uint32 questId);
    std::vector<std::pair<const Quest*, uint32>> GetCurrentQuestsRequiringItemId(uint32 itemId);
    const AreaTableEntry* GetCurrentArea();
    const AreaTableEntry* GetCurrentZone();
    std::string GetLocalizedAreaName(const AreaTableEntry* entry);
    bool IsInCapitalCity();
    ChatChannelSource GetChatChannelSource(Player* bot, uint32 type, std::string channelName);
    bool SayToGuild(std::string msg, bool likePlayer = false);
    bool SayToWorld(std::string msg);
    // twow-repo#517 stage c: whether this bot is on the channel (Channel::IsOn is private; PlayerbotAI is its friend).
    bool IsOnChannel(Channel const* channel) const;
    bool SayToGeneral(std::string msg);
    bool SayToTrade(std::string msg);
    bool SayToLFG(std::string msg);
    bool SayToLocalDefense(std::string msg);
    bool SayToWorldDefense(std::string msg);
    bool SayToGuildRecruitment(std::string msg);
    bool SayToParty(std::string msg, bool likePlayer = false);
    bool SayToRaid(std::string msg);
    bool Yell(std::string msg, bool likePlayer = false);
    bool Say(std::string msg, bool likePlayer = false);
    bool Whisper(std::string msg, std::string receiverName, bool likePlayer = false);
    bool TellPlayer(Player* player, std::ostringstream &stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false) { return TellPlayer(player, stream.str(), securityLevel, isPrivate, ignoreSilent); }
    bool TellPlayer(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false);
    bool TellPlayerNoFacing(Player* player, std::ostringstream& stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false) { return TellPlayerNoFacing(player, stream.str(), securityLevel, isPrivate, noRepeat, ignoreSilent); }
    bool TellPlayerNoFacing(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false);
    bool TellDebug(Player* player, std::string text, std::string strategy = "debug", BotState state = BotState::BOT_STATE_NON_COMBAT){ if (HasStrategy(strategy, state)) return TellPlayerNoFacing(player, text, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false); return false;}
    bool TellError(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool ignoreSilent = false);
    void SpellInterrupted(uint32 spellid);
    // twow-repo#333 finding 4 (TeleportCastPolicy.h): watch a hold of holdMs for the bot's own PREPARING teleport / hearthstone
    // (0 = nothing prepared: stop watching); UpdateAI ends the hold when the spell is no longer PREPARING.
    void WatchTeleportHold(uint32 holdMs);
    int32 CalculateGlobalCooldown(uint32 spellid);
    void InterruptSpell(bool withMeleeAndAuto = true);
    bool RemoveAura(const std::string& name);
    void RemoveShapeshift();
    void WaitForSpellCast(Spell *spell);
    bool PlaySound(uint32 emote);
    bool PlayEmote(uint32 emote);
    bool PlayAttackEmote(float chanceDivider);
    void Ping(float x, float y);
    void Poi(float x, float y, std::string icon_name = "This way", Player* player = nullptr, uint32 flags = 99, uint32 icon = 6 /* red flag */, uint32 icon_data = 0);
    Item * FindPoison() const;
    Item * FindConsumable(uint32 displayId) const;
    Item * FindBandage() const;
    Item* FindStoneFor(Item* weapon) const;
    Item* FindOilFor(Item* weapon) const;
#ifdef MANGOSBOT_ZERO
    void ImbueItem(Item* item, uint16 targetFlag, ObjectGuid targetGUID);
#else
    void ImbueItem(Item* item, uint32 targetFlag, ObjectGuid targetGUID);
#endif
    void ImbueItem(Item* item, uint8 targetInventorySlot);
    void ImbueItem(Item* item, Unit* target);
    void ImbueItem(Item* item);
    void EnchantItemT(uint32 spellid, uint8 slot, Item* item = nullptr);
    uint32 GetBuffedCount(Player* player, std::string spellname);

    bool GetSpellRange(std::string name, float* maxRange, float* minRange = nullptr);
    uint32 GetSpellCastDuration(Spell* spell);

    virtual bool HasAura(std::string spellName, Unit* player, bool maxStack = false, bool checkIsOwner = false, int maxAmount = -1, bool hasMyAura = false, int minDuration = 0, int auraTypeId = TOTAL_AURAS);
    virtual bool HasAnyAuraOf(Unit* player, ...);
    virtual bool HasMyAura(std::string spellName, Unit* player) { return HasAura(spellName, player, false, false, -1, true); }
    uint8 GetHealthPercent(const Unit& target) const;
    uint8 GetHealthPercent() const;
    uint8 GetManaPercent(const Unit& target) const;
    uint8 GetManaPercent() const;

    virtual bool IsInterruptableSpellCasting(Unit* player, std::string spell, uint8 effectMask);
    virtual bool HasAuraToDispel(Unit* player, uint32 dispelType);
    bool canDispel(const SpellEntry* entry, uint32 dispelType);
    static bool IsHealSpell(const SpellEntry* entry);

    static bool IsMiningNode(const GameObject* go);
    static bool IsHerb(const GameObject* go);

    bool HasSpell(std::string name) const;
    bool HasSpell(uint32 spellid) const;
    bool HasAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(std::string spellName, Unit* player, bool checkOwner = false);
    std::vector<Aura*> GetAuras(Unit* player, bool allAuras = true, bool positive = false);

    bool HasSpellItems(uint32 spellId, const Item* castItem) const;
    void DurabilityLoss(Item* item, double percent);

    SpellCastResult CheckSpellTargetAlignment(SpellEntry const* spellInfo, Unit* target);

    virtual bool CanCastSpell(std::string name, Unit* target, uint8 effectMask, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, GameObject* goTarget, uint8 effectMask, bool checkHasSpell = true, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, float x, float y, float z, uint8 effectMask, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastVehicleSpell(uint32 spellid, Unit* target);

    virtual bool CastSpell(std::string name, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, GameObject* goTarget, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, float x, float y, float z, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastPetSpell(uint32 spellId, Unit* target);
    bool CastVehicleSpell(uint32 spellId, Unit* target, float projectileSpeed, bool needTurn);
    bool CastVehicleSpell(uint32 spellId, float x, float y, float z);

    bool IsInVehicle(bool canControl = false, bool canCast = false, bool canAttack = false, bool canTurn = false, bool fixed = false, std::string vehicleName = "");

    uint32 GetEquipGearScore(Player* player, bool withBags, bool withBank);
    // mod-playerbots short forms.
    uint32 GetEquipGearScore(Player* player) { return GetEquipGearScore(player, false, false); }
    std::vector<Player*> GetRealPlayersInGroup();
    // mod-playerbots short forms: the error goes to the master, the cast
    // check uses effect mask 0 (any effect).
    bool TellError(std::string text) { return TellError(GetMaster(), text); }
    bool CanCastSpell(std::string name, Unit* target) { return CanCastSpell(name, target, 0); }
    uint32 GetEquipStatsValue(Player* player);
    bool HasSkill(SkillType skill);
    bool IsAllowedCommand(std::string text);
    float GetRange(std::string type);

    static ReputationRank GetFactionReaction(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate);
    // cmangos uses sFactionTemplateStore.LookupEntry(N); Penqle uses sObjectMgr.GetFactionTemplateEntry(N).
    static bool friendToAlliance(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sObjectMgr.GetFactionTemplateEntry(1)) >= REP_NEUTRAL; }
    static bool friendToHorde(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sObjectMgr.GetFactionTemplateEntry(2)) >= REP_NEUTRAL; }
    bool IsFriendlyTo(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(bot->GetFactionTemplateEntry(), templateEntry) >= REP_NEUTRAL; }
    bool IsFriendlyTo(uint32 faction) { return GetFactionReaction(bot->GetFactionTemplateEntry(), sObjectMgr.GetFactionTemplateEntry(faction)) >= REP_NEUTRAL; }
    static bool AddAura(Unit* unit, uint32 spellId);
    ReputationRank getReaction(FactionTemplateEntry const* factionTemplate) { return GetFactionReaction(bot->GetFactionTemplateEntry(), factionTemplate);}

    void InventoryIterateItems(IterateItemsVisitor* visitor, IterateItemsMask mask);
    void InventoryTellItems(Player* player, std::map<uint32, int> items, std::map<uint32, bool> soulbound);
    void InventoryTellItem(Player* player, ItemPrototype const* proto, int count, bool soulbound);
    std::list<Item*> InventoryParseItems(std::string text, IterateItemsMask mask);
    uint32 InventoryGetItemCount(FindItemVisitor* visitor, IterateItemsMask mask);
    std::string InventoryParseOutfitName(std::string outfit);
    ItemIds InventoryParseOutfitItems(std::string outfit);
    ItemIds InventoryFindOutfitItems(std::string name);

    void AccelerateRespawn(Creature* creature, float accelMod = 0);
    void AccelerateRespawn(ObjectGuid guid, float accelMod = 0) { Creature* creature = GetCreature(guid); if (creature) AccelerateRespawn(creature,accelMod); }

    std::list<Unit*> GetAllHostileUnitsAroundWO(WorldObject* wo, float distanceAround);
    std::list<Unit*> GetAllHostileNPCNonPetUnitsAroundWO(WorldObject* wo, float distanceAround);

    // LLM-012: these used to spawn a detached std::thread that captured a raw
    // WorldSession*/PlayerbotAI* and, after blocking on the future (plus a
    // std::this_thread::sleep_for), dereferenced it. Nothing kept the bot's
    // session (or this AI) alive for the life of that thread: logout deletes
    // the WorldSession on the world thread (World.cpp RemoveSession), and
    // shutdown deletes it too, so the detached thread could dereference freed
    // memory. Worse, futurePacket.get() rethrows whatever the async LLM/HTTP
    // task threw, and an exception escaping a detached thread's entry
    // function calls std::terminate() -- taking down the whole worldserver,
    // not just the bot. Neither function may capture bot/session pointers or
    // run off the world thread anymore: SendDelayedPacket()/ReceiveDelayedPacket()
    // now just stash the future, and UpdateDelayedPackets() (called from this
    // bot's own UpdateAIInternal tick, i.e. the world thread) polls it and
    // re-resolves bot->GetSession() at delivery time, checking it is still
    // non-null before touching it.
    void SendDelayedPacket(std::future<std::vector<std::pair<WorldPacket, uint32>>> futurePacket);
    void ReceiveDelayedPacket(std::future<std::vector<std::pair<WorldPacket, uint32>>> futurePacket);
 public:
    std::vector<Bag*> GetEquippedAnyBags();
    std::vector<Bag*> GetEquippedQuivers();
    std::vector<Item*> GetInventoryAndEquippedItems();
    std::vector<Item*> GetInventoryItems();
    uint32 GetInventoryItemsCountWithId(uint32 itemId);
    bool HasItemInInventory(uint32 itemId);
    bool HasNotFullStacksInBagsForLootItems(LootItemList &questLootItemList);
    bool HasQuestItemsInLootList(LootItemList &questLootItemList);
    bool HasQuestItemsInWOLootList(WorldObject* wo);
    bool CanLootSomethingFromWO(WorldObject* wo);
    PlayerbotHolder* GetHolder() const;
private:
    void InventoryIterateItemsInBags(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInEquip(IterateItemsVisitor* visitor);   
    void InventoryIterateItemsInBank(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInBuyBack(IterateItemsVisitor* visitor);

private:
    void _fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore);
    bool IsTellAllowed(Player* player, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL);

public:
	Player* GetBot() { return bot; }
    Player* GetMaster() { return master; }

    // accessor for the active engine so
    // cpp can build heartbeat / debug payloads without being
    // a friend class. Read-only.
    Engine* GetCurrentEngine() { return currentEngine; }
    // Read-only per-state access for module diagnostics (mod-dungeon-clear's
    // strategy gate dumps what an engine actually carries when HasStrategy
    // disagrees with observed behavior).
    Engine* GetEngine(BotState type) { return engines[(uint8)type]; }

    // Heartbeat-cadence accumulator used by ::TickHeartbeat.
    // Public so the helper can advance it in-place every UpdateAI tick;
    // there's no behavior risk since it's a pure accumulator (no class
    // invariant tied to its value beyond "nonnegative").
    uint32 scboteHeartbeatAcc = 0;

    // One-shot "needs level/gear sync" flag. Set by OnBotSummoned
    // when the bot logs in; cleared by TickHeartbeat after the sync runs.
    // Reason for the deferral: at OnBotLogin time the bot's master link is
    // not yet established (cmangos sets it later in the .bot create flow),
    // so the sync code can't tell what level to match. We watch from the
    // tick path and run once master becomes available.
    bool scboteLevelSyncPending = false;

    // One-shot "teleport bot to master on first available tick" flag. Set
    // by OnBotSummoned; cleared by TickHeartbeat after the teleport runs.
    // Same deferral rationale as scboteLevelSyncPending: master link is
    // typically not set at OnBotLogin time. Without this, bots summoned
    // via `.bot add` come online at their last logout position — often a
    // different continent than the master, so they can't be invited to
    // group or interacted with normally. .
    bool scboteTeleportPending = false;

    // Last raid-encounter creature ID we applied strategies for. Used by
    // TickEncounter to detect target-change transitions
    // and add/remove the per-boss strategy bundles. 0 means "no encounter
    // strategies applied". A non-zero value that doesn't match the current
    // target's creature_id triggers a strategy reset for the new target.
    uint32 scboteLastEncounterCreatureId = 0;

    // BotActionLog: track last combat state so we only log STATE snapshots
    // on actual combat-state transitions (combat-enter / combat-exit).
    // Without this we'd spam the log with "still in combat" lines every
    // tick. -1 means "uninitialized" so the first observation always logs.
    int scboteLastCombatLogged = -1;

    //Checks if the bot is really a player. Players always have themselves as master.
    //
    // Sentinel comparison: bot sessions are tagged as either "disconnected/bot"
    // (the historical cmangos sentinel — what we pass to the WorldSession ctor)
    // or "<BOT>" (what `WorldSession::WorldSession` actually overwrites the
    // address to when sock==null at line 101 of WorldSession.cpp, regardless
    // of what remote_ip we pass). We must check BOTH because the ctor's
    // null-socket branch ignores the remote_ip parameter. Without the second
    // check, every bot was misclassified as a real player → HandleTeleportAck
    // early-return → cross-map far-teleport never completes → bot stuck mid-
    // teleport forever (every a bot / cross-continent bot). See     // 2026-05-07 root-cause investigation.
    bool IsRealPlayer()
    {
        std::string const& addr = bot->GetSession()->GetRemoteAddress();
        return addr != "disconnected/bot" && addr != "<BOT>";
    }
    bool IsRealPlayer(Unit* unit)
    {
        if (!unit->IsPlayer()) return false;
        std::string const& addr = ((Player*)unit)->GetSession()->GetRemoteAddress();
        return addr != "disconnected/bot" && addr != "<BOT>";
    }
    bool IsSelfMaster() { return master ? (master == bot) : false; }
    //Bot has a master that is a player.
    bool HasRealPlayerMaster() { return master && (!GetBotAI(master) || GetBotAI(master)->IsRealPlayer()); } 
    //Bot has a master that is actively playing.
    bool HasActivePlayerMaster() const { return master && !GetBotAI(master); }
    // G4: deaths close in time and place, not reset by XP (DeathLoopPolicy.h).
    void RecordDeathForLoop();
    bool IsInDeathLoop() const;
    // #422: death series (any place) and deaths without a killer while gathering.
    void RecordDeathForSeries();
    // Hotfix 8.1: [TankPath] diagnostic for bots on a tank path (4.3 / 7.3).
    void UpdateTankPathDiag(uint32 now);
    // twow-repo#575 (PATH-01 F4, AiPlayerbot.CustomRaceStartZones): the walk to the island machine along the travel graph;
    // false = no graph walk (the caller walks straight as before).
    bool WalkToMachineByGraph(float x, float y, float z, uint32 now);
    bool IsCautious() const { return deathSeries.Cautious(uint32(time(nullptr))); }
    // Hotfix 8.5: the declared profession purpose ([Purpose]).
    ai::gather_purpose::State& GetGatherPurpose() { return gatherPurpose; }
    // Hotfix 8.7 (twow-repo#472): [Fishing] counters of the running fishing purpose.
    ai::fishing::PurposeTrace& GetFishingTrace() { return fishingTrace; }
    // Hotfix 8.9 (twow-repo#474): [ItemUse] counters.
    ai::item_use::Trace& GetItemUseTrace() { return itemUseTrace; }
    bool IsGatherPurposeSuppressed(uint32 purpose) const { return gatherDeaths.Suppressed(purpose, uint32(time(nullptr))); }
    void ClearDeathLoop() { recentDeaths.clear(); lastDeathHostileArea = false; shortLife = ai::death_loop::ShortLife(); }
    // twow-repo#601/#602 G2 (AiPlayerbot.DeathLoop.FarEscape): a revive (short life after it) and the latest death.
    void RecordReviveForLoop();
    bool GetLatestLoopDeath(ai::death_loop::Death& death, uint32& zoneId) const;
    // twow-repo#541 A39 (AiPlayerbot.DeathLoop.Escape): when the last evacuation of this bot happened (repeat check).
    void MarkDeathLoopEscape(uint32 nowSeconds) { lastDeathLoopEscapeAt = nowSeconds; }
    uint32 GetDeathLoopSize() const { return uint32(recentDeaths.size()); }
    //Checks if the bot is summoned as alt of a player
    bool IsAlt() { return HasRealPlayerMaster() && !sRandomPlayerbotMgr.IsRandomBot(bot); }
    //Get the group leader or the master of the bot.
    Player* GetGroupMaster() { return bot->InBattleGround() ? master : bot->GetGroup() ? (sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid()) ? sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid()) : master) : master; }

    bool IsGroupLeader() { return bot->GetGroup() && bot->GetGroup()->GetLeaderGuid() == bot->GetObjectGuid(); }

    //Check if player is safe to use.    
    static bool IsSafe(Player* player, WorldObject* obj) {return obj && obj->GetMapId() == player->GetMapId() && obj->GetInstanceId() == player->GetInstanceId() && (!obj->IsPlayer() || !((Player*)obj)->IsBeingTeleported() || !((Player*)obj)->GetSession()->GetPlayer()); }
    bool IsSafe(WorldObject* obj) { return IsSafe(bot, obj); }
    bool IsSafe(Player* player) { return IsSafe(bot, player); }

    //Returns a semi-random (cycling) number that is fixed for each bot.
    uint32 GetFixedBotNumber(BotTypeNumber typeNumber, uint32 maxNum = 100, float cyclePerMin = 1, bool ignoreGuid = false); 

    GrouperType GetGrouperType();
    GuilderType GetGuilderType();
    uint32 GetMaxPreferedGuildSize();

    bool HasPlayerNearby(WorldPosition pos, float range);
    bool HasPlayerNearby(float range = sPlayerbotAIConfig.reactDistance);
    bool HasManyPlayersNearby(uint32 trigerrValue = 20, float range = sPlayerbotAIConfig.sightDistance);
    bool ChannelHasRealPlayer(std::string channelName);


    ActivePiorityType GetPriorityType();
    std::pair<uint32,uint32> GetPriorityBracket(ActivePiorityType type);
    bool AllowActive(ActivityType activityType);
    bool AllowActivity(ActivityType activityType = ALL_ACTIVITY, bool checkNow = false);

    bool HasCheat(BotCheatMask mask) const;
    BotCheatMask GetCheat() { return cheatMask; }
    void SetCheat(BotCheatMask mask) { cheatMask = mask; }

    // Cache master's GUID alongside the raw pointer so we can revalidate
    // the pointer each tick against ObjectAccessor — if the master Player
    // was destroyed (logout / disconnect) since SetMaster() was called,
    // FindPlayer(masterGuid) returns nullptr (or a different live Player)
    // and we null `master` BEFORE any deref. See RevalidateMasterPointer.
    // 2026-05-05: bot crashed with
    // EXECUTE at 0x0 in UpdateAI's logout-cancel block right after the
    // master (the master) disconnected — `if (master && IsInCombat(master))`
    // dereferenced a freed Player. The `master &&` guard catches null
    // but not dangling.
    void SetMaster(Player* m)
    {
        this->master = m;
        this->masterGuid = m ? m->GetObjectGuid() : ObjectGuid();
    }

    // Null `master` if the Player it points at is gone. Was an inline block at
    // the top of UpdateAI; it is a method now because the TICK is not the only
    // path that dereferences the pointer - the LOGOUT path does too, and that is
    // exactly where a master who just disconnected leaves a dangling pointer
    // behind (see PlayerbotHolder::LogoutPlayerBot).
    void RevalidateMasterPointer();
    AiObjectContext* GetAiObjectContext() { return aiObjectContext; }
    void SetAiObjectContext(AiObjectContext* aiObjectContext) { this->aiObjectContext = aiObjectContext; }
    ChatHelper* GetChatHelper() { return &chatHelper; }
    bool IsOpposing(Player* player);
    static bool IsOpposing(uint8 race1, uint8 race2);
    PlayerbotSecurity* GetSecurity() { return &security; }

    WorldPosition GetJumpDestination() { return jumpDestination; }
    void SetJumpDestination(const WorldPosition& pos) { jumpDestination = pos; }
    void ResetJumpDestination() { jumpDestination = WorldPosition(); }

    bool IsJumping() { return jumpTime; }
    void SetFallAfterJump() { fallAfterJump = true; }
    void SetJumpTime(uint32 time) { jumpTime = time; }
    bool CanMove();
    void StopMoving();
    bool IsInRealGuild();
    void SetPlayerFriend(bool isFriend) {isPlayerFriend = isFriend;}
    bool IsPlayerFriend() { return isPlayerFriend; }
    bool HasPlayerRelation();

    bool IsStateActive(BotState state) const;
    time_t GetCombatStartTime() const;

    void OnCombatStarted();
    void OnCombatEnded();
    void OnDeath();
    void OnResurrected();
    
    void SetActionDuration(const Action* action);
    void SetActionDuration(uint32 duration);

    const Action* GetLastExecutedAction(BotState state) const;

    bool IsImmuneToSpell(uint32 spellId) const;

    bool IsInPve();
    bool IsInPvp();
    bool IsInRaid();

    void SetMoveToTransport(bool flag = true) { isMovingToTransport = flag; }
    bool GetMoveToTransport() { return isMovingToTransport; }

    void SetShouldLogOut(bool val = true) { shouldLogOut = val; }
    bool GetShouldLogOut() { return shouldLogOut; }

    PlayerTalentSpec GetTalentSpec();

        // Role handed out by the dungeon finder (LFT_ROLE_* bits: 1 tank,
        // 2 heal, 4 damage; 0 = none). Overrides the talent-derived combat
        // strategy in AiFactory, and survives ResetStrategies() - which is the
        // whole point, since that runs whenever the master changes, i.e. right
        // when the bot joins the player's group.
        // Modules set this while a bot runs an activity that stands the
        // party on top of TELEPORT areatriggers (a dungeon-clear run parks
        // right at instance entrances/exits): the stock "area trigger" relay
        // must not port such a bot out mid-run - live, the run's tank walked
        // into the Deadmines exit trigger and vanished ("leader tank
        // vanished"; caught by a gdb trap on Player::TeleportTo under
        // AreaTriggerAction::Execute). See that action for the gate.
        void SetSuppressAreaTriggerRelay(bool on) { m_suppressAreaTriggerRelay = on; }
        bool IsAreaTriggerRelaySuppressed() const { return m_suppressAreaTriggerRelay; }

        void SetForcedRole(uint8 role) { m_forcedRole = role; }
        uint8 GetForcedRole() const { return m_forcedRole; }
    void UpdateTalentSpec(PlayerTalentSpec spec = PlayerTalentSpec::TALENT_SPEC_INVALID);

    bool CanEnterArea(const AreaTrigger* area);
    void Unmount();

    void QueuePacket(WorldPacket& pkt);
    // Hotfix 8.8 (twow-repo#474): use a game object through the player's own handler.
    void UseGameObjectDirect(ObjectGuid guid);

    float GetLevelFloat() const;

    static bool CanSpellClick(Player* bot, uint32 entry);
    bool CanSpellClick(ObjectGuid guid) const;
    bool HandleSpellClick(uint32 entry);
    bool HandleSpellClick(ObjectGuid guid);

    void SetLastEvent(Event& event) { lastEvent = event; }
    Event& GetLastEvent() { return lastEvent; }

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> GetWeakPtr() const { return m_weakRef; }
    void SetWeakPtr(MaNGOS::unique_weak_ptr<PlayerbotAI> weakRef) { m_weakRef = std::move(weakRef); }
#endif

private:
    bool UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned);
    void UpdateFaceTarget(uint32 elapsed, bool minimal);

    // LLM-012: holds one async chat-packet batch (see SendDelayedPacket/
    // ReceiveDelayedPacket above) while it is drained. `futPackets` is the
    // still-running async result; once ready its packets move into `ready`
    // and are delivered one at a time, `pendingDelayMs` ticking down between
    // each, so the drip-feed pacing of the old sleep_for() loop is preserved
    // without ever blocking the world thread.
    struct DelayedPacketBatch
    {
        std::future<std::vector<std::pair<WorldPacket, uint32>>> futPackets;
        std::queue<std::pair<WorldPacket, uint32>> ready;
        int64 pendingDelayMs = 0;

        bool Done() const { return !futPackets.valid() && ready.empty(); }
    };
    std::vector<DelayedPacketBatch> pendingSessionPackets;  // -> bot->GetSession()->QueuePacket()
    std::vector<DelayedPacketBatch> pendingReceivePackets;  // -> botOutgoingPacketHandlers.AddPacket()

    // Drains one set of batches, delivering one ready packet per batch per
    // tick (once its per-packet delay has elapsed) via `deliver`, and erasing
    // batches once their future is consumed and their queue is empty.
    // `deliver` runs on the world thread (only ever called from
    // UpdateDelayedPackets, itself only called from this bot's own AI tick),
    // so it may safely re-resolve and dereference the session/handler.
    template <typename Deliver>
    static void DrainDelayedPacketBatches(std::vector<DelayedPacketBatch>& batches, uint32 elapsed, Deliver deliver);

    // Drains pendingSessionPackets/pendingReceivePackets. Must only be called
    // from this bot's own AI tick (world thread) -- see the comment on
    // SendDelayedPacket/ReceiveDelayedPacket for why.
    void UpdateDelayedPackets(uint32 elapsed);

    // Executes the commands a BotDialogueProvider asked for on this bot's
    // behalf, if any. Called from this bot's own AI tick, next to
    // UpdateDelayedPackets, because the two halves of one dialogue round trip
    // -- the line to say and the thing to do -- come back on the same tick.
    //
    // Every command runs through HandleCommand with the SPEAKER as fromPlayer,
    // so PlayerbotSecurity decides whether it happens, exactly as it does for
    // the same words typed. Nothing here grants anything: it can only replay a
    // command a player was already entitled to give.
    void RunPendingDialogueCommands();

    // twow-repo#602 ROT-01 F6: the creatures attacking the bot while it fights, for the adds column of deaths.csv (the death clears
    // the attacker set before OnDeath runs). Called from this bot's own AI tick only; the last snapshot is kept while the bot is dead.
    void UpdateDeathAddsSnapshot();
    ai::death_adds::Snapshot deathAdds;

protected:
	Player* bot;
	Player* master;
	uint8 m_forcedRole = 0;
    // #416: last [BotSlowUpdate] line (seconds), for the rate limit.
    uint32 lastSlowUpdateLog = 0;
    // #420: [GroupBuff] counters of the current window.
    ai::group_buff::BuffWindow groupBuffWindow;
    // #416: phase of the bot for [BotSlowUpdate].
    uint32 travelChooseChecked = 0;
    // #416 (7.3): value-cache size, taken on the bot's own thread for [MemStores].
    std::atomic<uint32> ownValueCount{ 0 };
    // #416 (7.5) F1: eviction of idle cached values.
    ai::value_evict::EvictClock valueEvictClock;
    std::atomic<uint32> evictedValues{ 0 };
    std::atomic<uint32> sharedValueCount{ 0 };
    // Hotfix 8.1: more per-bot stores for [MemStores], counted on the bot's thread.
    std::atomic<uint32> whisperCount{ 0 };
    std::atomic<uint32> chatQueueCount{ 0 };
    std::atomic<uint32> packetQueueCount{ 0 };
    std::atomic<uint32> recordedCount{ 0 };
    std::atomic<uint32> createdObjectCount{ 0 };
    std::atomic<uint32> lastUpdateTime{ 0 };
    uint32 lastIdleLog = 0;
    void ReportIdle(uint32 idleSeconds);
    // twow-repo#601 R01 L1 (AiPlayerbot.ActivityTrace): one activity sample a minute, [BotActivity] per clock hour.
    ai::bot_activity::Clock activityClock;
    void SampleActivity(uint32 now);
    // twow-repo#601 (AiPlayerbot.ActivityTrace.StuckDetail): one [StuckDetail] line per stuck episode.
    ai::bot_activity::StuckEpisode stuckEpisode;
    void ReportStuckDetail(ai::bot_activity::State const& state);
    // twow-repo#601 (AiPlayerbot.ActivityTrace.IdleDetail): one [IdleDetail] start and end line per idle episode.
    ai::bot_activity::IdleEpisode idleEpisode;
    void ReportIdleDetail(ai::bot_activity::State const& state, ai::bot_activity::Kind kind, ai::bot_activity::IdleNote note, uint32 now);
    // Hotfix 8.2: targets this bot could not get closer to.
    ai::unreachable::Approach reachApproach;
    ai::unreachable::IgnoreList unreachableTargets;
    uint32 lastUnreachableLog = 0;
    uint32 lastValueCountTime = 0;
    uint32 lastValueNamesTime = 0;
    mutable std::mutex valueNamesMutex;
    std::string topValueNames;
    // #421 C: progress watch for the quest rescue teleport.
    ai::quest_search::ProgressTracker questProgress;
    uint32 lastQuestProgressCheck = 0;
    // twow-repo#575 (PATH-01 F4): until when the dispatched graph walk to the island machine runs (no re-plan while moving),
    // and the next time a machine_walk_fallback line may be written.
    uint32 machineWalkUntil = 0;
    uint32 machineWalkFallbackLog = 0;
    // twow-repo#575 (PATH-01 F2): set by an island hub rescue, cleared by the bot's next progress.
    std::atomic<bool> islandHubRescuePending{ false };
    // Hotfix 8.12: combat without progress (bot 27 Nilenata).
    ai::stuck_combat::State stuckCombat;
    ai::gather_purpose::State gatherPurpose;
    ai::fishing::PurposeTrace fishingTrace;
    ai::item_use::Trace itemUseTrace;
    std::string fishingLastBreak;
    std::atomic<uint32> lastQuestRescue{ 0 };
    std::atomic<bool> questRescueRequested{ false };
    std::atomic<bool> questRescueDone{ false };
    // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.ResetOnProgressEvents): reset events
    // (ai::quest_failure_class::Reset*) asked for by the world thread (rescue teleport, battleground return), taken by
    // the bot's own update; the level the progress watch saw at its last check (0 = none yet: this login).
    std::atomic<uint32> questRouteResetEvents{ 0 };
    uint32 questProgressLevel = 0;
    // The quest log state the progress watch saw at its last check (ai::quest_failure_class::MixQuestState, 0 = none yet).
    uint32 questProgressState = 0;
    void ApplyQuestRouteReset(uint32 events);
    // twow-repo#541/#551: set by the world thread (rndbot park), read by the bot's map thread.
    std::atomic<bool> parked{ false };
    // twow-repo#517 stage a (AiPlayerbot.BG.PlayerFill): set and cleared by the world thread while the manager owns this
    // bot's battleground queue entry and, after its accept, until the bot is back from that battleground
    // (RandomPlayerbotMgr::CheckBgFill); read by BGStatusAction on the bot's map thread.
    std::atomic<bool> bgFillPending{ false };
    // twow-repo#627 S7 (AiPlayerbot.QuestGroups.Enable): the bot's role in a help group (0 none, 1 follower, 2 leader), set by the
    // world-thread manager (quest_groups::WorldThreadManager), read on the bot's map thread (LeaveFarAway, AutoShare, CLI-Deep hooks).
    std::atomic<uint8> questGroupRole{ 0 };
    // twow-repo#627: set by PosStuck (CLI-Deep hook, bot thread) when it detects this bot as a help-group leader, taken by the manager.
    std::atomic<bool> questGroupLeaderStuck{ false };
    // twow-repo#628 / #627: the AI part of a group change the world-thread managers posted (ai::group_ai_refresh::Request bits),
    // taken in the bot's next UpdateAI on its own map thread (ApplyGroupAiRequests).
    std::atomic<uint32> groupAiRequests{ 0 };
    std::atomic<uint32> questGroupForce{ 0 };   // S7 StagedOps: the ForceTravel request's quest/objective (PackForce)
    uint32 lastParkedUpdateMs = 0;
    // twow-repo#551 (AiPlayerbot.Park.Quiet): when a quiet parked bot may do its next idle emote (seconds, 0 = never).
    std::atomic<uint32> nextParkIdleEmote{ 0 };
    // twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder): the episode reset a park asks for (world thread, SetParked)
    // and the pending band rescue (QuestBand.cpp: (rung + 1) | requestedAt << 32, 0 = none; bot thread writes, world thread takes).
    std::atomic<bool> bandEpisodeResetPending{ false };
    std::atomic<uint64> bandRescue{ 0 };
    std::atomic<uint32> bandTripFromZone{ 0 };   // the zone the last band step left (from_zone= of the lines on any thread)
    uint32 lastBotSave = 0;          // twow-repo#541 A47: time of this bot's last own save (seconds)
    bool botSaveScheduled = false;   // A47: first save offset applied
    ai::stall_guard::TopActions updateActions;
    uint32 firstUpdateTime = 0;
    uint32 lastReviveTime = 0;
    bool wasAlive = true;

public:
    // #416: called when an UpdateAI call took elapsedMs.
    void ReportSlowUpdate(uint32 elapsedMs);
    // #420: the bot has a real player as master, or a real player (or a real
    // player's bot) is in its group on the same map.
    bool IsInGroupWithRealPlayer();
    // #420: counts a helpful aura cast on another unit outside combat.
    void RecordGroupBuff(SpellEntry const* spellInfo, Unit* target);
    // #420: writes one [GroupBuff] line when the window is over.
    void ReportGroupBuff(uint32 now);
    // #416: travel-target candidates checked in this update.
    void AddTravelChooseChecked(uint32 count) { travelChooseChecked += count; }
    // #416 (7.3): read by [MemStores] on the world thread.
    uint32 GetOwnValueCount() const { return ownValueCount.load(); }
    uint32 TakeEvictedValues() { return evictedValues.exchange(0); }
    uint32 GetSharedValueCount() const { return sharedValueCount.load(); }
    // Hotfix 8.1: per-bot stores and the last UpdateAI time for [MemStores].
    uint32 GetWhisperCount() const { return whisperCount.load(); }
    uint32 GetChatQueueCount() const { return chatQueueCount.load(); }
    uint32 GetPacketQueueCount() const { return packetQueueCount.load(); }
    uint32 GetRecordedCount() const { return recordedCount.load(); }
    uint32 GetCreatedObjectCount() const { return createdObjectCount.load(); }
    uint32 GetLastUpdateTime() const { return lastUpdateTime.load(); }
    // Hotfix 8.2: one reach attempt at this distance; true when the target is now
    // ignored as unreachable (bots without a real player; with one only under Rotation.ReachPlayerMaster, twow-repo#630).
    bool CheckReachProgress(Unit* target, float distance);
    // twow-repo#630 follow-up (Rotation.ReachDropEngagement): after that drop a bot with a real player as master lets go of the
    // target entirely (attack/pull/old target, selection, swing), so its combat ends and follow resumes (ReachDropPolicy.h).
    void DropUnreachableEngagement(Unit* target);
    bool IsUnreachableTarget(ObjectGuid guid) const { return unreachableTargets.Ignored(guid.GetRawValue(), uint32(time(nullptr))); }
    std::string GetTopValueNames() const { std::lock_guard<std::mutex> lock(valueNamesMutex); return topValueNames; }
    // #421 C: set on the bot's thread, taken and acted on by the world thread.
    bool TakeQuestRescueRequest() { return questRescueRequested.exchange(false); }
    // twow-repo#587: a request is pending (world thread peek, not taken).
    bool HasQuestRescueRequest() const { return questRescueRequested.load(); }
    // twow-repo#587 (Z1b): a bot stranded on the other continent asks for the rescue (per-bot limit as the idle rescue).
    void RequestQuestRescue(uint32 now)
    {
        if (!lastQuestRescue.load() || now - lastQuestRescue.load() >= ai::quest_search::RescuePerBotSeconds)
            questRescueRequested = true;
    }
    void OnQuestRescued(uint32 now) { lastQuestRescue = now; questRescueDone = true; }
    // twow-repo#575 (PATH-01 F2): an island hub rescue is pending until the bot makes progress (its own thread clears it);
    // a second hub rescue while it is still pending finds the island empty for this bot (world thread, StartZonePolicy.h).
    bool IslandHubRescueWithoutProgress() const { return islandHubRescuePending.load(); }
    void MarkIslandHubRescue() { islandHubRescuePending = true; }
    // twow-repo#601 S1 (ResetOnProgressEvents): any thread asks the bot's own update to reset the grind-fallback
    // counter, the quest route backoff and the quest search stage (ai::quest_failure_class::Reset* bits).
    void RequestQuestRouteReset(uint32 events) { questRouteResetEvents.fetch_or(events); }
    // twow-repo#517 stage a: a quest rescue is asked for, or was done and not yet taken by the bot's own update.
    bool IsQuestRescueBusy() const { return questRescueRequested.load() || questRescueDone.load(); }
    // twow-repo#517 stage a: the manager queued this bot for a battleground and accepts its invite; BGStatusAction then
    // leaves WAIT_JOIN alone so nothing is done twice (also a WAIT_JOIN latched before the accept: the flag stays set
    // until the bot is back from the battleground).
    void SetBgFillPending(bool value) { bgFillPending = value; }
    bool IsBgFillPending() const { return bgFillPending; }
    // twow-repo#627 S7: the help-group role (0 none, 1 follower, 2 leader) and the leader-stuck note (members above); the role is
    // written by the world-thread manager, the note by PosStuck on the bot thread and taken by the manager.
    uint8 GetQuestGroupRole() const { return questGroupRole.load(); }
    void SetQuestGroupRole(uint8 role) { questGroupRole.store(role); }
    void SetQuestGroupLeaderStuck(bool value) { questGroupLeaderStuck.store(value); }
    bool TakeQuestGroupLeaderStuck() { return questGroupLeaderStuck.exchange(false); }
    // twow-repo#628 / #627 (world thread): post the AI part of a group change (ai::group_ai_refresh::Merge: the last change wins).
    void RequestGroupAi(uint32 request);
    uint32 PendingGroupAiRequests() const { return groupAiRequests.load(); }
    // S7 QuestGroups.StagedOps: the forced quest travel on the bot's own thread (the parameters before the request bit).
    void RequestQuestForce(uint32 packedForce) { questGroupForce.store(packedForce); RequestGroupAi(ai::group_ai_refresh::ForceTravel); }
    // Bot thread (UpdateAI): run the posted requests - strategy rebuild and reset first, then an S7 role set.
    void ApplyGroupAiRequests();
    // twow-repo#517 stage a (OB-10): a fill bot is out of its battleground again. Quest search stage 0 and
    // questProgress.lastChange = now, as the questRescueDone path in UpdateAI - but without OnQuestRescued(): that sets
    // lastQuestRescue, and the per-bot rescue limit would then block a later real rescue.
    void OnReturnFromBattleground(uint32 now);
    // twow-repo#541/#551 (rndbot park): a parked bot updates its AI every ai::park::AiIntervalMs only.
    bool IsParked() const { return parked; }
    // twow-repo#601 (OB-00 28.10.2026, cli-pathfinding/far-travel-stuck/CAUSE.md), the bot's own thread only: the state of its
    // RosterFarMove (FarMovePolicy.h WaitStateOf on the "last movement" value); pending = waiting, travel and no stall.
    ai::far_move::WaitState FarMoveWaitState() const;
    bool IsFarMovePending() const { return FarMoveWaitState() == ai::far_move::WaitState::Waiting; }
    // twow-repo#551 (AiPlayerbot.Park.Persist), world thread only: the stored park state was handled after this login
    // (a new login is a new PlayerbotAI); attempts counts restore tries of a bot that was busy.
    bool parkRestoreChecked = false;
    uint32 parkRestoreAttempts = 0;
    // twow-repo#541 (jitter): with AiPlayerbot.AiDelayJitterPct the first parked update is spread over an interval.
    void SetParked(bool value);
    uint32 GetQuestIdleSeconds(uint32 now) const { return questProgress.IdleSeconds(now); }
    // #416: time of one executed action (ms > 0), for the top-3 of this update.
    void RecordActionTime(std::string const& name, uint32 ms) { updateActions.Add(name, ms); }
    // twow-repo#601 GrindMode (AiPlayerbot.QuestFirstProgression.GrindMode = 2, QuestGrindModePolicy.h): this bot is a
    // quest-first roster bot on its own below the max level and must not grind as a substitute activity. Always false
    // with GrindMode 0 or 1 (the config is read first).
    bool IsQuestNoGrind();
    // twow-repo#601 S2 / S4 (GrindMode = 1, OB-10 rev. 2): the same population, under the gap filler (grind only inside
    // the quest route backoff). Always false with GrindMode 0 or 2 (the config is read first).
    bool IsQuestGapGrind();
    // twow-repo#601 decision order (AiPlayerbot.Grind.QuestFirstOrder, owner 20.10.2026): a quest-first roster bot on its own
    // below the max level whose "attack anything" target must be a quest objective or block its way (QuestFirstOrderPolicy.h).
    // Always false with the switch 0 (the config is read first).
    bool IsQuestFirstOrder();
    // GrindMode 2 or QuestFirstOrder: no kill trip the grind target filter would refuse (grind / boss / skinning / forced).
    bool IsQuestNoGrindTrip();
    // QuestFirstOrder: adds 1 to one minute counter (ai::quest_first_order::Event as uint8; any thread, relaxed).
    static void NoteQuestOrder(uint8 event);
    // QuestFirstOrder, world thread: "[QuestOrder] minute ..." with the counts since the last call (counters reset).
    static std::string TakeQuestOrderMinuteLine();
    // QuestFirstOrder, the bot's own thread: one "[QuestOrder] state=no_quest|grind_refused" line per kind and episode (OB-00:
    // how often no quest is found, per bot / band / zone); EndQuestOrderEpisode: a quest target ends it ("state=quest_found
    // seconds="). Nothing logged before the first no_quest / grind_refused of the bot.
    void NoteQuestOrderEpisode(uint8 event);
    void EndQuestOrderEpisode();
    // twow-repo#601 S2 / S4 [GrindWindow] (GrindMode 1, OB-10 rev. 2): adds n to one minute counter
    // (ai::quest_grind_mode::WindowEvent as uint8; any thread, relaxed). Called only on GrindMode 1 paths.
    static void NoteGrindWindow(uint8 event, uint32 n = 1);
    // GrindMode 1, world thread: "[GrindWindow] minute ..." with the counts since the last call (counters reset); empty when
    // every count is 0 (no line).
    static std::string TakeGrindWindowMinuteLine();
    // twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder, QuestBand.cpp): the band step. Every member reads the switch
    // first or touches only state the band path writes; nothing with BandLadder 0.
    // The band episode of this bot (bot thread only): the last two zones it left by a band step and the band it left them in,
    // the expiries since the last arrival / rescue, the running trip's facts for the lines, the last band rescue and the
    // rate limit of the skip lines.
    struct BandEpisode
    {
        uint32 leftZones[2] = { 0, 0 };
        uint32 leftBandMin = 0;
        uint32 expiries = 0;
        uint32 fromZone = 0;
        uint32 rung = 0;
        uint32 stepAt = 0;
        uint32 lastBandRescue = 0;
        uint32 skipLogAt = 0;
    };
    BandEpisode& GetBandEpisode() { return bandEpisode; }
    uint32 GetBandTripFromZone() const { return bandTripFromZone.load(std::memory_order_relaxed); }
    // Config first (BandLadder), then the ladder is built, the cohort, a persistent quest-first roster bot on its own (no real
    // master, no group) below the max level.
    bool InQuestBandScope();
    // InQuestBandScope() and the bot's ladder key has a usable row for its race and level (O(1) bitmap): the giver filter.
    bool QuestBandGiverFilter();
    // Switch first, then a band trip runs (travel target with a band end, active) - the idle quest rescue waits for it.
    bool BandTripRunning();
    // A park ended the episode (SetParked): consumed by the next band step on the bot thread.
    bool TakeBandEpisodeReset() { return bandEpisodeResetPending.exchange(false, std::memory_order_relaxed); }
    void OnBandStep(uint32 now, uint32 fromZone, uint32 bandMin, uint32 rung);
    void OnBandArrived(uint32 now);
    // An expiry (end or cooldown): logs it, and the second one of the episode asks for the forward rescue.
    void OnBandExpired(uint32 now, uint32 rung, bool cooldown, float distLeft);
    // World thread (ProcessQuestRescues): the band rescue's rung + 1, 0 = a plain rescue (an old one is dropped).
    uint32 TakeBandRescueRung(uint32 now);
    // World thread: a band rescue waits (written only by OnBandExpired, BandLadder > 0). The global limiter refused its taken
    // request: it is put back for the next scan (no per-bot check - the band slot was already stamped).
    bool HasBandRescuePending() const { return bandRescue.load(std::memory_order_relaxed) != 0; }
    void RequeueBandRescue() { questRescueRequested = true; }
    // Minute counters (ai::quest_band::Event as uint8; any thread, relaxed) and the world thread's minute lines.
    static void NoteBand(uint8 event, bool cohortB);
    static std::vector<std::string> TakeQuestBandMinuteLines();
    // GrindMode 2, the bot's own thread: counts a blocked grind decision (ai::quest_grind_mode::Reason as uint8) for the
    // minute line and writes one [QuestIdle] line per reason and idle episode (at most 4). Nothing without GrindMode 2.
    void NoteQuestIdle(uint8 reason, std::string const& target);
    // GrindMode 2: a quest target was found - the idle episode ends, every reason may log once again.
    void ClearQuestIdle() { questIdleLogged = 0; questIdleClass = 0; }
    // GrindMode 2: the S1 class (ai::quest_failure_class::FailureClass as uint8) of the bot's latest classified quest
    // route rejection - shown as class= on its [QuestIdle] lines until the idle episode ends.
    void NoteQuestIdleClass(uint8 failureClass) { questIdleClass = failureClass; }
    // GrindMode 2, world thread: "[QuestIdle] minute ..." with the counts since the last call (counters reset).
    static std::string TakeQuestIdleMinuteLine();
    // twow-repo#601 S3a (AiPlayerbot.QuestFeasibility.Enable, QuestFeasibilityPolicy.h), bot's own thread: why this bot
    // must not take the quest (roster bot on its own; class quests exempt from the solo group rule only); None with
    // Enable 0, before anything else.
    ai::quest_feasibility::Reason QuestFeasibilityReason(Quest const* quest);
    // S3a: the same for an accept path / the quest item / the giver relation (giver: every reason, GiverInactive);
    // counts it for the minute line and writes a rate-limited [QuestFeasibility] state=skip line. True = skip.
    bool SkipInfeasibleQuest(Quest const* quest, ai::quest_feasibility::Path path);
    // S3a: one infeasible quest dropped by the quest log cleanup (minute line).
    static void NoteQuestFeasibilityDrop();
    // S3a, world thread: "[QuestFeasibility] minute ..." with the counts since the last call (counters reset).
    static std::string TakeQuestFeasibilityMinuteLine();

protected:
    // twow-repo#601 GrindMode: the reasons this bot already logged in its idle episode (bit mask of
    // ai::quest_grind_mode::ReasonBit, 0 = none; bot's own thread only).
    uint8 questIdleLogged = 0;
    uint8 questIdleClass = 0;   // GrindMode 2: last S1 rejection class of the idle episode (0 = none; bot's own thread)
    uint8 questOrderLogged = 0;    // twow-repo#601 QuestFirstOrder: kinds logged in the episode (EpisodeBit; bot's own thread)
    uint32 questOrderSince = 0;    // QuestFirstOrder: episode start (epoch s, 0 = none)
    BandEpisode bandEpisode;     // twow-repo#601 S5a: the band episode (bot's own thread only)
    // twow-repo#601 S3a: the bot's recent [QuestFeasibility] skip lines (16 quests, bot's own thread only).
    ai::quest_feasibility::SkipLog questFeasibilityLog;
    std::deque<ai::death_loop::Death> recentDeaths;
    bool lastDeathHostileArea = false;   // A39: the latest death lay in an area of the hostile faction
    uint32 lastDeathLoopEscapeAt = 0;    // A39: time of the last evacuation (repeat_after_escape)
    ai::death_loop::ShortLife shortLife;   // G2: revive -> death streak (bot's own thread only)
    ai::death_loop::Death lastLoopDeath;   // G2: the latest death (atSeconds 0 = none), the loop centre of an escape
    uint32 lastLoopDeathZone = 0;
    // #422: deaths anywhere (cautious mode) and environmental gathering deaths.
    ai::death_series::Series deathSeries;
    ai::death_series::PurposeSuppression gatherDeaths;
    // Hotfix 8.1: [TankPath] window of a bot on a tank path.
    ai::tank_path_diag::Window tankPathWindow;
    uint32 tankPathLastSample = 0;
    bool tankPathOn = false;
	bool m_suppressAreaTriggerRelay = false;
	// GUID-shadow of `master` so we can verify the pointer is still
	// alive each tick without dereferencing it. Set in SetMaster().
	// Used by RevalidateMasterPointer() at the top of UpdateAI.
	ObjectGuid masterGuid;
	uint32 accountId;
    AiObjectContext* aiObjectContext;
    Engine* currentEngine;
    ReactionEngine* reactionEngine;
    Engine* engines[(uint8)BotState::BOT_STATE_ALL];
    BotState currentState;
    ChatHelper chatHelper;
    std::queue<ChatCommandHolder> chatCommands;
    std::mutex chatCommandsMutex;   // audit A29: guards chatCommands
    std::queue<ChatQueuedReply> chatReplies;
    std::mutex chatRepliesMutex;
    PacketHandlingHelper botOutgoingPacketHandlers;
    // twow-repo#563 (X1): packets for this bot from any thread, handled in UpdateAI.
    ai::BoundedInbox<std::unique_ptr<WorldPacket>> botPacketInbox{ ai::BotPacketInboxCapacity };
    // twow-repo#563 (X3c): value writes from other bots, applied in UpdateAI.
    ai::BoundedInbox<std::function<void(ai::AiObjectContext*)>> contextWriteInbox{ ai::ContextWriteInboxCapacity };
    // twow-repo#563 (X3b): this bot's last published targets, read by other bots.
    mutable std::mutex publishedTargetsMutex;
    std::shared_ptr<PublishedTargets const> publishedTargets;
    ai::x3b::ConditionBoard conditionBoard;
    std::atomic<bool> publishedFollowing{ true };
    PacketHandlingHelper masterIncomingPacketHandlers;
    PacketHandlingHelper masterOutgoingPacketHandlers;
    CompositeChatFilter chatFilter;
    PlayerbotSecurity security;
    std::map<std::string, time_t> whispers;
    std::pair<ChatMsg, time_t> currentChat;
    static std::set<std::string> unsecuredCommands;
    bool allowActive[MAX_ACTIVITY_TYPE];
    time_t allowActiveCheckTimer[MAX_ACTIVITY_TYPE];
    bool inCombat = false;
    bool isMoving = false;
    bool isWaiting = false;
    BotCheatMask cheatMask = BotCheatMask::none;
    WorldPosition jumpDestination;
    uint32 jumpTime;
    bool fallAfterJump;
    uint32 faceTargetUpdateDelay;
    bool combatIdleTick = false;   // twow-repo#541 (audit A17): last DoNextAction ran the combat engine and executed nothing
    ObjectGuid combatIdleVictim;   // twow-repo#541 (audit A17): victim of a pending stretched wait (empty = none)
    bool oomBackoffPending = false;   // twow-repo#602 ROT-01 3c-B: an out-of-mana backoff wait is running
    uint32 tankKitLevel = 0;   // twow-repo#602 (Rotation.TankKitFromLevel): the level the strategies were last built for; a crossing of the tank key level rebuilds them
    ai::cast_wait::Watch castWait;   // twow-repo#602 (AiPlayerbot.CastWait.Preparing): pending wait for the bot's own PREPARING cast
    ai::teleport_cast::HoldWatch teleportHold;   // twow-repo#333 finding 4: pending travel / unstuck / zone-escape hold for a teleport
    bool isPlayerFriend = false;
    bool isMovingToTransport = false;
    bool shouldLogOut = false;
    bool m_recordMessages = false;
    bool m_recordIncommingMessages = false;
    std::vector<std::string> m_recordedMessages;
    Event lastEvent;

public:
    void RecordMessages(bool record, bool incomming = false) { m_recordMessages = record; m_recordIncommingMessages = incomming; if (!record) m_recordedMessages.clear(); }
    bool IsRecordingMessages() const { return m_recordMessages; }
    bool IsRecordingIncommingMessages() const { return m_recordIncommingMessages; }
    std::vector<std::string> GetRecordedMessages() { m_recordMessages = false; m_recordIncommingMessages= false; auto msgs = m_recordedMessages; m_recordedMessages.clear(); return msgs; }
    void ClearRecordedMessages() { m_recordedMessages.clear(); m_recordMessages = false; m_recordIncommingMessages = false;}

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> m_weakRef;
#endif
};

template<typename T>
T* PlayerbotAI::GetStrategy(const std::string& name, BotState type)
{
    return  dynamic_cast<T*>(engines[(uint8)type]->GetStrategy(name));
}
