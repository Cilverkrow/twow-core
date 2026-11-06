# twow-repo#333 Berufe M2-T (OB-00 06.11.2026: measurement base of M2 "use professions", log only, compatible with ADR-0041):
# with AiPlayerbot.ProfessionUse.Trace on, three unthrottled countable "[ProfCount]" lines - an item the bot created (the
# created flag of SMSG_ITEM_PUSH_RESULT), a sale with its copper, equipped gear whose creator is the bot. No new switch, no
# behaviour: every line sits behind the Trace read and the packet still goes to its handler.
# This contract pins the lines, their gate, the packet layout constants and the pure rules' tests
# (t/profession_use_policy_tests.cpp).

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
    message(FATAL_ERROR "#333 M2-T ProfCount: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2-T ProfCount: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#333 M2-T ProfCount: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/ProfessionUsePolicy.h" policy)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/SellAction.cpp" sell)
read_source("${PB_SOURCE_DIR}/strategy/actions/EquipAction.cpp" equip)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_MODULE_DIR}/t/profession_use_policy_tests.cpp" tests)

# 0. Documented under ProfessionUse.Trace (no new key).
require_text("${dist}" "#   [ProfCount] event=created bot= level= item= count= quality= class= subclass= conjured= signature=" "dist: created line")
require_text("${dist}" "#   [ProfCount] event=sold bot= level= item= count= copper=" "dist: sold line")
require_text("${dist}" "#   [ProfCount] event=equipped_selfmade bot= level= item= quality= class= subclass=" "dist: equipped_selfmade line")
require_text("${dist}" "\nAiPlayerbot.ProfessionUse.Trace = 0\n" "Trace stays default 0")

# 1. The pure rules and the packet layout of Player::SendNewItem, with their tests.
require_text("${policy}" "constexpr std::size_t kPushCreatedOffset = 12;" "(1) created flag offset")
require_text("${policy}" "constexpr std::size_t kPushItemOffset = 25;" "(1) item id offset")
require_text("${policy}" "constexpr std::size_t kPushCountOffset = 37;" "(1) count offset")
require_text("${policy}" "constexpr std::size_t kPushSize = 41;" "(1) packet size")
require_text("${policy}" "    return size >= kPushSize && forBot && created != 0;" "(1) created rule")
require_text("${policy}" "    return after > before ? after - before : 0;" "(1) copper rule")
require_text("${policy}" "    return equipped && creator != 0 && creator == botGuid;" "(1) self-made rule")
require_text("${tests}" "PushCreated(41, true, 1)" "(1) tests: created")
require_text("${tests}" "SoldCopper(100, 125) == 25" "(1) tests: copper")
require_text("${tests}" "SelfMadeEquipped(7, 7, true)" "(1) tests: self-made")

# 2. Created: in the bot's packet hook, behind the Trace read, the packet still queued for its handler afterwards.
set(created_gate "if (sPlayerbotAIConfig.professionUseTrace && packet.GetOpcode() == SMSG_ITEM_PUSH_RESULT && bot)")
require_text("${ai_cpp}" "${created_gate}" "(2) created line behind Trace")
require_text("${ai_cpp}" "packet.read<uint64>(0) == bot->GetObjectGuid().GetRawValue()" "(2) only the bot's own push")
require_text("${ai_cpp}" "\"[ProfCount] event=created bot=%u level=%u item=%u count=%u quality=%u class=%u subclass=%u conjured=%u signature=%u\"" "(2) created line format")
require_order("${ai_cpp}" "${created_gate}" "\"[ProfCount] event=created" "(2) gate first")
require_order("${ai_cpp}" "\"[ProfCount] event=created" "		botOutgoingPacketHandlers.AddPacket(packet);\n	}\n}" "(2) the packet still goes to its handler")
count_text("${ai_cpp}" "\"[ProfCount] event=" 1 "(2) one ProfCount line in PlayerbotAI.cpp")

# 3. Sold: the money delta after the sale and before the gold cheat resets it, behind Trace; only a real sale.
require_text("${sell}" "if (sPlayerbotAIConfig.professionUseTrace)\n        {\n            if (uint32 const copper = ai::profession_use::SoldCopper(botMoney, bot->GetMoney()))" "(3) sold line behind Trace, copper > 0")
require_text("${sell}" "\"[ProfCount] event=sold bot=%u level=%u item=%u count=%u copper=%u\"" "(3) sold line format")
require_order("${sell}" "bot->GetSession()->HandleSellItemOpcode(p);" "\"[ProfCount] event=sold" "(3) after the sale")
require_order("${sell}" "\"[ProfCount] event=sold" "bot->SetMoney(botMoney);" "(3) before the gold cheat reset")
require_order("${sell}" "uint32 const soldItemId = item->GetProto()->ItemId;" "bot->GetSession()->HandleSellItemOpcode(p);" "(3) item id read before the sale")
count_text("${sell}" "\"[ProfCount] event=" 1 "(3) one ProfCount line in SellAction.cpp")

# 4. Equipped self-made: only after a real equip (the EquipAction event), behind Trace, creator == bot and equipped.
require_text("${equip}" "if (!sPlayerbotAIConfig.professionUseTrace || !bot || !item)\n        return;" "(4) behind Trace")
require_text("${equip}" "ai::profession_use::SelfMadeEquipped(item->GetGuidValue(ITEM_FIELD_CREATOR).GetRawValue(), bot->GetObjectGuid().GetRawValue(),\n            item->IsEquipped())" "(4) creator is the bot, equipped")
require_text("${equip}" "\"[ProfCount] event=equipped_selfmade bot=%u level=%u item=%u quality=%u class=%u subclass=%u\"" "(4) line format")
require_text("${equip}" "    if (!sameItemBack)\n        sPlayerbotAIConfig.logEvent(ai, \"EquipAction\", item->GetProto()->Name1, std::to_string(item->GetProto()->ItemId));\n    if (!sameItemBack)   // twow-repo#333 M2-T: the same real change only\n        TraceSelfMadeEquip(bot, item);" "(4) only a real change (EQUIP-FLIP-01), the #308 lines unchanged")
count_text("${equip}" "TraceSelfMadeEquip(bot, item);" 2 "(4) both equip paths")
count_text("${equip}" "\"[ProfCount] event=" 1 "(4) one ProfCount line in EquipAction.cpp")
