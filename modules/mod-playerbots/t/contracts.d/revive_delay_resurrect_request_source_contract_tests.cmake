# twow-repo#575 H3 (A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix: code default 0 until the acceptance unit passes, then a flip
# pair): AiPlayerbot.Revive.DelayResurrectRequest. SMSG_RESURRECT_REQUEST was registered without the delay flag:
# PacketHandlingHelper::Handle drops a packet whose trigger still holds an event unless its opcode is marked delayed, so a
# second request while the trigger held the first was lost. With the key the opcode is marked delayed (the existing
# AddHandler(..., shouldDelay) path, as SMSG_LOOT_RESPONSE).
# This contract pins the key (dist, member, read, [ConfigEcho] sorted), the one registration with the key as the delay flag,
# the unchanged delay mechanism (Handle keeps a delayed packet whose trigger is busy) and the dead-state guard of the
# accept action.
# Mode (OB-00 09.11.2026, option (a): the acceptance rides along in CLI-GRP's in-realm cohort test T30-1): the key is read
# as 0 off / 1 all / 2 guid bit 1 set / 3 guid bit 1 clear (ResurrectDelayPolicy.h), independent of the CohortPolicy parity,
# so T30-1's parity arms and this key form a balanced 2x2 in one realm.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "DelayResurrectRequest: missing ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle expected description)
  set(rest "${text}")
  set(n 0)
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${len}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "DelayResurrectRequest: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/AcceptResurrectAction.h" accept_h)

# 1. The key: dist default 0, member mode default 0, read as a clamped mode, [ConfigEcho] in sorted place.
require_text("${dist}" "\nAiPlayerbot.Revive.DelayResurrectRequest = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Revive.DelayResurrectRequest =" 1 "one dist line")
# OB-10 review 09.11.: the dist text names when the flag is read.
require_text("${dist}" "# Read when the bot's AI is created (the packet handler table, at login): a config reload affects only bots logged in\n# afterwards.\n" "dist: read at AI creation, reload only for later bots")
require_text("${config_h}" "    uint32 reviveDelayResurrectRequest = 0;" "member: mode, default 0")
require_text("${config_cpp}" "    reviveDelayResurrectRequest = ai::resurrect_delay::ClampMode(config.GetIntDefault(\"AiPlayerbot.Revive.DelayResurrectRequest\", 0));" "read as a clamped mode, default 0")
require_text("${config_cpp}" "#include \"playerbot/ResurrectDelayPolicy.h\"" "config includes the policy")
require_text("${dist}" "#   2 = only bots whose low guid has bit 1 set ((guid >> 1) & 1 = 1), 3 = only bots with bit 1 clear; any other value acts\n#   like 0. 2 / 3 are NOT the cohort B / A modes of other keys (those split by parity, bit 0)" "dist: the bit-1 modes, named as not the parity cohorts")
require_text("${config_cpp}" "            {\"Revive.CorpseRunFirst.MaxSeconds\", Value(reviveCorpseRunFirstMaxSeconds)},\n            {\"Revive.DelayResurrectRequest\", Value(reviveDelayResurrectRequest)},\n            {\"Revive.SpiritHealerFull\", Value(reviveSpiritHealerFull)}," "[ConfigEcho] sorted")

# 2. One registration, the key as the delay flag; loot response keeps its delay.
count_text("${ai_cpp}" "AddHandler(SMSG_RESURRECT_REQUEST," 1 "resurrect request registrations")
require_text("${ai_cpp}" "    botOutgoingPacketHandlers.AddHandler(SMSG_RESURRECT_REQUEST, \"resurrect request\",\n        ai::resurrect_delay::Applies(sPlayerbotAIConfig.reviveDelayResurrectRequest, bot->GetGUIDLow()));" "the delayed registration, per bot by the mode")
require_text("${ai_cpp}" "#include \"playerbot/ResurrectDelayPolicy.h\"" "PlayerbotAI.cpp includes the policy")
# The split is the policy's guid bit 1, never the CohortPolicy parity.
string(FIND "${ai_cpp}" "ai::cohort::Applies(sPlayerbotAIConfig.reviveDelayResurrectRequest" parity_use)
if(NOT parity_use EQUAL -1)
  message(FATAL_ERROR "DelayResurrectRequest: the key must not use the CohortPolicy parity split")
endif()
read_source("${PB_SOURCE_DIR}/ResurrectDelayPolicy.h" policy_h)
require_text("${policy_h}" "    return mode >= 0 && mode <= 3 ? std::uint32_t(mode) : ModeOff;" "policy: unknown mode = off")
require_text("${policy_h}" "    return ((guidLow >> 1) & 1u) != 0;" "policy: guid bit 1")
require_text("${policy_h}" "    return mode == ModeAll || (mode == ModeOnlyBit1Set && Bit1Set(guidLow)) ||\n           (mode == ModeOnlyBit1Clear && !Bit1Set(guidLow));" "policy: Applies")
read_source("${PB_MODULE_DIR}/tests.cmake" tests_cmake)
require_text("${tests_cmake}" "add_test(NAME resurrect_delay_policy\n  COMMAND resurrect_delay_policy_tests" "unit test registered")
require_text("${ai_cpp}" "    botOutgoingPacketHandlers.AddHandler(SMSG_LOOT_RESPONSE, \"loot response\", true);" "the model: loot response delayed")

# 3. The delay mechanism the key relies on (unchanged): AddHandler stores the flag, Handle keeps a delayed packet whose
#    trigger is busy (HandlePacket false) and drops the others.
require_text("${ai_cpp}" "void PacketHandlingHelper::AddHandler(uint16 opcode, std::string handler, bool shouldDelay)\n{\n    handlers[opcode] = handler;\n    delay[opcode] = shouldDelay;\n}" "AddHandler stores the flag")
require_text("${ai_cpp}" "        if (!helper.HandlePacket(handlers, *queue.top()))\n            if (delay[queue.top()->GetOpcode()])\n                delayed.push(std::move(queue.top()));\n        queue.pop();" "Handle keeps a delayed busy packet")
require_text("${ai_cpp}" "    queue = std::move(delayed);" "the kept packets are the next pass's queue")

# 4. Accepting stays a dead-state action (a late request after a revive does nothing).
require_text("${accept_h}" "            if (!sServerFacade.IsAlive(bot))" "accept only when dead")
