# twow-repo#341 (AiPlayerbot.Equip.AfterRollWon, default 0; owner repro 25.10.2026 on C): the core stores a won Need/Greed item
# (Group::CountTheRoll) without SMSG_ITEM_PUSH_RESULT, so "equip upgrades" never ran for it. SMSG_LOOT_ROLL_WON (sent to every
# voter) now runs it, for this bot's own win of a gear item only.

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
    message(FATAL_ERROR "#341 equip after roll: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#341 equip after roll: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/triggers/WorldPacketTriggerContext.h" triggers)
read_source("strategy/generic/WorldPacketHandlerStrategy.cpp" strategy)
read_source("strategy/actions/EquipAction.cpp" equip)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" config_template)

# Key, default 0, documented.
require_text("${config_cpp}" "equipAfterRollWon = config.GetBoolDefault(\"AiPlayerbot.Equip.AfterRollWon\", false);" "key, default 0")
require_text("${config_h}" "bool equipAfterRollWon = false;" "member default 0")
require_text("${config_template}" "AiPlayerbot.Equip.AfterRollWon = 0" "key documented")

# Packet -> trigger -> "equip upgrades".
require_text("${ai_cpp}" "botOutgoingPacketHandlers.AddHandler(SMSG_LOOT_ROLL_WON, \"loot roll won\", true);" "packet handler")
require_text("${triggers}" "creators[\"loot roll won\"] = [](PlayerbotAI* ai) { return new WorldPacketTrigger(ai, \"loot roll won\"); };" "packet trigger")
require_text("${strategy}" "        \"loot roll won\",
        NextAction::array(0, new NextAction(\"equip upgrades\", relevance), NULL)));" "trigger node runs equip upgrades")

# The action: the switch first (0 = nothing, no bag scan), then the packet in Group::SendLootRollWon's order, own win of gear only.
require_order("${equip}" "    else if (event.getSource() == \"loot roll won\")
    {
        if (!sPlayerbotAIConfig.equipAfterRollWon)
            return false;" "        data >> lootGuid >> lootSlot >> itemId >> randomSuffix >> randomPropertyId >> winner >> rollNumber >> rollType;" "switch before the packet")
require_text("${equip}" "if (!ai::equip_after_roll::Equips(true, winner == bot->GetObjectGuid(), gear))
            return false;" "only this bot's own win of a gear item")
require_text("${equip}" "itemProto->Class == ItemClass::ITEM_CLASS_ARMOR || itemProto->Class == ItemClass::ITEM_CLASS_CONTAINER);" "gear = weapon, armour, bag (the push-result rule)")
# The AutoEquipUpgradeLoot gate stays in front of every source.
require_order("${equip}" "    if (!sPlayerbotAIConfig.autoEquipUpgradeLoot && !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;" "    else if (event.getSource() == \"loot roll won\")" "AutoEquipUpgradeLoot gate first")
