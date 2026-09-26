if(NOT DEFINED PB_MODULE_DIR OR NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "PB_MODULE_DIR and TW_CORE_ROOT are required")
endif()

# #275: `talk N` runs exactly the displayed gossip option or says why not.
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/GossipHelloAction.cpp" action)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/GossipHelloAction.h" action_header)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp" chat_strategy)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/NPCHandler.cpp" npc_handler)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}")
  endif()
endfunction()

function(function_region text signature next_signature output)
  string(FIND "${text}" "${signature}" begin)
  if(begin EQUAL -1)
    message(FATAL_ERROR "Missing function ${signature}")
  endif()
  string(SUBSTRING "${text}" ${begin} -1 tail)
  string(FIND "${tail}" "${next_signature}" end)
  if(end EQUAL -1)
    set(${output} "${tail}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${tail}" 0 ${end} region)
    set(${output} "${region}" PARENT_SCOPE)
  endif()
endfunction()

# The chat path: "talk" reaches the gossip action with the number as parameter.
require_text("${chat_strategy}" "\"talk\"" "talk chat trigger")
require_text("${chat_strategy}" "new NextAction(\"gossip hello\"" "talk -> gossip hello mapping")

# Premise from Core: the innkeeper option only asks the client, the bind itself
# needs CMSG_BINDER_ACTIVATE. If Core ever binds directly, revisit this fix.
function_region("${player}" "void Player::SetBindPoint" "}" set_bind_point)
require_text("${set_bind_point}" "SMSG_BINDER_CONFIRM" "Core bind confirmation request")
require_text("${npc_handler}" "void WorldSession::HandleBinderActivateOpcode" "Core binder activation handler")

function_region("${action}" "bool GossipHelloAction::Execute" "void GossipHelloAction::TellGossipText" execute)
function_region("${action}" "bool GossipHelloAction::ProcessGossip" "GossipHelloAction::GossipMenuSnapshot GossipHelloAction::SnapshotGossipMenu" process)
function_region("${action}" "bool GossipHelloAction::ConfirmBindPoint" "ZZZ_END_OF_FILE" confirm)

# No silent fallback: non-numbers and 0 are rejected, a stale menu is rejected.
require_text("${execute}" "find_first_not_of(\"0123456789\")" "numeric-only talk parameter")
require_text("${execute}" "gossipNpc != guid" "menu bound to the NPC it was opened for")
forbid_text("${execute}" "if (menuToSelect > 0) menuToSelect--;" "old atoi fallback to option 1")
require_text("${action_header}" "ObjectGuid gossipNpc;" "remembered gossip NPC")

# Client confirmations only for an explicit player choice, never for the random
# rpg browse path (otherwise wandering bots rebind at every inn).
string(FIND "${process}" "if (noFeedback)\n        return true;" browse_return)
string(FIND "${process}" "case GOSSIP_OPTION_INNKEEPER:" innkeeper_case)
if(browse_return EQUAL -1 OR innkeeper_case EQUAL -1 OR NOT browse_return LESS innkeeper_case)
  message(FATAL_ERROR "Innkeeper confirmation must come after the rpg browse early return")
endif()
require_text("${process}" "GossipMenuItem const item = menu.GetItem(actualMenuToSelect);" "item copied before selection")
require_text("${process}" "!(SnapshotGossipMenu() == before)" "menu shown again only when it changed")

# The bind uses the client's Core handler with all its gates, not a direct write.
require_text("${confirm}" "HandleBinderActivateOpcode(activate)" "binder activation through Core handler")
forbid_text("${confirm}" "SetHomebind" "direct homebind write")
forbid_text("${confirm}" "CharacterDatabase" "direct database access")

message(STATUS "GOSSIP_SELECT_SOURCE_CONTRACT=PASS")
