file(READ "${PLAYERBOT_AI_SOURCE}" source)

if(NOT source MATCHES "packet\\.GetOpcode\\(\\) == CMSG_MESSAGECHAT")
  message(FATAL_ERROR
    "Bot dialogue delivery must identify internally generated chat packets")
endif()

if(NOT source MATCHES "session->HandleMessagechatOpcode\\(chatPacket\\)")
  message(FATAL_ERROR
    "Bot dialogue delivery must dispatch chat on the bot world-thread tick")
endif()

string(FIND "${source}"
  "if (packet.GetOpcode() == CMSG_MESSAGECHAT)" chat_gate)
string(FIND "${source}"
  "session->HandleMessagechatOpcode(chatPacket)" direct_dispatch)
string(FIND "${source}"
  "else\n                    session->QueuePacket(packet)" queue_fallback)

if(chat_gate EQUAL -1 OR direct_dispatch EQUAL -1 OR queue_fallback EQUAL -1)
  message(FATAL_ERROR "Expected focused chat dispatch block was not found")
endif()

if(NOT chat_gate LESS direct_dispatch OR NOT direct_dispatch LESS queue_fallback)
  message(FATAL_ERROR
    "Chat packets must be dispatched directly before the non-chat queue fallback")
endif()

message(STATUS "BOT_DIALOGUE_DELIVERY_SOURCE_CONTRACT=PASS")
