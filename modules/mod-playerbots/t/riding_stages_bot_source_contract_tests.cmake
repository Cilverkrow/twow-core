# twow-repo#295: bots follow the riding stages of the players. The decisions are the
# pure policy in src/playerbot/RidingStagesBotPolicy.h (riding_stages_bot_policy); this
# pins the hooks that apply them and the core switch that keeps the old behaviour.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

# The text from start_marker up to end_marker, for checks inside one function.
function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# The switch must come before the first use of `grant` inside the region.
function(require_switch_first body grant description)
  string(FIND "${body}" "CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED" switch_offset)
  string(FIND "${body}" "${grant}" grant_offset)
  if(switch_offset EQUAL -1 OR grant_offset EQUAL -1 OR NOT switch_offset LESS grant_offset)
    message(FATAL_ERROR "${description}: the riding stages switch must come before ${grant}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/RidingStagesBotPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/values/MountValues.cpp" mount_values)
file(READ "${PB_SOURCE_DIR}/strategy/values/BudgetValues.cpp" budget)
file(READ "${PB_SOURCE_DIR}/strategy/values/BudgetValues.h" budget_header)
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
file(READ "${PB_SOURCE_DIR}/strategy/ItemVisitors.h" visitors)
file(READ "${PB_SOURCE_DIR}/strategy/actions/CheckMountStateAction.cpp" check_mount)
file(READ "${PB_SOURCE_DIR}/strategy/actions/BuyAction.cpp" buy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/TrainerAction.cpp" trainer)
file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)

# The rules are the core's (FunserverRidingStages.h), not a copy; the policy stays pure.
require_text("${policy}" "#include \"FunserverRidingStages.h\"" "core riding rules")
foreach(constant "MOUNT1_PRICE_COPPER" "MOUNT2_PRICE_COPPER" "FAMILY1_REQUIRED_SKILL" "FAMILY2_REQUIRED_SKILL" "RANK_LEVEL[0]" "RANK_LEVEL[2]")
  require_text("${policy}" "FunserverRiding::${constant}" "core constant")
endforeach()
foreach(forbidden "Player*" "sWorld" "SpellEntry" "World.h")
  forbid_text("${policy}" "${forbidden}" "server type in the pure policy")
endforeach()
require_text("${mount_values}" "FunserverRiding::MountedSpeedPct(true, skill, player->GetLevel(), effective, (spellInfo->Custom & SPELL_CUSTOM_MOUNT_SPEED_100) != 0)" "server speed rule for the bot")
require_text("${mount_values}" "!(spellInfo->Custom & SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED)" "racing cars keep their spell value")
require_text("${mount_values}" "FunserverRiding::FamilyOf(" "server family rule")
require_text("${mount_values}" "player->GetSkillValuePure(SKILL_RIDING)" "same riding value as the core")

# One switch, the core's Funserver.Riding.Stages.Enabled; off keeps the old behaviour.
foreach(source mount_values budget usage buy trainer travel_mgr factory)
  require_text("${${source}}" "sWorld.getConfig(CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED)" "riding stages switch in ${source}")
endforeach()

# A. No free riding and no free mounts from the factory.
region("${factory}" "void PlayerbotFactory::InitSkills()" "uint32 skillLevel = bot->GetLevel() < 40 ? 0 : 1;" init_skills)
require_switch_first("${init_skills}" "SetSkill(SKILL_RIDING" "InitSkills")
region("${factory}" "void PlayerbotFactory::InitMounts()" "void PlayerbotFactory::InitPotions()" init_mounts)
require_switch_first("${init_mounts}" "learnSpell(" "InitMounts")

# B. Riding skill 762, the stage gate, collection items, effective speeds.
region("${mount_values}" "uint32 MountSkillTypeValue::Calculate()" "switch (bot->getRace())" skill_type)
require_switch_first("${skill_type}" "return SKILL_RIDING;" "MountSkillTypeValue")
require_text("${mount_values}" "riding_stages::MayBuyMount(" "vendor trip gate")
require_text("${mount_values}" "sMountMgr.GetMountSpellId(proto->ItemId)" "collection items through collection_mount")
require_text("${mount_values}" "COLLECTION_LEARN_SPELL" "collection learn spell 46499")
require_text("${mount_values}" "FormRunSpeed(5419)" "travel form speed from its passive")
require_text("${mount_values}" "FormRunSpeed(2645)" "ghost wolf speed from its spell")
require_text("${mount_values}" "mount.GetEffectiveSpeed(bot, canFly)" "max mount speed by effective speed")
require_text("${mount_values}" "MountValue::GetEffectiveSpeed(player, auraSpell->Id)" "current mount speed by effective speed")
require_text("${mount_values}" "BuyPrice > mountMoney" "no vendor trip for a mount the budget cannot pay")
require_text("${visitors}" "MountValue::GetCollectionMountSpell(proto)" "collection items count as mounts")
require_text("${visitors}" "return !bot->HasSpell(collectionSpell);" "a learned collection item is no mount")

# C. The bot mounts by effective speed.
require_text("${check_mount}" "i.GetEffectiveSpeed(bot, canFly) > j.GetEffectiveSpeed(bot, canFly)" "mount ranking by effective speed")
require_text("${check_mount}" "currentSpeed >= mount.GetEffectiveSpeed(bot, canFly)" "remount only for a faster mount")
forbid_text("${check_mount}" "i.GetSpeed(canFly) > j.GetSpeed(canFly)" "mount ranking by the DBC value")

# D. Buying and learning collection mounts.
require_text("${usage}" "riding_stages::ClassifyMountOffer(" "mount offer by effective speed")
require_text("${usage}" "!bot->HasSpell(mountSpell)" "known mounts are not needed")
require_text("${usage}" "RequiredReputationFaction" "no upgrade the vendor refuses for reputation")

# E. Budget: next rank plus the missing mount, saved for; the flying lines fixed.
require_text("${budget}" "riding_stages::MountBudgetCopper(level, moneyWanted, hasMount, hasSwiftMount)" "mount budget")
require_text("${budget_header}" "NeedMoneyFor::travel, NeedMoneyFor::mount }" "bots save for riding")
require_text("${budget}" "saveMoneyForRidingStages : saveMoneyFor" "saving for riding only with the switch")
forbid_text("${budget}" "level >= flyingMountCost" "flying cost compared with the level")
forbid_text("${budget}" "moneyWanted += basicFlyingRidingLevel" "a level added as money")

# F. Mounts are no vendor gear and are paid from the mount budget.
require_text("${buy}" "moneyKey = (uint32)NeedMoneyFor::mount;" "mount purchases from the mount budget")
require_text("${buy}" "if (policyGear && !mountPurchase)" "vendor gear policy skips mounts")

# G. Riding training from the mount budget.
require_text("${trainer}" "TrainerType == TRAINER_TYPE_MOUNTS" "riding trainer")
require_text("${trainer}" "ridingTraining ? NeedMoneyFor::mount : NeedMoneyFor::spells" "riding training from the mount budget")

# H. Travel budget at the unmounted run speed.
require_text("${travel_mgr}" "riding_stages::TravelBudgetRunSpeed(" "travel budget speed")
require_text("${travel_mgr}" "baseMoveSpeed[MOVE_RUN]" "unmounted run speed")

# Diagnostics: one line per purchase, learn and training, nothing per tick.
require_text("${buy}" "[Riding] buy bot=%u" "purchase line")
require_text("${check_mount}" "[Riding] learn bot=%u" "learn line")
require_text("${trainer}" "[Riding] train bot=%u" "training line")

message(STATUS "RIDING_STAGES_BOT_SOURCE_CONTRACT=PASS")
