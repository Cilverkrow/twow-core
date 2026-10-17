# twow-repo#308 EQUIP-FLIP-01 (OB-00 16.10.2026): AiPlayerbot.Equip.KeepWeaponsUnlessUpgrade (default 0) - the equip
# upgrade check takes the hands off only for a hand-item candidate in the bags; a re-equip of the old main / off hand is
# no EquipAction event (always on).

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
    message(FATAL_ERROR "#308 keep weapons: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#308 keep weapons: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#308 keep weapons: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#308 keep weapons: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/EquipAction.cpp" equip)
read_source("KeepWeaponsPolicy.h" policy)

require_text("${config_h}" "bool equipKeepWeaponsUnlessUpgrade = false;" "switch default off")
require_text("${config_cpp}" "equipKeepWeaponsUnlessUpgrade = config.GetBoolDefault(\"AiPlayerbot.Equip.KeepWeaponsUnlessUpgrade\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Equip.KeepWeaponsUnlessUpgrade = 0" "documented")

# The upgrade run: unequip only when the policy says so; the policy is "always without the switch".
between("${equip}" "bool EquipUpgradesAction::Execute(Event& event)" "\n}\n" run)
require_text("${run}" "bool unequipHands = true;" "unequip by default")
require_text("${run}" "if (sPlayerbotAIConfig.equipKeepWeaponsUnlessUpgrade)" "decision behind the switch")
require_text("${run}" "unequipHands = ai::keep_weapons::UnequipHands(true, HandCandidateInBags(ai, bot));" "only for a hand candidate")
require_text("${run}" "if (unequipHands && oldMainhand)\n        UnequipAction::UnequipItem(ai, bot, oldMainhand, true);" "main hand gated")
require_text("${run}" "if (unequipHands && oldOffhand)\n        UnequipAction::UnequipItem(ai, bot, oldOffhand, true);" "off hand gated")
require_order("${run}" "bool unequipHands = true;" "FindItemUsageVisitor visitor(bot, ItemUsage::ITEM_USAGE_EQUIP);" "decided before the candidate list")
require_text("${run}" "EquipItem(ai, GetMaster(), item, sameHandBack, sameHandBack);" "old hand back marked")
require_text("${policy}" "return !keepSwitch || handCandidateInBags;" "policy: always without the switch")

# The event: no EquipAction for the same hand item back on (always on).
between("${equip}" "void EquipAction::EquipItem(PlayerbotAI* ai, Player* requester, Item* item, bool silent, bool sameItemBack)" "\n}\n" equip_item)
require_text("${equip_item}" "if (!sameItemBack)\n        sPlayerbotAIConfig.logEvent(ai, \"EquipAction\"" "event only on a real change")

message(STATUS "keep_weapons source contract passed")
