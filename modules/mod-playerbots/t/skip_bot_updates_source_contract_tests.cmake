# twow-repo#541 (owner 11.10.2026): AiPlayerbot.Perf.SkipBotUpdatePackets (default 0). For a player flagged by the bot
# module and WITHOUT a client socket the core neither builds nor sends SMSG_UPDATE_OBJECT / COMPRESSED / DESTROY.
# Every bookkeeping step (visible set, broadcast listeners, update masks) stays; only the packet work is skipped.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

if(NOT DEFINED CORE_SOURCE_DIR)
  set(CORE_SOURCE_DIR "${PB_SOURCE_DIR}/../../../..")
endif()

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 skip bot updates: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 skip bot updates: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#541 skip bot updates: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#541 skip bot updates: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Object.cpp" object_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/UpdateData.cpp" update_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${PB_SOURCE_DIR}/PlayerbotMgr.cpp" mgr_cpp)

# Switch default off; the module sets the flag at bot login.
require_text("${config_h}" "bool perfSkipBotUpdatePackets = false;" "switch default off")
require_text("${config_cpp}" "perfSkipBotUpdatePackets = config.GetBoolDefault(\"AiPlayerbot.Perf.SkipBotUpdatePackets\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Perf.SkipBotUpdatePackets = 0" "documented")
require_text("${mgr_cpp}" "bot->SetSkipClientUpdates(sPlayerbotAIConfig.perfSkipBotUpdatePackets);" "set at bot login")

# Never for a player with a client.
require_text("${player_h}" "bool SkipsClientUpdates() const { return m_skipClientUpdates.load(std::memory_order_relaxed) && !HasClientSocket(); }" "client socket wins")

# Skip points: packet work only.
between("${object_cpp}" "void Object::BuildUpdateDataForPlayer(Player* pl, UpdateDataMapType& update_players)" "void Object::AddToClientUpdateList()" values)
require_order("${values}" "if (pl->SkipsClientUpdates())\n        return;" "BuildValuesUpdateBlockForPlayer(&iter->second, iter->first);" "values block skipped")
between("${object_cpp}" "void Object::SendCreateUpdateToPlayer(Player* player)" "\n}\n" send_create)
require_order("${send_create}" "if (player->SkipsClientUpdates())" "BuildCreateUpdateBlockForPlayer(&upd, player);" "create send skipped")
between("${object_cpp}" "void Object::DestroyForPlayer(Player *target) const" "\n}\n" destroy)
require_order("${destroy}" "if (target->SkipsClientUpdates())" "target->GetSession()->SendPacket(&data);" "destroy packet skipped")
between("${update_cpp}" "void UpdateData::Send(WorldSession* session, bool hasTransport)" "\n}\n" send)
require_order("${send}" "if (player->SkipsClientUpdates())" "BuildPacket(&data, nullptr, hasTransport);" "batched packets skipped")

# The visible-set bookkeeping is unconditional in the batched visibility update.
between("${player_cpp}" "void Player::UpdateVisibilityOf(WorldObject const* viewPoint, T* target, UpdateData& data, std::set<WorldObject*>& visibleNow)" "template void Player::UpdateVisibilityOf(" vis)
require_order("${vis}" "if (!SkipsClientUpdates())   // twow-repo#541 (SkipBotUpdatePackets): the visible set below stays\n                target->BuildOutOfRangeUpdateBlock(&data);" "m_visibleGUIDs.erase(t_guid);" "out-of-range block skipped, erase kept")
require_order("${vis}" "if (!SkipsClientUpdates())   // twow-repo#541 (SkipBotUpdatePackets): the visible set below stays\n                target->BuildCreateUpdateBlockForPlayer(&data, this);" "UpdateVisibilityOf_helper(m_visibleGUIDs, target);" "create block skipped, insert kept")
string(FIND "${vis}" "if (!SkipsClientUpdates())\n            {" wrapped)
if(NOT wrapped EQUAL -1)
  message(FATAL_ERROR "#541 skip bot updates: the visible-set bookkeeping must not be inside the skip condition")
endif()

message(STATUS "skip_bot_updates source contract passed")
