# twow-repo#541 (owner 11.10.2026, measurement only): AiPlayerbot.PacketTrace (default 0). Counts every packet the
# CanPacketSend hook sees per receiver (bot read / bot ignored / real), map bucket and class; thread-local with
# batched flush; one [BotPackets] line per bucket and minute. Off: the hook does what it did before.

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
    message(FATAL_ERROR "#541 packet trace: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 packet trace: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#541 packet trace: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#541 packet trace: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotScripts.cpp" scripts)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("PacketTracePolicy.h" policy)

require_text("${config_h}" "bool packetTrace = false;" "switch default off")
require_text("${config_cpp}" "packetTrace = config.GetBoolDefault(\"AiPlayerbot.PacketTrace\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.PacketTrace = 0" "documented")

# Hook: counting only with the switch; the old delivery path unchanged after it (X1 contract reads the hook).
between("${scripts}" "bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override" "void OnPacketHandled(" hook)
require_order("${hook}" "if (sPlayerbotAIConfig.packetTrace && TracePacket(player, ai, packet))\n                return false;" "if (!ai)\n                return true;" "counting behind the switch, before the old path")
require_order("${hook}" "if (!ai)\n                return true;" "ai->QueueBotOutgoingPacket(packet);\n            return false;" "old bot path at the end")
between("${scripts}" "bool TracePacket(Player* player, PlayerbotAI* ai, WorldPacket const& packet)" "class PlayerbotWorldScript" trace)
require_text("${trace}" "Receiver const receiver = !ai ? Real : (ai->WantsBotOutgoingPacket(opcode) ? BotRead : BotIgnored);" "read vs ignored by the inbox filter")
require_order("${trace}" "if (!ai || !local.SampleThisBotPacket())\n            return false;" "ai->QueueBotOutgoingPacket(packet);" "only the timed sample queues here")
require_order("${trace}" "ai->QueueBotOutgoingPacket(packet);" "return true;" "sampled packet reported as queued")
string(REGEX MATCHALL "ai->QueueBotOutgoingPacket\\(packet\\)" queues "${trace}")
list(LENGTH queues queue_count)
if(NOT queue_count EQUAL 1)
  message(FATAL_ERROR "#541 packet trace: TracePacket queues at most once (found ${queue_count})")
endif()

# Policy: thread-local, batched.
require_text("${policy}" "thread_local Local local;" "thread-local counters")
require_text("${policy}" "if (++pending >= FlushEvery)\n                Flush(totals);" "batched flush")

# Minute line only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.packetTrace)" "[BotPackets] map=%s kib_bot_read=%llu kib_bot_ignored=%llu kib_real=%llu%s" "minute line behind the switch")
require_text("${mgr}" "[BotPackets] inbox_copy_samples=%llu inbox_copy_avg_ns=%llu sample_every=%u flush_every=%u" "inbox copy cost line")

message(STATUS "packet_trace source contract passed")
