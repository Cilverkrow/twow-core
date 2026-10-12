#pragma once

#include "Action.h"
#include "Queue.h"
#include "Trigger.h"
#include "Multiplier.h"
#include "AiObjectContext.h"
#include "Strategy.h"
#include "playerbot/BotState.h"

#include <functional>
#include <unordered_map>
#include <atomic>

namespace ai
{
    class ActionExecutionListener
    {
    public:
        virtual bool Before(Action* action, const Event& event) = 0;
        virtual bool AllowExecution(Action* action, const Event& event) = 0;
        virtual void After(Action* action, bool executed, const Event& event) = 0;
        virtual bool OverrideResult(Action* action, bool executed, const Event& event) = 0;
        virtual ~ActionExecutionListener() {};
    };

    // -----------------------------------------------------------------------------------------------------------------------

    class ActionExecutionListeners : public ActionExecutionListener
    {
    public:
        virtual ~ActionExecutionListeners() override;

    // ActionExecutionListener
    public:
        virtual bool Before(Action* action, const Event& event) override;
        virtual bool AllowExecution(Action* action, const Event& event) override;
        virtual void After(Action* action, bool executed, const Event& event) override;
        virtual bool OverrideResult(Action* action, bool executed, const Event& event) override;

    public:
        void Add(ActionExecutionListener* listener)
        {
            listeners.push_back(listener);
        }
        void Remove(ActionExecutionListener* listener)
        {
            listeners.remove(listener);
        }

    private:
        std::list<ActionExecutionListener*> listeners;
    };

    // -----------------------------------------------------------------------------------------------------------------------

    // twow-repo#541 (audit A21, AiPlayerbot.Perf.LazyEngineInit): engine rebuilds put off because the engine was
    // not running (deferred) and the ones run later at the start of its DoNextAction (caught). Relaxed counters,
    // drained once per minute by the world thread ([LazyEngineInit]); deferred - caught = Init() calls saved.
    namespace lazy_engine_init
    {
        inline std::atomic<uint64> deferred{ 0 };
        inline std::atomic<uint64> caught{ 0 };
    }

    enum ActionResult
    {
        ACTION_RESULT_UNKNOWN,
        ACTION_RESULT_OK,
        ACTION_RESULT_IMPOSSIBLE,
        ACTION_RESULT_USELESS,
        ACTION_RESULT_FAILED
    };

    class Engine : public PlayerbotAIAware
    {
    public:
        Engine(PlayerbotAI* ai, AiObjectContext *factory, BotState state);

	    void Init();
        void addStrategy(const std::string& name);
		void addStrategies(std::string first, ...);
        bool removeStrategy(const std::string& name, bool init = true);
        bool HasStrategy(const std::string& name);
        Strategy* GetStrategy(const std::string& name) const;
        void removeAllStrategies();
        void toggleStrategy(const std::string& name);
        std::string ListStrategies();
        std::list<std::string_view> GetStrategies();
		bool ContainsStrategy(StrategyType type);
        // twow-repo#541 (audit A21): deferInitWhileInactive = this engine neither runs nor is the reaction engine
        // (PlayerbotAI decides). A real change then only marks the engine stale; the rebuild comes on activation.
        void ChangeStrategy(const std::string& names, bool deferInitWhileInactive = false);
		void PrintStrategies(Player* requester, const std::string& engineType);
        std::string GetLastAction() { return lastAction; }
        const Action* GetLastExecutedAction() const { return lastExecutedAction; }

    public:
	    virtual bool DoNextAction(Unit*, int depth, bool minimal, bool isStunned);
	    ActionResult ExecuteAction(const std::string& name, Event& event);
        bool CanExecuteAction(const std::string& name, bool isUseful = true, bool isPossible = true);

    public:
        void AddActionExecutionListener(ActionExecutionListener* listener)
        {
            actionExecutionListeners.Add(listener);
        }
        void removeActionExecutionListener(ActionExecutionListener* listener)
        {
            actionExecutionListeners.Remove(listener);
        }

    public:
	    virtual ~Engine(void);

    protected:
        // twow-repo#541 (audit A08): ownerCache = true only on the bot's own AI tick (DoNextAction,
        // ProcessTriggers, PushDefaultActions). ExecuteAction/CanExecuteAction also run for commands on
        // foreign threads and keep the plain strategy walk.
        bool MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType, bool ownerCache = false);
        // Returns false when the reset was deferred because a DoNextAction
        // walk owns the queue right now; see the comment at Engine::Reset.
        bool Reset();
        void ProcessTriggers(bool minimal);
        void PushDefaultActions();
        void PushAgain(ActionNode* actionNode, float relevance, const Event& event, bool ownerCache = false);
        ActionNode* CreateActionNode(const std::string& name, bool ownerCache = false);
        // twow-repo#541 (audit A08): CreateActionNode through the per-engine owner cache.
        ActionNode* CreateActionNodeCached(const std::string& name);
        virtual Action* InitializeAction(ActionNode* actionNode);
        virtual bool ListenAndExecute(Action* action, Event& event);

    private:
        void LogAction(const char* format, ...);
        void LogValues();
        // Cheap change-detector for the attached strategy set. A strategy
        // change that leaves the token unchanged is a no-op and must not call
        // Init(), because Init() -> Reset() empties the action queue.
        //
        // This used to be an ordered string join, which allocated on every
        // addStrategy/ChangeStrategy - twice, once before and once after.
        // BattleGroundTactics issues ChangeStrategy("-buff") on every
        // non-combat tick of every bot in a battleground, so at ~1000 bots
        // that string was built on the hottest path there is. The token is an
        // XOR of the per-name hashes (order-independent, and exactly undone
        // when the name is removed again) mixed with the set size, kept up to
        // date by addStrategy/removeStrategy/removeAllStrategies.
        static uint64 StrategyNameHash(const std::string& name)
        {
            return static_cast<uint64>(std::hash<std::string>()(name));
        }
        uint64 StrategySetToken() const
        {
            return strategiesHash ^ (static_cast<uint64>(strategies.size()) * 0x9E3779B97F4A7C15ULL);
        }
        // twow-repo#541 (audit A08): called at every insert/erase/clear of `strategies`, also with the
        // switch off (then the map is empty and this is one empty() check), so a config reload that
        // turns the switch on again never finds an entry from an older strategy set.
        void ClearActionNodeOwners() { if (!actionNodeOwners.empty()) actionNodeOwners.clear(); }

    protected:
	    Queue queue;
	    std::list<TriggerNode*> triggers;
        std::list<Multiplier*> multipliers;
        AiObjectContext* aiObjectContext;
        std::map<std::string, Strategy*> strategies;
        // XOR of StrategyNameHash() over every key in `strategies`.
        uint64 strategiesHash = 0;
        // twow-repo#541 (audit A08, AiPlayerbot.Perf.ActionNodeOwnerCache): base action name (the part
        // before the first "::") -> first strategy in `strategies` order whose ActionNode factories
        // answer it, nullptr = none does. Only owners are kept, never nodes. Filled only by
        // CreateActionNodeCached on the bot's own AI tick, emptied by ClearActionNodeOwners at every
        // change of `strategies`. Per engine = per bot, same thread discipline as `strategies`.
        std::unordered_map<std::string, Strategy*> actionNodeOwners;
        float lastRelevance;
        std::string lastAction;
        ActionExecutionListeners actionExecutionListeners;
        BotState state;
        Action* lastExecutedAction;
        // True while DoNextAction is walking `queue`. Reset() must not drain
        // the queue in that window; it sets reinitPending instead and
        // DoNextAction re-inits once the walk is over.
        bool inDoNextAction = false;
        bool reinitPending = false;
        uint32 actionBudgetDeferrals = 0;   // twow-repo#541 (Perf.ActionBudget): expensive actions deferred in a row
        // twow-repo#541 (audit A21): the strategy set changed while this engine was not running and its triggers,
        // multipliers and queue are not rebuilt yet. Init() clears it; DoNextAction rebuilds first when set.
        // Protected on purpose: only Engine::ChangeStrategy sets it.
        bool initStale = false;

    public:
		bool testMode;
        bool initMode = true;
    };
}
