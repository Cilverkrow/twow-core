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

# `first` must come before `second` inside the text.
function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "${description}: ${first} must come before ${second}")
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
file(READ "${PB_SOURCE_DIR}/strategy/values/TrainerValues.cpp" trainer_values)
file(READ "${PB_SOURCE_DIR}/strategy/values/VendorValues.cpp" vendor_values)

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
foreach(source mount_values budget usage buy trainer travel_mgr factory trainer_values vendor_values)
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

# F2. The vendor trigger prices a mount like BuyAction and the mount vendor trip: from the
#     mount budget, not from the gear money or the vendor gear allowance (the mount budget
#     holds the price, so it counted twice and the bot travelled without buying).
require_text("${vendor_values}" "MountValue::GetMountSpell(vendorItem->item)" "mounts known at the vendor trigger")
require_text("${vendor_values}" "proto->BuyPrice > AI_VALUE2(uint32, \"free money for\", (uint32)NeedMoneyFor::mount)" "mount priced from the mount budget")
require_order("${vendor_values}" "(uint32)NeedMoneyFor::mount" "freeMoney.find(usage)" "mounts priced before the per-usage budgets")
require_text("${vendor_values}" "BuyAction::VendorGearAllowance(ai)" "gear keeps the vendor gear allowance")

# G. Riding training from the mount budget, each rank of a visit against fresh money.
require_text("${trainer}" "TrainerType == TRAINER_TYPE_MOUNTS" "riding trainer")
require_text("${trainer}" "ridingTraining ? NeedMoneyFor::mount : NeedMoneyFor::spells" "riding training from the mount budget")
require_text("${trainer}" "RESET_AI_VALUE2(uint32, \"free money for\", moneyKey);" "free money read fresh for every rank")
require_text("${trainer}" "ridingTraining && bot->GetMoney() < cost" "no rank paid with money the bot lacks")

# G2. Every racial riding trainer under its own race: all ten share trainer template 1,
#     which the spell map files under its first trainer's race (3690, Tauren) only.
require_text("${policy}" "SpellList MountTrainersByRace(" "pure filing of the mount trainers by race")
region("${trainer_values}" "trainableSpellMap* TrainableSpellMapValue::Calculate()" "std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()" spell_map)
require_switch_first("${spell_map}" "riding_stages::MountTrainersByRace(" "TrainableSpellMapValue")
require_text("${spell_map}" "spellMap->find(TRAINER_TYPE_MOUNTS)" "only the mount trainers are filed again")
require_text("${spell_map}" "uint32(trainer->TrainerRace)" "each trainer's own race")
require_text("${spell_map}" "IsSameTrainerSpell" "equal spells merged per race")
# Trainable spells, available trainers and the train cost all read that filing by the bot's race.
region("${trainer_values}" "std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()" "std::string TrainableSpellsValue::Format()" trainable_spells)
require_text("${trainable_spells}" "trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace()" "trainable riding ranks of the own race")
region("${trainer_values}" "std::vector<int32> AvailableTrainersValue::Calculate()" "uint32 TrainCostValue::Calculate()" available_trainers)
require_text("${available_trainers}" "trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->getRace()" "riding trainers of the own race")
require_text("${available_trainers}" "\"trainable spells\", getQualifier()" "available trainers from the trainable spells")

# H. Travel budget at the unmounted run speed.
require_text("${travel_mgr}" "riding_stages::TravelBudgetRunSpeed(" "travel budget speed")
require_text("${travel_mgr}" "baseMoveSpeed[MOVE_RUN]" "unmounted run speed")

# Diagnostics: one line per purchase, learn and training, nothing per tick.
require_text("${buy}" "[Riding] buy bot=%u" "purchase line")
require_text("${check_mount}" "[Riding] learn bot=%u" "learn line")
require_text("${trainer}" "[Riding] train bot=%u" "training line")

message(STATUS "RIDING_STAGES_BOT_SOURCE_CONTRACT=PASS")
