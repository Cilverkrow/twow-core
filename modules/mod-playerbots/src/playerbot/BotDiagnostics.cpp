#include "BotDiagnostics.h"
#include "PlayerbotAIConfig.h"
#include "playerbot.h"
#include "PlayerbotAI.h"
#include "TravelMgr.h"
#include "strategy/values/LastMovementValue.h"
#include "BoundedBotTrace.h"
#include <atomic>
#include <mutex>

namespace ai { namespace botdiag {
    thread_local const char* gLastPhaseTag     = nullptr;
    thread_local const char* gLastPhaseBotName = nullptr;

    bool IsActionLogEnabled()
    {
        return sPlayerbotAIConfig.enableActionLog;
    }

    void TraceBehavior(PlayerbotAI* ai, const char* reason, const char* detail)
    {
        // Never enable the global per-action/spell logging to collect this sample.
        static std::atomic<bool> finished{false};
        if (!sPlayerbotAIConfig.behaviorTrace || finished.load(std::memory_order_relaxed) || !ai)
            return;
        Player* bot = ai->GetBot();
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() ||
            bot->GetMapId() != sPlayerbotAIConfig.behaviorTraceMap || bot->GetInstanceId() != 0)
            return;
        float dx = bot->GetPositionX() - sPlayerbotAIConfig.behaviorTraceX;
        float dy = bot->GetPositionY() - sPlayerbotAIConfig.behaviorTraceY;
        if (dx * dx + dy * dy > sPlayerbotAIConfig.behaviorTraceRadius * sPlayerbotAIConfig.behaviorTraceRadius)
            return;

        static std::mutex mutex;
        static BoundedBotTrace sample;
        uint32 sequence; uint64 dropped;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!sample.Take(WorldTimer::getMSTime(), bot->GetGUIDLow()))
            {
                if (sample.Finished() && !finished.exchange(true))
                    Log::Instance().out(LOG_PERFORMANCE, "TW_BOT_BEHAVIOR_END emitted=%u suppressed=%llu", sample.Emitted(), (unsigned long long)sample.Suppressed());
                return;
            }
            sequence = sample.Emitted(); dropped = sample.Suppressed();
        }
        // Read manual values on the existing AI owner only; never evaluate an
        // action, run a trigger, change its master, or retain object pointers.
        AiObjectContext* context = ai->GetAiObjectContext();
        GuidPosition rpg = context->GetValue<GuidPosition>("rpg target")->Get();
        TravelTarget* travel = context->GetValue<TravelTarget*>("travel target")->Get();
        LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();
        WorldPosition destination = movement.lastMoveShort;
        WorldPosition next = destination;
        if (!movement.lastPath.empty())
        {
            destination = movement.lastPath.getBack();
            next = movement.lastPath.getPath().front().point;
        }
        std::string nextAction = context->GetValue<std::string>("next rpg action")->Get();
        Log::Instance().out(LOG_PERFORMANCE,
            "TW_BOT_BEHAVIOR seq=%u suppressed=%llu bot=%s guid=%u reason=%s detail=%.160s "
            "pos=%.2f,%.2f,%.2f map=%u motion=%u moving=%u taxi=%u combat=%u afk=%u master=%u "
            "rpg=%llu entry=%d rpgpos=%.2f,%.2f,%.2f next_rpg=%.80s travel_status=%d travel_entry=%d "
            "path_size=%zu next=%.2f,%.2f,%.2f dest_map=%u dest=%.2f,%.2f,%.2f",
            sequence, (unsigned long long)dropped, bot->GetName(), bot->GetGUIDLow(), reason, detail,
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
            uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()), uint32(bot->IsMoving()),
            uint32(bot->IsTaxiFlying()), uint32(bot->IsInCombat()), uint32(bot->isAFK()),
            ai->GetMaster() ? ai->GetMaster()->GetGUIDLow() : 0,
            (unsigned long long)rpg.GetRawValue(), int32(rpg.GetEntry()), rpg.getX(), rpg.getY(), rpg.getZ(), nextAction.c_str(),
            travel ? int32(travel->GetStatus()) : -1, travel ? travel->GetEntry() : 0,
            movement.lastPath.getPath().size(), next.getX(), next.getY(), next.getZ(),
            destination.getMapId(), destination.getX(), destination.getY(), destination.getZ());
    }
}}
