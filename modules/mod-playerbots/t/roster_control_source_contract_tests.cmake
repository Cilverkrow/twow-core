if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# #354: the call sites actually use the roster-control policy. The decision
# table itself is covered by roster_control_policy_tests.
file(READ "${PB_SOURCE_DIR}/PlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rnd)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai)
file(READ "${PB_SOURCE_DIR}/PlayerbotSecurity.cpp" security)
file(READ "${PB_SOURCE_DIR}/strategy/actions/UseMeetingStoneAction.cpp" meeting_stone)
file(READ "${PB_SOURCE_DIR}/strategy/actions/LeaveGroupAction.cpp" leave)
file(READ "${PB_SOURCE_DIR}/strategy/actions/TrainerAction.cpp" trainer)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChatActionContext.h" action_context)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/ChatTriggerContext.h" trigger_context)
file(READ "${PB_SOURCE_DIR}/strategy/generic/ChatCommandHandlerStrategy.cpp" chat_strategy)

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

# 1+2: every `.bot` subcommand passes the GM-only and offline gates before its handler.
function_region("${mgr}" "std::string PlayerbotHolder::ProcessBotCommand" "bool PlayerbotMgr::HandlePlayerbotMgrCommand" process)
string(FIND "${process}" "IsGmOnlyBotCommand(cmd)" gm_gate)
string(FIND "${process}" "BotCommandAcceptsOfflineBot(cmd)" offline_gate)
string(FIND "${process}" "(this->*it->second)(bot, master, realParam)" dispatch)
if(gm_gate EQUAL -1 OR offline_gate EQUAL -1 OR dispatch EQUAL -1 OR NOT gm_gate LESS dispatch OR NOT offline_gate LESS dispatch)
  message(FATAL_ERROR "GM-only and offline gates must run before the handler dispatch")
endif()

# Summon: a random/roster bot is no longer summonable by anyone.
function_region("${mgr}" "std::string PlayerbotHolder::HandleBotSummon" "std::string PlayerbotHolder::HandleBotRemoveLogout" summon)
require_text("${summon}" "DecideRosterControl(master, bot)" "summon through roster control")
forbid_text("${summon}" "bool allowed = isMasterAccount || isRandomAccount;" "old open random-bot summon")

# 3: `.rndbot` from a game session needs GM rank before any handler runs.
function_region("${rnd}" "bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand" "void RandomPlayerbotMgr::OnPlayerLogout" console)
string(FIND "${console}" "IsRndbotCommandAllowedForPlayer(cmd)" rnd_gate)
string(FIND "${console}" "handlers[\"reset\"]" rnd_handlers)
if(rnd_gate EQUAL -1 OR rnd_handlers EQUAL -1 OR NOT rnd_gate LESS rnd_handlers)
  message(FATAL_ERROR "rndbot GM gate must run before the handler table")
endif()

# 4: only plain names reach the logon database.
function_region("${mgr}" "uint32 PlayerbotHolder::GetAccountId" "std::string PlayerbotHolder::ListBots" account)
string(FIND "${account}" "IsPlainName(name)" name_gate)
string(FIND "${account}" "LoginDatabase.PQuery" name_query)
if(name_gate EQUAL -1 OR name_query EQUAL -1 OR NOT name_gate LESS name_query)
  message(FATAL_ERROR "name validation must run before the logon query")
endif()

# 5: chat `debug` runs only after the full playerbot permission check.
function_region("${ai}" "void PlayerbotAI::HandleCommand(uint32 type" "void PlayerbotAI::HandleBotOutgoingPacket" handle)
string(FIND "${handle}" "PLAYERBOT_SECURITY_ALLOW_ALL" allow_all)
string(FIND "${handle}" "HandleRemoteCommand(filtered.substr(6))" remote_debug)
if(allow_all EQUAL -1 OR remote_debug EQUAL -1 OR NOT allow_all LESS remote_debug)
  message(FATAL_ERROR "chat debug must run after the ALLOW_ALL check")
endif()

# GM bypass: one configurable threshold everywhere, and every use is logged.
foreach(region process summon console)
  require_text("${${region}}" "result=gm_bypass" "gm_bypass log in ${region}")
endforeach()
require_text("${summon}" "IsGmBypass(master->GetSession()->GetSecurity(), sPlayerbotAIConfig.rosterControlGmMinSecurity)" "summon GM threshold from config")
require_text("${console}" "IsGmBypass(handler->GetSession()->GetSecurity(), sPlayerbotAIConfig.rosterControlGmMinSecurity)" "rndbot GM threshold from config")
function_region("${mgr}" "std::list<std::string> PlayerbotHolder::HandlePlayerbotCommand" "uint32 PlayerbotHolder::GetAccountId" dispatcher)
forbid_text("${dispatcher}" "useSecurity >= SEC_GAMEMASTER, " "hard-coded SEC_GAMEMASTER (4) as admin flag; the owner is GM 3")

# The adapter only fills the request; the decision is the pure policy.
require_text("${security}" "return ai::roster_control::Decide(request);" "adapter delegates to the policy")


# ---- #292 player commands ----

# One GM threshold: chat permission levels use the same bypass as bot admin.
function_region("${security}" "PlayerbotSecurityLevel PlayerbotSecurity::LevelFor" "bool PlayerbotSecurity::CheckLevelFor" level_for)
require_text("${level_for}" "IsGmBypass(from->GetSession()->GetSecurity(), sPlayerbotAIConfig.rosterControlGmMinSecurity)" "LevelFor GM threshold from config")
forbid_text("${level_for}" ">= SEC_GAMEMASTER" "hard-coded SEC_GAMEMASTER (4) in LevelFor")

# Summon, both paths: decision, then safety and cooldown, then the cooldown mark.
require_text("${summon}" "CheckRosterSummon(master, bot)" ".bot summon safety check")
require_text("${summon}" "MarkRosterSummon(bot)" ".bot summon starts the cooldown")
function_region("${meeting_stone}" "bool SummonAction::Execute" "bool SummonAction::SummonUsingGos" chat_summon)
string(FIND "${chat_summon}" "DecideRosterControl(requester, bot)" chat_decide)
string(FIND "${chat_summon}" "CheckRosterSummon(requester, bot)" chat_check)
string(FIND "${chat_summon}" "MarkRosterSummon(bot)" chat_mark)
if(chat_decide EQUAL -1 OR chat_check EQUAL -1 OR chat_mark EQUAL -1 OR NOT chat_decide LESS chat_check OR NOT chat_check LESS chat_mark)
  message(FATAL_ERROR "chat summon must decide, then check safety, then mark the cooldown")
endif()

# Leave: roster bots go only for master, leader, GM; repeat is answered.
function_region("${leave}" "bool LeaveGroupAction::Execute" "bool LeaveGroupAction::Leave" leave_exec)
require_text("${leave_exec}" "roster_control::MayDismiss(" "leave through the dismiss rule")
require_text("${leave_exec}" "I am not in a group." "idempotent repeat")

# Train: registered in all three places, authorized, never a raw grant or travel.
require_text("${action_context}" "creators[\"train\"]" "train action")
require_text("${trigger_context}" "ChatCommandTrigger(ai, \"train\")" "train chat trigger")
require_text("${chat_strategy}" "supported.push_back(\"train\");" "train accepted as a command")
function_region("${trainer}" "bool TrainCommandAction::Execute" "ZZZ_END_OF_FILE" train_exec)
require_text("${train_exec}" "DecideRosterControl(requester, bot)" "train authorization")
require_text("${train_exec}" "Iterate(requester, trainer, &TrainCommandAction::Learn, spells)" "train through the normal trainer path")
foreach(forbidden learnClassLevelSpells learnSpell TeleportTo "request named travel target" DoSpecificAction)
  forbid_text("${train_exec}" "${forbidden}" "train side effect ${forbidden}")
endforeach()

message(STATUS "ROSTER_CONTROL_SOURCE_CONTRACT=PASS")
