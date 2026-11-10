# twow-repo#333 T28b (plan A3-PLAN-ZIEL T28b, OB-00 09.11.2026 T28 Q1 (a); CLI-BERUFE): AiPlayerbot.Gathering.PaceReach (0 off
# default, 1 all, 2 cohort B, 3 cohort A; > 3 -> 0 with an error line) and AiPlayerbot.Gathering.PaceReachYards (45, 30-60 else
# 45 with an error line; an error line when not beyond the LootDistance), ConfigEcho. A roster bot on its own, not grouped, whose
# herbalism / mining lags behind min(rank cap, 5 x level) may pick an orange / yellow node up to PaceReachYards instead of the
# loot distance. Pins: the switch (dist block byte-exact, members, clamps, echo order, loader placed right after Gathering.Trace
# - away from the combat-rule comment), the pure policy (standard headers only; limits; skills: herbalism / mining only; the
# per-bot state byte-exact), the ONE hook in LootObjectStack::OrderByDistance with one key test (body byte-exact, and without
# the T28b lines it is the old body: the 0-path neutrality), the T28b helpers byte-exact (scope, far rule, pick clock, end of a
# far pick with the reset of "loot target", open), the hooks in LootAction / MoveToLootAction / OpenLootAction, the callers
# reading the shared list unchanged, the far counters appended to the [Gathering] event=share line, no T28b token outside its
# files (nothing in PlayerbotAI.cpp / PlayerbotAI.h), no travel / quest / purpose writes, and the unit test registration.
# A byte-exact pin that fails means the reviewed text changed: review the new text, then update the pin with it.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

set(TAG "#333 T28b Gathering.PaceReach")

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_module path out_var)
  file(READ "${PB_MODULE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${TAG}: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Byte-exact: the two texts are equal; else the length of their common prefix and the line it ends in.
function(require_equal actual expected description)
  if(NOT "${actual}" STREQUAL "${expected}")
    string(LENGTH "${actual}" la)
    string(LENGTH "${expected}" le)
    set(n ${la})
    if(le LESS n)
      set(n ${le})
    endif()
    set(lo 0)
    set(hi ${n})
    while(lo LESS hi)
      math(EXPR mid "(${lo} + ${hi} + 1) / 2")
      string(SUBSTRING "${actual}" 0 ${mid} pa)
      string(SUBSTRING "${expected}" 0 ${mid} pe)
      if("${pa}" STREQUAL "${pe}")
        set(lo ${mid})
      else()
        math(EXPR hi "${mid} - 1")
      endif()
    endwhile()
    string(SUBSTRING "${expected}" 0 ${lo} same)
    string(REGEX MATCHALL "\n" breaks "${same}")
    list(LENGTH breaks line)
    math(EXPR line "${line} + 1")
    message(FATAL_ERROR "${TAG}: ${description} drifted from the reviewed text (pinned byte-exact): first difference in pinned line ${line} (byte ${lo}; lengths ${la} / ${le})")
  endif()
endfunction()

# The text from the first `begin` to the next `end` after it (both included); fatal when either is missing.
function(between text begin end out_var)
  string(FIND "${text}" "${begin}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing region start: ${begin}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing region end after '${begin}': ${end}")
  endif()
  string(LENGTH "${end}" end_length)
  math(EXPR b "${b} + ${end_length}")
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

# Occurrences of needle in text (string(FIND) loop, no regex).
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_length)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${needle_length}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" n)
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "${TAG}: ${description}: '${needle}' found ${n} times, expected ${expected}")
  endif()
endfunction()

read_source("aiplayerbot.conf.dist.in" dist)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("GatheringPacePolicy.h" policy)
read_source("GatheringTracePolicy.h" trace_policy)
read_source("LootObjectStack.h" stack_h)
read_source("LootObjectStack.cpp" stack_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/LootAction.cpp" loot_cpp)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("strategy/values/HasAvailableLootValue.h" has_loot_h)
read_source("strategy/triggers/LootTriggers.cpp" loot_trig_cpp)
read_source("strategy/actions/MoveToTravelTargetAction.cpp" travel_pause_cpp)
read_source("strategy/actions/MoveToRpgTargetAction.cpp" rpg_pause_cpp)
read_source("strategy/actions/GuildCreateActions.cpp" guild_pause_cpp)
read_source("strategy/actions/AddLootAction.cpp" add_loot_cpp)
read_source("strategy/values/TravelValues.cpp" travel_values_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_travel_cpp)
read_source("TravelMgr.cpp" travel_mgr_cpp)
read_source("GatherPurposePolicy.h" purpose_policy)
read_module("tests.cmake" tests_cmake)
read_module("t/gathering_pace_policy_tests.cpp" unit_test)

# The reviewed texts (byte-exact pins). pin_order_base is OrderByDistance as it was before T28b (release/9.x 0582f26e).
set(pin_order_base [==[
std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    size_t beforeShrink = availableLoot.size();
    availableLoot.shrink(time(0) - 30);
    if (availableLoot.size() < beforeShrink)
        sLog.outDebug("[BOT LOOT] %s: loot stack expired %zu corpse(s) (>30s old, dropped before looting)",
            bot->GetName(), beforeShrink - availableLoot.size());

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);
    return result;
}
]==])
set(pin_order [==[
std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    size_t beforeShrink = availableLoot.size();
    availableLoot.shrink(time(0) - 30);
    if (availableLoot.size() < beforeShrink)
        sLog.outDebug("[BOT LOOT] %s: loot stack expired %zu corpse(s) (>30s old, dropped before looting)",
            bot->GetName(), beforeShrink - availableLoot.size());

    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): the one place the reach rule lives - the "has available loot" value,
    // LootAction and the travel / rpg pauses all read this list, so they agree on a far node. Off (or out of scope): nullptr.
    PlayerbotAI* const paceAi = (maxDistance > 0 && sPlayerbotAIConfig.gatheringPaceReach) ? GatheringPaceScope(bot) : nullptr;
    uint32 const paceNow = paceAi ? uint32(time(nullptr)) : 0;

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
        else if (paceAi && distance <= float(sPlayerbotAIConfig.gatheringPaceReachYards) && PaceFarOk(paceAi, lootObject, paceNow))
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);
    return result;
}
]==])

set(pin_t28b [==[
// twow-repo#333 T28b begin (AiPlayerbot.Gathering.PaceReach, plan A3-PLAN-ZIEL T28b, OB-00 09.11.2026; GatheringPacePolicy.h).
// Writes only this bot's far-pick state (its loot stack), its [Gathering] window counters and - when a far pick ends without
// an open (20 s, or a strong hostile near its node) - its "loot target". No travel target, quest, strategy, chance, drop or price.
PlayerbotAI* ai::GatheringPaceScope(Player* bot)
{
    PlayerbotAI* const botAi = bot ? GetBotAI(bot) : nullptr;
    if (!botAi || !ai::cohort::Applies(sPlayerbotAIConfig.gatheringPaceReach, bot->GetGUIDLow()) || bot->GetGroup() ||
        !IsRosterBotOnItsOwn(botAi))
        return nullptr;
    return botAi;
}

// A node beyond the loot distance, within the reach: herbalism / mining behind its target, orange or yellow, not missed, budget
// left, 0 strong hostiles within 30 yd of the node (one scan per node and 10 s).
bool LootObjectStack::PaceFarOk(PlayerbotAI* ai, LootObject& lootObject, uint32 now)
{
    if (!gathering_pace::IsPaceSkill(lootObject.skillId))
        return false;
    // The target of the gather purpose (TravelValues.cpp): min(GetSkillMax, GetSkillMaxForLevel = 5 x level).
    uint32 const value = bot->GetSkillValue(lootObject.skillId);
    if (!gathering_pace::Behind(value, bot->GetSkillMax(lootObject.skillId), bot->GetSkillMaxForLevel(bot)) ||
        !gathering_pace::FarColorOk(lootObject.reqSkillValue, value))
        return false;
    uint64 const guid = lootObject.guid.GetRawValue();
    if (!pace.FarOk(guid, now))
        return false;
    // Not yet picked: offered only while "loot available" can fire for it (no "all targets", not mounted, LootTriggers.cpp) -
    // else the travel / rpg pause would hold for a node LootAction never picks, and its clock (from the pick) never starts.
    AiObjectContext* const context = ai->GetAiObjectContext();
    if (pace.pending != guid && (!context->GetValue<std::list<ObjectGuid>>("all targets")->Get().empty() ||
        context->GetValue<bool>("mounted", "self target")->Get()))
        return false;
    int clear = pace.HostilesClear(guid, now);
    if (clear < 0)
    {
        WorldObject* wo = lootObject.GetWorldObject(bot);
        clear = wo ? 1 : 0;
        if (wo)
            for (Unit* hostile : ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, gathering_pace::HostileYards))
                if (gathering_pace::Strong(bot->GetLevel(), hostile->GetLevel()))
                {
                    clear = 0;
                    break;
                }
        pace.PutHostiles(guid, now, clear == 1);
    }
    return clear == 1;
}

void ai::GatheringPacePick(PlayerbotAI* ai, LootObject& lootObject)
{
    Player* bot = ai->GetBot();
    if (!GatheringPaceScope(bot))
        return;
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    uint32 const now = uint32(time(nullptr));
    uint64 const guid = lootObject.guid.GetRawValue();
    WorldObject* wo = lootObject.GetWorldObject(bot);
    bool const far = wo && bot->GetDistance(wo) > sPlayerbotAIConfig.lootDistance;
    if (pace.pending && pace.pending != guid && pace.Leave(now))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);
    if (far && pace.Pick(guid, now))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Pick);
}

bool ai::GatheringPaceTimeout(PlayerbotAI* ai, LootObject const& lootObject)
{
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    uint64 const guid = lootObject.guid.GetRawValue();
    uint32 const now = uint32(time(nullptr));
    if (!pace.EndDue(guid, now))
        return false;
    // Within the loot distance the near rules apply as before: a node reached at 19.8 s still opens (and counts as a far open).
    Player* bot = ai->GetBot();
    LootObject target = lootObject;
    WorldObject* wo = target.GetWorldObject(bot);
    if (wo && bot->GetDistance(wo) <= sPlayerbotAIConfig.lootDistance)
        return false;
    // MoveToLootAction follows the loot target without a distance cap: the far pick ends here.
    pace.End(guid, now);
    ai->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(LootObject());
    ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);
    return true;
}

void ai::GatheringPaceOpened(PlayerbotAI* ai, LootObject const& lootObject)
{
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    if (pace.Opened(lootObject.guid.GetRawValue()))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Open);
}
// twow-repo#333 T28b end]==])

set(pin_state [==[
struct State
{
    struct Miss
    {
        uint64_t guid = 0;
        uint32_t until = 0;
    };
    struct Hostiles
    {
        uint64_t guid = 0;
        uint32_t at = 0;
        bool clear = false;
    };

    Miss misses[MissSlots];
    uint32_t nextMiss = 0;
    Hostiles hostiles[HostileCacheSlots];
    uint32_t nextHostiles = 0;
    uint64_t pending = 0;      // the far pick waiting for its open (0 = none)
    uint32_t pickedAt = 0;
    uint32_t pickTimes[FarPicksPerHour] = {};   // the last far picks, oldest at nextPick (0 = free)
    uint32_t nextPick = 0;

    bool Missed(uint64_t guid, uint32_t now) const
    {
        for (Miss const& miss : misses)
            if (miss.guid == guid && now < miss.until)
                return true;
        return false;
    }

    void AddMiss(uint64_t guid, uint32_t now)
    {
        for (Miss& miss : misses)
            if (miss.guid == guid)
            {
                miss.until = now + MissSeconds;
                return;
            }
        misses[nextMiss] = Miss{ guid, now + MissSeconds };
        nextMiss = (nextMiss + 1) % MissSlots;
    }

    // Sliding budget: a new far pick is allowed while fewer than FarPicksPerHour picks lie in the last HourSeconds, i.e. the
    // oldest of the last FarPicksPerHour picks is free or at least HourSeconds old.
    bool BudgetLeft(uint32_t now) const
    {
        uint32_t const oldest = pickTimes[nextPick];
        return !oldest || now - oldest >= HourSeconds;
    }

    uint32_t PicksLastHour(uint32_t now) const
    {
        uint32_t n = 0;
        for (uint32_t at : pickTimes)
            if (at && now - at < HourSeconds)
                ++n;
        return n;
    }

    bool PendingExpired(uint64_t guid, uint32_t now) const
    {
        return pending && pending == guid && now - pickedAt >= TimeoutSeconds;
    }

    // May this node be a far pick now: not missed, not a timed-out pick, and either the pick already counted or budget left.
    bool FarOk(uint64_t guid, uint32_t now) const
    {
        if (!guid || Missed(guid, now) || PendingExpired(guid, now))
            return false;
        return pending == guid || BudgetLeft(now);
    }

    // The cached hostile check of a node: -1 unknown or older than HostileCacheSeconds, 0 a strong hostile, 1 clear.
    int HostilesClear(uint64_t guid, uint32_t now) const
    {
        for (Hostiles const& entry : hostiles)
            if (entry.guid == guid && now - entry.at < HostileCacheSeconds)
                return entry.clear ? 1 : 0;
        return -1;
    }

    void PutHostiles(uint64_t guid, uint32_t now, bool clear)
    {
        for (Hostiles& entry : hostiles)
            if (entry.guid == guid)
            {
                entry.at = now;
                entry.clear = clear;
                return;
            }
        hostiles[nextHostiles] = Hostiles{ guid, now, clear };
        nextHostiles = (nextHostiles + 1) % HostileCacheSlots;
    }

    // LootAction set the loot target to a far node: the clock starts here. true = a new far pick (counted once per pick). The
    // caller lets a different pending pick go first (Leave).
    bool Pick(uint64_t guid, uint32_t now)
    {
        if (!guid || pending == guid)
            return false;
        pickTimes[nextPick] = now;
        nextPick = (nextPick + 1) % FarPicksPerHour;
        pending = guid;
        pickedAt = now;
        return true;
    }

    // LootAction set a near target: a pending far pick goes; true = it had timed out (it goes on the miss list).
    bool Leave(uint32_t now)
    {
        if (!pending)
            return false;
        bool const timedOut = now - pickedAt >= TimeoutSeconds;
        if (timedOut)
            AddMiss(pending, now);
        pending = 0;
        return timedOut;
    }

    // The open of this node started: true = it was the pending far pick.
    bool Opened(uint64_t guid)
    {
        if (!pending || pending != guid)
            return false;
        pending = 0;
        return true;
    }

    // The pending far pick ends without an open: TimeoutSeconds after its pick, or the cached check saw a strong hostile near
    // its node. The caller skips it while the bot is within the loot distance of the node (the near rules apply there).
    bool EndDue(uint64_t guid, uint32_t now) const
    {
        return PendingExpired(guid, now) || (pending && pending == guid && HostilesClear(guid, now) == 0);
    }

    // The caller ends the far pick (it resets the loot target): the node goes on the miss list.
    void End(uint64_t guid, uint32_t now)
    {
        AddMiss(guid, now);
        pending = 0;
    }
};
]==])

set(pin_distblock [==[
# twow-repo#333 T28b (CLI-BERUFE 09.11.2026; plan A3-PLAN-ZIEL T28b, OB-00 09.11.2026 T28 Q1 (a): built with default 0, A/B in W8
# as an in-realm crossover with modes 2 / 3): the mining / herbalism reach. With the mode on for the bot, a roster bot on its own
# (the scope of [ProfessionUse] / Gathering.Trace, and not in a group) whose herbalism or mining skill is below
# min(rank cap, 5 x level) - the target of the gather purpose - may pick an orange or yellow herb / ore node (skill below the
# node's required value + 50) up to Gathering.PaceReachYards away instead of the LootDistance (25 yd). The rule sits in the one
# shared loot list (LootObjectStack::OrderByDistance), so the loot trigger, the loot pick and the travel / rpg loot pauses agree.
# Limits of a far pick (a node beyond the LootDistance that only this key made eligible): 0 strong hostiles (level >= bot
# level - 7) within 30 yd of the node (checked once per node and 10 s); at most 12 far picks in any 3600 s of the bot
# (sliding); not opened 20 s after the pick (the clock starts when the bot picks it as its loot target), or a strong hostile
# seen near the node on the way -> while the bot is still beyond the LootDistance the loot target is reset and the node goes
# on a miss list (8 entries, 600 s each); once within the LootDistance the near rules apply as before. Far picks need the
# "loot available" trigger, so they happen only while the bot is not mounted, not in combat and its "all targets" list is
# empty. Nodes within the LootDistance are picked exactly as before.
# Does not change: travel targets, gather purposes, quest order, skill-up chances, drops, prices, the queue-time node scan
# (AddLootAction). Skinning is excluded, so are cooking, survival and fishing (owner W2) and every other lock.
# Proof: with Gathering.Trace = 1 the hourly [Gathering] event=share line counts far_picks= far_opens= far_timeouts=
# (far_timeouts: far picks ended without an open, by the 20 s or a strong hostile).
# Mode: 0 = off (default), 1 = all bots, 2 = only cohort B (odd guid), 3 = only cohort A (even guid). Any other value acts like 0
# ("[Gathering] config key=PaceReach value= result=off" error line). With 0, or for a bot outside its cohort, nothing changes.
AiPlayerbot.Gathering.PaceReach = 0
# The far reach in yards, 30 to 60; any other value is 45 ("[Gathering] config key=PaceReachYards value= result=45" error line).
# A reach not beyond the LootDistance makes no far pick ("... result=no far pick" error line with the mode on).
AiPlayerbot.Gathering.PaceReachYards = 45
]==])

# 1. The switch: documented right after Gathering.Trace (away from the combat-rule comment), defaults 0 / 45; members; the
#    loaders with their clamps and error lines right after the Gathering.Trace loader; ConfigEcho in sorted position.
require_text("${dist}" "\nAiPlayerbot.Gathering.PaceReach = 0\n" "dist key, default 0")
require_text("${dist}" "\nAiPlayerbot.Gathering.PaceReachYards = 45\n" "dist reach, default 45")
forbid_text("${dist}" "\nAiPlayerbot.Gathering.PaceReach = 1" "dist default 1")
forbid_text("${dist}" "\nAiPlayerbot.Gathering.PaceReach = 2" "dist default 2")
forbid_text("${dist}" "\nAiPlayerbot.Gathering.PaceReach = 3" "dist default 3")
require_order("${dist}" "\nAiPlayerbot.Gathering.Trace = 0\n# twow-repo#333 T28b (CLI-BERUFE 09.11.2026; " "\nAiPlayerbot.Gathering.PaceReach = 0\n# The far reach in yards, 30 to 60; any other value is 45" "dist block right after Gathering.Trace")
require_text("${dist}" "# Mode: 0 = off (default), 1 = all bots, 2 = only cohort B (odd guid), 3 = only cohort A (even guid). Any other value acts like 0" "dist modes")
require_text("${dist}" "0 strong hostiles (level >= bot\n# level - 7) within 30 yd of the node (checked once per node and 10 s)" "dist limits: hostile rule")
require_text("${dist}" "at most 12 far picks in any 3600 s of the bot\n# (sliding); not opened 20 s after the pick" "dist limits: budget and timeout")
require_text("${dist}" "miss list (8 entries, 600 s each)" "dist limits: miss list")
require_text("${dist}" "# Does not change: travel targets, gather purposes, quest order, skill-up chances, drops, prices" "dist: what it does not change")
require_text("${dist}" "Skinning is excluded, so are cooking, survival and fishing (owner W2)" "dist: excluded skills")
require_text("${dist}" "#     s182=<value>/<max> s186=<value>/<max> s393=<value>/<max>\n#     far_picks= far_opens= far_timeouts=" "dist: far counters appended to the share line format")
between("${dist}" "# twow-repo#333 T28b (CLI-BERUFE 09.11.2026;" "\nAiPlayerbot.Gathering.PaceReachYards = 45\n" dist_block)
require_equal("${dist_block}" "${pin_distblock}" "the dist block")

require_text("${config_h}" "    uint32 gatheringPaceReach = 0;\n    uint32 gatheringPaceReachYards = 45;\n" "config members, defaults 0 / 45")
set(loader "    gatheringPaceReach = config.GetIntDefault(\"AiPlayerbot.Gathering.PaceReach\", 0);\n    if (gatheringPaceReach > 3)\n    {\n        sLog.outError(\"[Gathering] config key=PaceReach value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", gatheringPaceReach);\n        gatheringPaceReach = 0;\n    }\n")
require_text("${config_cpp}" "${loader}" "mode read, > 3 is off with the error line")
require_text("${config_cpp}" "    gatheringPaceReachYards = config.GetIntDefault(\"AiPlayerbot.Gathering.PaceReachYards\", int32(ai::gathering_pace::DefaultYards));\n    if (!ai::gathering_pace::YardsValid(gatheringPaceReachYards))\n    {\n        sLog.outError(\"[Gathering] config key=PaceReachYards value=%d result=%u (30 to 60 yd)\", int32(gatheringPaceReachYards), ai::gathering_pace::DefaultYards);\n        gatheringPaceReachYards = ai::gathering_pace::Yards(gatheringPaceReachYards);\n    }\n" "reach read, out of range (negative included) -> default with the error line")
require_text("${config_cpp}" "        gatheringPaceReachYards = ai::gathering_pace::Yards(gatheringPaceReachYards);\n    }\n    // A reach not beyond the LootDistance makes no far pick: say so (the values stay as set).\n    if (gatheringPaceReach && float(gatheringPaceReachYards) <= lootDistance)\n        sLog.outError(\"[Gathering] config key=PaceReachYards value=%u result=no far pick (LootDistance %.1f yd)\", gatheringPaceReachYards, lootDistance);\n" "reach not beyond the LootDistance: error line")
require_order("${config_cpp}" "    lootDistance = config.GetFloatDefault(\"AiPlayerbot.LootDistance\"" "float(gatheringPaceReachYards) <= lootDistance" "LootDistance read before the reach test")
require_text("${config_cpp}" "        gatheringTrace = 0;\n    }\n    // twow-repo#333 T28b (OB-00 09.11.2026, T28 Q1 (a)): " "loader right after the Gathering.Trace loader")
require_order("${config_cpp}" "A3 step 5 (ADR-0041): the combat rule" "gatheringPaceReach = config.GetIntDefault(" "loader far below the combat-rule comment")
require_count("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.Gathering.PaceReach\"" 1 "mode read once")
require_count("${config_cpp}" "{\"Gathering.PaceReach\", Value(gatheringPaceReach)}," 1 "ConfigEcho PaceReach once")
require_count("${config_cpp}" "{\"Gathering.PaceReachYards\", Value(gatheringPaceReachYards)}," 1 "ConfigEcho PaceReachYards once")
require_text("${config_cpp}" "{\"Gather.QuestHerbFilter\", Value(gatherQuestHerbFilter)},\n            {\"Gathering.PaceReach\", Value(gatheringPaceReach)},\n            {\"Gathering.PaceReachYards\", Value(gatheringPaceReachYards)},\n            {\"Gathering.Trace\", Value(gatheringTrace)},\n" "ConfigEcho in sorted order")

# 2. The pure policy: standard headers only, no config, no logging, no game types; the limits; herbalism / mining only; the
#    per-bot state (eligibility, sliding budget, miss list, hostile cache, pick clock, end of a far pick) byte-exact.
require_count("${policy}" "#include \"" 0 "only standard headers (<...>) in the pure policy")
forbid_text("${policy}" "sPlayerbotAIConfig" "config read in the pure policy")
forbid_text("${policy}" "sLog" "logging in the pure policy")
forbid_text("${policy}" "Player*" "game types in the pure policy")
require_text("${policy}" "namespace ai::gathering_pace" "policy namespace")
foreach(limit "kHerbalism = 182;" "kMining = 186;" "DefaultYards = 45;" "MinYards = 30;" "MaxYards = 60;" "FarPicksPerHour = 12;"
    "HourSeconds = 3600;" "TimeoutSeconds = 20;" "MissSlots = 8;" "MissSeconds = 600;" "HostileCacheSeconds = 10;" "StrongLevelGap = 7;")
  require_text("${policy}" "constexpr uint32_t ${limit}" "limit ${limit}")
endforeach()
require_text("${policy}" "constexpr float HostileYards = 30.0f;" "hostile radius 30 yd")
between("${policy}" "inline bool IsPaceSkill(uint32_t skill)" "\n}\n" pace_skill)
require_text("${pace_skill}" "    return skill == kHerbalism || skill == kMining;\n}" "herbalism and mining only")
foreach(excluded "393" "SKINNING" "185" "COOKING" "142" "SURVIVAL" "356" "FISHING" "129")
  forbid_text("${pace_skill}" "${excluded}" "excluded skill in IsPaceSkill")
endforeach()
require_text("${policy}" "    uint32_t const target = skillMax < levelMax ? skillMax : levelMax;\n    return value < target;" "lag rule: below min(rank cap, 5 x level)")
require_text("${policy}" "    return value < reqSkillValue + 50;" "orange / yellow only")
require_text("${policy}" "    return !(botLevel > hostileLevel + StrongLevelGap);" "strong hostile: level >= bot level - 7")
require_text("${policy}" "    return raw >= MinYards && raw <= MaxYards;" "reach range")
between("${policy}" "struct State\n{" "\n};\n" state)
require_text("${state}" "        if (!guid || Missed(guid, now) || PendingExpired(guid, now))\n            return false;\n        return pending == guid || BudgetLeft(now);" "eligibility: miss list, timed-out pick, budget")
require_text("${state}" "        uint32_t const oldest = pickTimes[nextPick];\n        return !oldest || now - oldest >= HourSeconds;" "sliding budget: 12 in any 3600 s")
require_text("${state}" "        return pending && pending == guid && now - pickedAt >= TimeoutSeconds;" "timeout counted from the pick")
require_text("${state}" "            if (entry.guid == guid && now - entry.at < HostileCacheSeconds)" "hostile cache expires after 10 s")
require_text("${state}" "        if (timedOut)\n            AddMiss(pending, now);" "a pick left after 20 s goes on the miss list")
require_text("${state}" "        return PendingExpired(guid, now) || (pending && pending == guid && HostilesClear(guid, now) == 0);" "end: 20 s after the pick or a strong hostile near the pending node")
require_text("${state}" "    void End(uint64_t guid, uint32_t now)\n    {\n        AddMiss(guid, now);\n        pending = 0;\n    }" "an ended far pick goes on the miss list")
between("${state}" "    bool Pick(uint64_t guid, uint32_t now)" "\n    }\n" pick)
require_order("${pick}" "if (!guid || pending == guid)\n            return false;" "pickedAt = now;" "a re-pick neither counts nor restarts the clock")
require_equal("${state}" "${pin_state}" "the per-bot far-pick state (gathering_pace::State)")

# 3. The ONE hook: LootObjectStack::OrderByDistance, one key test, after the IsLootPossible check; near entries exactly as before.
between("${stack_cpp}" "std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)" "\n}\n" order_body)
set(key_comment "    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): the one place the reach rule lives - the \"has available loot\" value,\n    // LootAction and the travel / rpg pauses all read this list, so they agree on a far node. Off (or out of scope): nullptr.\n")
set(key_line "    PlayerbotAI* const paceAi = (maxDistance > 0 && sPlayerbotAIConfig.gatheringPaceReach) ? GatheringPaceScope(bot) : nullptr;\n    uint32 const paceNow = paceAi ? uint32(time(nullptr)) : 0;\n")
set(far_branch "        else if (paceAi && distance <= float(sPlayerbotAIConfig.gatheringPaceReachYards) && PaceFarOk(paceAi, lootObject, paceNow))\n            sortedMap[distance] = lootObject;\n")
require_text("${order_body}" "${key_line}" "one key test, only with a distance limit (Add's trim pass stays out)")
require_count("${order_body}" "sPlayerbotAIConfig.gatheringPaceReach)" 1 "one key test in OrderByDistance")
require_count("${order_body}" "sPlayerbotAIConfig.gatheringPaceReach" 2 "the key and the reach only")
require_text("${order_body}" "        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));\n        if (!maxDistance || distance <= maxDistance)\n            sortedMap[distance] = lootObject;\n${far_branch}" "near entries unchanged, far entries only through the lever")
require_order("${order_body}" "        if (!lootObject.IsLootPossible(bot))\n            continue;\n" "else if (paceAi && " "the pace gate after IsLootPossible")
require_order("${order_body}" "availableLoot.shrink(time(0) - 30);" "${key_line}" "the 30 s expiry unchanged before the gate")
require_count("${stack_cpp}" "PaceFarOk(" 2 "PaceFarOk: one call, one definition")
require_equal("${order_body}" "${pin_order}" "LootObjectStack::OrderByDistance")
#    Neutrality of the 0-path: without the T28b lines (comment, key line, far branch) the body is the old one, byte for byte -
#    with the key 0 paceAi is nullptr, so no scope test, no time(), no value read, no scan.
string(REPLACE "${key_comment}${key_line}\n" "" order_without "${order_body}")
string(REPLACE "${far_branch}" "" order_without "${order_without}")
require_equal("${order_without}" "${pin_order_base}" "OrderByDistance without the T28b lines (the 0-path)")
#    The T28b helpers (scope, far rule, pick clock, end, open), byte-exact; the semantic pins below name what each must keep.
between("${stack_cpp}" "// twow-repo#333 T28b begin" "// twow-repo#333 T28b end" t28b)
require_equal("${t28b}" "${pin_t28b}" "the T28b helpers in LootObjectStack.cpp")
#    Scope: cohort mode, not grouped, a roster bot on its own (cheap tests first).
between("${stack_cpp}" "PlayerbotAI* ai::GatheringPaceScope(Player* bot)" "\n}\n" scope)
require_text("${scope}" "    if (!botAi || !ai::cohort::Applies(sPlayerbotAIConfig.gatheringPaceReach, bot->GetGUIDLow()) || bot->GetGroup() ||\n        !IsRosterBotOnItsOwn(botAi))\n        return nullptr;\n    return botAi;" "scope: its cohort, not grouped, a roster bot on its own")
#    Eligibility of a far node: skill, lag, colour, miss / budget / timeout, pickable now, then the cached hostile scan.
between("${stack_cpp}" "bool LootObjectStack::PaceFarOk(PlayerbotAI* ai, LootObject& lootObject, uint32 now)" "\n}\n" far_ok)
require_text("${far_ok}" "    if (!gathering_pace::IsPaceSkill(lootObject.skillId))\n        return false;\n" "herbalism / mining only (no skinning, cooking, survival, fishing)")
require_count("${far_ok}" "skillId" 3 "the skill id only for the skill gate, the value and the max")
require_text("${far_ok}" "    if (!gathering_pace::Behind(value, bot->GetSkillMax(lootObject.skillId), bot->GetSkillMaxForLevel(bot)) ||\n        !gathering_pace::FarColorOk(lootObject.reqSkillValue, value))\n        return false;\n" "lag and colour gate")
require_text("${far_ok}" "    if (!pace.FarOk(guid, now))\n        return false;\n" "miss list, timed-out pick, budget gate")
require_text("${far_ok}" "    if (pace.pending != guid && (!context->GetValue<std::list<ObjectGuid>>(\"all targets\")->Get().empty() ||\n        context->GetValue<bool>(\"mounted\", \"self target\")->Get()))\n        return false;\n" "an unpicked far node only while the loot trigger can fire")
require_order("${far_ok}" "pace.HostilesClear(guid, now)" "ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, gathering_pace::HostileYards)" "cache before the scan")
require_text("${far_ok}" "                if (gathering_pace::Strong(bot->GetLevel(), hostile->GetLevel()))\n                {\n                    clear = 0;\n                    break;\n                }\n        pace.PutHostiles(guid, now, clear == 1);\n    }\n    return clear == 1;\n}" "0 strong hostiles, the scan cached, the result returned")
require_count("${stack_cpp}" "GetAllHostileNPCNonPetUnitsAroundWO" 1 "one hostile scan site")
#    The callers read the shared list unchanged; no second reach rule anywhere.
require_text("${has_loot_h}" "AI_VALUE(LootObjectStack*, \"available loot\")->CanLoot(sPlayerbotAIConfig.lootDistance);" "has available loot unchanged")
require_text("${loot_cpp}" "std::vector<LootObject> candidates = AI_VALUE(LootObjectStack*, \"available loot\")->OrderByDistance(sPlayerbotAIConfig.lootDistance);" "LootAction list unchanged")
require_text("${travel_pause_cpp}" "LootObject lootObject = AI_VALUE(LootObjectStack*, \"available loot\")->GetLoot(sPlayerbotAIConfig.lootDistance);" "travel pause unchanged")
require_text("${rpg_pause_cpp}" "LootObject lootObject = AI_VALUE(LootObjectStack*, \"available loot\")->GetLoot(sPlayerbotAIConfig.lootDistance);" "rpg pause unchanged")
require_text("${guild_pause_cpp}" "LootObject lootObject = AI_VALUE(LootObjectStack*, \"available loot\")->GetLoot(sPlayerbotAIConfig.lootDistance);" "guild-create pause unchanged")
forbid_text("${loot_cpp}" "PaceFarOk" "a second reach rule in LootAction.cpp")
forbid_text("${loot_cpp}" "GetAllHostileNPCNonPetUnitsAroundWO" "a hostile rule in LootAction.cpp")
forbid_text("${move_cpp}" "PaceFarOk" "a second reach rule in MovementActions.cpp")

# 4. The clock: LootAction starts it at the pick (after the loot target is set); MoveToLootAction (no distance cap) and the open
#    end a far pick 20 s after the pick, or with a strong hostile near its node, while the bot is beyond the loot distance:
#    "loot target" reset, the node on the miss list.
between("${loot_cpp}" "bool LootAction::Execute(Event& event)" "\n}\n" pick_body)
require_text("${pick_body}" "    context->GetValue<LootObject>(\"loot target\")->Set(lootObject);\n    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): a far pick's 20 s clock starts at this pick.\n    if (sPlayerbotAIConfig.gatheringPaceReach)\n        GatheringPacePick(ai, lootObject);\n    return true;\n}" "the clock starts at the pick")
between("${loot_cpp}" "bool OpenLootAction::Execute(Event& event)" "\n}\n" open_body)
require_text("${open_body}" "bool OpenLootAction::Execute(Event& event)\n{\n    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): a far pick not opened 20 s after its pick, or with a strong hostile\n    // near its node, ends before another try while the bot is beyond the loot distance (loot target reset, the node on the miss\n    // list), with the react delay as every run.\n    if (sPlayerbotAIConfig.gatheringPaceReach && GatheringPaceTimeout(ai, AI_VALUE(LootObject, \"loot target\")))\n    {\n        SetDuration(sPlayerbotAIConfig.reactDelay);\n        return false;\n    }\n    // twow-repo#333 P1-w:" "open site: the end check first, before another open try")
require_text("${open_body}" "    if (result)\n    {\n        // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): the started herb / ore cast on a pending far pick ends its clock.\n        if (sPlayerbotAIConfig.gatheringPaceReach && castDuration)\n            GatheringPaceOpened(ai, lootObject);\n        AI_VALUE(LootObjectStack*, \"available loot\")->Remove(lootObject.guid);\n" "open site: a started cast ends the clock")
require_count("${loot_cpp}" "sPlayerbotAIConfig.gatheringPaceReach" 3 "three key tests in LootAction.cpp (pick, end, open)")
require_count("${loot_cpp}" "GatheringPace" 3 "three T28b calls in LootAction.cpp")
between("${move_cpp}" "bool MoveToLootAction::Execute(Event& event)" "\n}\n" move_body)
require_text("${move_body}" "    LootObject loot = AI_VALUE(LootObject, \"loot target\");\n    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): this follows the loot target without a distance cap - a far pick\n    // not opened 20 s after its pick, or with a strong hostile near its node, ends here while the bot is beyond the loot\n    // distance (loot target reset, the node on the miss list).\n    if (sPlayerbotAIConfig.gatheringPaceReach && GatheringPaceTimeout(ai, loot))\n        return false;\n    if (!loot.IsLootPossible(bot))" "end check first in MoveToLootAction")
require_count("${move_cpp}" "sPlayerbotAIConfig.gatheringPaceReach" 1 "one key test in MovementActions.cpp")
require_count("${move_cpp}" "GatheringPace" 1 "one T28b call in MovementActions.cpp")
between("${stack_cpp}" "bool ai::GatheringPaceTimeout(PlayerbotAI* ai, LootObject const& lootObject)" "\n}\n" timeout)
require_text("${timeout}" "    if (!pace.EndDue(guid, now))\n        return false;\n" "the end only when due (nothing pending: no world lookup)")
require_text("${timeout}" "    if (wo && bot->GetDistance(wo) <= sPlayerbotAIConfig.lootDistance)\n        return false;\n" "within the loot distance the near rules apply")
require_order("${timeout}" "if (!pace.EndDue(guid, now))" "bot->GetDistance(wo) <= sPlayerbotAIConfig.lootDistance" "due test before the distance test")
require_order("${timeout}" "bot->GetDistance(wo) <= sPlayerbotAIConfig.lootDistance" "    pace.End(guid, now);\n    ai->GetAiObjectContext()->GetValue<LootObject>(\"loot target\")->Set(LootObject());\n    ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);\n    return true;" "the end: miss list, loot target reset, counted")
between("${stack_cpp}" "void ai::GatheringPaceOpened(PlayerbotAI* ai, LootObject const& lootObject)" "\n}\n" opened)
require_text("${opened}" "    if (pace.Opened(lootObject.guid.GetRawValue()))\n        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Open);" "a started cast ends the clock")
between("${stack_cpp}" "void ai::GatheringPacePick(PlayerbotAI* ai, LootObject& lootObject)" "\n}\n" pick_helper)
require_order("${pick_helper}" "    if (!GatheringPaceScope(bot))\n        return;" "pace.Pick(guid, now)" "pick counted only in scope")
require_text("${pick_helper}" "bool const far = wo && bot->GetDistance(wo) > sPlayerbotAIConfig.lootDistance;" "a far pick = beyond the loot distance")
require_text("${pick_helper}" "    if (pace.pending && pace.pending != guid && pace.Leave(now))\n        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);\n    if (far && pace.Pick(guid, now))\n        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Pick);\n}" "another target lets the pending pick go; only a far target starts the clock")

# 5. Neutrality of the T28b code: it writes the bot's own far-pick state, its window counters and (end only) its loot target.
require_count("${t28b}" "->Set(" 1 "only the loot target is written")
foreach(write "SET_AI_VALUE" "RESET_AI_VALUE" "\"travel target\"" "SetStatus(" "SetTarget(" "SetExpire" "ClearValues" "ChangeStrategy"
    "ResetStrategies" "CastSpell(" "DoSpecificAction" "MoveTo(" "MoveNear(" "TellMaster" "TellError" "SetSelection" "->Remove(" "->Clear"
    "bot->Set" "Interrupt" "GetGatherPurpose" "SetSkill" "sLog.out")
  forbid_text("${t28b}" "${write}" "write in the T28b code")
endforeach()
foreach(text ai_cpp ai_h)
  foreach(token "gatheringPace" "GatheringPace" "gathering_pace" "Pace()")
    forbid_text("${${text}}" "${token}" "T28b code in PlayerbotAI")
  endforeach()
endforeach()
require_text("${stack_h}" "#include \"playerbot/GatheringPacePolicy.h\"" "policy include in LootObjectStack.h")
require_text("${stack_h}" "        gathering_pace::State pace;\n" "far-pick state on the bot's loot stack")
#    No T28b token outside its files (a second reach rule, a state write from UpdateAI or a pause, ...).
set(t28b_files "GatheringPacePolicy.h" "GatheringTracePolicy.h" "LootObjectStack.cpp" "LootObjectStack.h" "PlayerbotAIConfig.cpp"
  "PlayerbotAIConfig.h" "strategy/actions/LootAction.cpp" "strategy/actions/MovementActions.cpp")
file(GLOB_RECURSE pb_sources LIST_DIRECTORIES false "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(LENGTH pb_sources pb_source_count)
if(pb_source_count LESS 100)
  message(FATAL_ERROR "${TAG}: source scan found only ${pb_source_count} files under ${PB_SOURCE_DIR}")
endif()
foreach(source IN LISTS pb_sources)
  file(RELATIVE_PATH relative "${PB_SOURCE_DIR}" "${source}")
  if(relative IN_LIST t28b_files)
    continue()
  endif()
  file(STRINGS "${source}" hits REGEX "gatheringPace|GatheringPace|gathering_pace|Pace\\(\\)")
  list(LENGTH hits hit_count)
  if(hit_count GREATER 0)
    message(FATAL_ERROR "${TAG}: T28b code outside its files in ${relative}: ${hits}")
  endif()
endforeach()

# 6. Observability: the far counters at the END of the [Gathering] event=share line (T28-M fields unchanged), only in an open
#    window (Gathering.Trace 1).
require_text("${trace_policy}" "\"opens_skin=%u crafts=%u s182=%u/%u s186=%u/%u s393=%u/%u\"\n        \" far_picks=%u far_opens=%u far_timeouts=%u\"," "far counters appended after s393")
require_text("${trace_policy}" "unsigned(skinning.value), unsigned(skinning.max),\n        unsigned(window.farPicks), unsigned(window.farOpens), unsigned(window.farTimeouts));" "far counter arguments last")
between("${trace_policy}" "    void NoteFar(FarEvent event)" "\n    }\n" note_far)
require_text("${note_far}" "        if (!Open())\n            return;" "far counters only in an open window")

# 7. The unit test, registered like gathering_trace_policy.
require_text("${tests_cmake}" "add_executable(gathering_pace_policy_tests\n  \"\${PB_MODULE_DIR}/t/gathering_pace_policy_tests.cpp\")\n\ntarget_include_directories(gathering_pace_policy_tests PRIVATE\n  \"\${PB_MODULE_DIR}/src/playerbot\")\n" "unit test target")
require_text("${tests_cmake}" "add_test(NAME gathering_pace_policy\n  COMMAND gathering_pace_policy_tests\n  WORKING_DIRECTORY \"\${CMAKE_BINARY_DIR}\")" "unit test registered")
require_text("${unit_test}" "#include \"GatheringPacePolicy.h\"" "unit test includes the policy")
require_text("${unit_test}" "timeout due at 20 s after the pick" "unit test: timeout from the pick")
require_text("${unit_test}" "a strong hostile near the pending node ends it" "unit test: hostile end")
require_text("${unit_test}" "13th node: no budget" "unit test: hourly budget")
require_text("${unit_test}" "sliding: 12 per hour" "unit test: sliding budget")
require_text("${unit_test}" "no cooking, survival, fishing (W2)" "unit test: excluded skills")
require_text("${unit_test}" "far counters appended after s393" "unit test: line tail")

message(STATUS "${TAG} source contract passed")
