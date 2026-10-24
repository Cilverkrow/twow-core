# twow-repo#628 ScanSlices (AiPlayerbot.BotGroups.AdHoc.ScanSlices, AiPlayerbot.QuestGroups.ScanSlices; default 0): the pass of
# the world-thread group managers over all bots, spread over several manager updates (prepared 24.10.2026 for the case that the
# scan alone breaks 2 ms of the world tick with 540 bots; merged only on evidence). 0 or 1 = the whole pass in one update, with
# exactly the old order: leave rules of all bots, then their candidates, then the pairs / formations.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#628 ScanSlices: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#628 ScanSlices: unexpected ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#628 ScanSlices: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

function(region text first last out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#628 ScanSlices: region start missing: ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${last}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#628 ScanSlices: region end missing: ${last}")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("ScanSlicePolicy.h" policy)
read_source("AdhocGroupWorld.cpp" adhoc)
read_source("AdhocGroupWorld.h" adhoc_h)
read_source("QuestGroupWorld.cpp" s7)
read_source("PlayerbotAIConfig.cpp" config_source)
read_source("aiplayerbot.conf.dist.in" config_template)

# Keys: default 0 (old behaviour), clamped, documented.
require_text("${config_source}" "botGroupsAdhocScanSlices = std::min<uint32>(64, (uint32)config.GetIntDefault(\"AiPlayerbot.BotGroups.AdHoc.ScanSlices\", 0));" "ad-hoc key, default 0")
require_text("${config_source}" "questGroupsScanSlices = std::min<uint32>(64, (uint32)config.GetIntDefault(\"AiPlayerbot.QuestGroups.ScanSlices\", 0));" "S7 key, default 0")
require_text("${config_template}" "AiPlayerbot.BotGroups.AdHoc.ScanSlices = 0" "ad-hoc key documented")
require_text("${config_template}" "AiPlayerbot.QuestGroups.ScanSlices = 0" "S7 key documented")
require_text("${policy}" "std::uint32_t const n = std::max<std::uint32_t>(1, slices);" "0 slices = 1")

# Ad-hoc: a slice per update, the pairs after the last one; candidates by id, resolved again at the pairs.
require_text("${adhoc}" "ScanSlice(bots, scan_slice::SliceEnd(pass.guids.size(), pass.next, sPlayerbotAIConfig.botGroupsAdhocScanSlices), now);" "ad-hoc slice")
require_text("${adhoc}" "    if (pass.next >= pass.guids.size())
    {
        Pairs(bots, now);" "pairs once all slices are done")
region("${adhoc}" "void WorldThreadManager::ScanSlice(" "\n}\n" scan_slice)
require_order("${scan_slice}" "if (CheckLeave(bot, group, now))" "ObjectiveKey const key = inAdhoc ? entry.key : CurrentObjective(bot);" "leave rules of the slice before its candidates (old order)")
require_text("${scan_slice}" "pass.buckets[BucketKey(bot, key)].push_back({ bot->GetGUIDLow(), inAdhoc ? group->GetId() : 0u, key });" "candidate by id")
require_text("${adhoc_h}" "        std::uint32_t guid = 0;
        std::uint32_t groupId = 0;  // its ad-hoc group (led by it) or 0" "no pointers kept across updates")
region("${adhoc}" "void WorldThreadManager::Pairs(" "\n}\n" pairs)
require_order("${pairs}" "if (!Eligible(bot))" "Invite const invite = Decide(" "candidate resolved again before any invite")
require_text("${pairs}" "if (c.groupId ? (!group || group->GetId() != c.groupId || !group->IsLeader(bot->GetObjectGuid()) ||" "group state checked again")
require_text("${adhoc}" "scan_us_max=%u bots=%u slices=%u\"" "ad-hoc minute line shows the slices")

# S7: the same for the scan; FormOne checks every bot again.
require_text("${s7}" "ScanBots(bots, scan_slice::SliceEnd(round.guids.size(), round.next, sPlayerbotAIConfig.questGroupsScanSlices), now);" "S7 slice")
require_text("${s7}" "        if (round.next >= round.guids.size())
        {
            FormRound(bots, now);" "formations once all slices are done")
require_text("${s7}" "    if (!enabled)
        round = ScanRound();" "a switched-off S7 drops a running scan")
require_text("${s7}" "    if (!leader || !Eligible(leader) || leader->GetGroup())
        return false;" "FormOne checks the leader again")
require_text("${s7}" "        if (!bot || !Eligible(bot) || bot->GetGroup())
            continue;" "FormOne checks each member again")
forbid_text("${s7}" "void WorldThreadManager::Scan(" "the old one-piece scan")
require_text("${s7}" "scan_only_us_max=%u bots=%u slices=%u\"" "S7 minute line shows the slices")
