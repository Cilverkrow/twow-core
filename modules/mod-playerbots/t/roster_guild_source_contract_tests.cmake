if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#485: guilds of persistent roster bots. Counts from the core's memory (copies taken under
# the GuildMgr locks, never a Petition* on a map thread), a guild target per faction from the roster
# size, no founding beyond it, approved names, no /say towards bots. BotsPerGuild = 0 keeps the stock
# path; only the canBuyPetition fix and the /say gates act without the switch.

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Text from start_marker up to end_marker (both must exist, in that order).
function(text_between text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "Missing section start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(FIND "${rest}" "${end_marker}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "Missing section end after ${start_marker}: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end_at} section)
  set(${out} "${section}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/RosterGuildPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.cpp" create)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildCreateActions.h" create_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/PetitionSignAction.cpp" sign)
file(READ "${PB_SOURCE_DIR}/strategy/values/GuildValues.cpp" values)
file(READ "${PB_SOURCE_DIR}/strategy/values/GuildValues.h" values_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/GuildManagementActions.cpp" manage)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/GuildTriggers.cpp" triggers)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# 1. Pure policy, included only by the .cpp files that use it (PlayerbotAI.h and
#    PlayerbotAIConfig.h are in the botpch.h chain).
require_text("${policy}" "namespace ai::roster_guild" "policy namespace")
reject_text("${policy}" "#include \"" "game or playerbot include in the pure policy")
require_text("${policy}" "return botsPerGuild && rosterMember && !realPlayerMaster;" "new path only with BotsPerGuild > 0 for roster bots on their own")
foreach(header_name ai_header config_header)
  reject_text("${${header_name}}" "RosterGuildPolicy.h\"" "policy include in a PCH-chain header (${header_name})")
endforeach()

# 2. Bug fix without switch: "Hitem:5863:" was never parsed; the core refuses a second petition of
#    the same owner (PetitionsHandler.cpp:91-93). Presence check only, no Petition* dereferenced.
reject_text("${create}" "Hitem:5863:" "unparsed item count qualifier")
require_text("${create}" "if (bot->HasItemCount(5863, 1))" "charter in the bags blocks a purchase")
require_text("${create}" "if (sGuildMgr.GetPetitionByOwnerGuid(bot->GetObjectGuid()))" "existing petition blocks a purchase")
reject_text("${create}" "GetPetitionByOwnerGuid(bot->GetObjectGuid())->" "dereferenced petition of the owner")

# 3. Critic B5.1/B5.2: bot code keeps no Petition* - counts, names and signatures are copies taken
#    under the petition lock (GuildMgr::GetPetitionSummary* / Collect*Summaries).
foreach(source_name create sign values manage triggers)
  foreach(pointer_getter "GetPetitionByCharterGuid(" "GetPetitionById(" "->GetSignatureCount()" "GetSignatureForPlayerGuid(" "->Rename(")
    reject_text("${${source_name}}" "${pointer_getter}" "Petition* access from bot code in ${source_name}")
  endforeach()
endforeach()

# 4. The plan: one std::mutex, inputs from copies and the player cache, no SQL.
text_between("${create}" "// --- twow-repo#485: roster guild plan" "// --- end of the roster guild plan" plan)
foreach(needle
    "std::mutex lock;"
    "std::lock_guard<std::mutex> guard(state.lock);"
    "sRandomPlayerbotMgr.PersistentRosterGuids()"
    "sObjectMgr.GetPlayerDataByGUID(guid)"
    "Player::TeamForRace(uint8(data->uiRace))"
    "roster_guild::TargetGuilds(roster[f], sPlayerbotAIConfig.rosterGuildBotsPerGuild)"
    "sGuildMgr.CollectGuildSummaries(guilds);"
    "sGuildMgr.CollectPetitionSummaries(petitions);"
    "roster_guild::MayBuyCharter("
    "roster_guild::TryReserveFounding(fs.target, fs.ledger)"
    "roster_guild::FinishFounding(fs.ledger, founded);"
    "roster_guild::SnapshotInterval(sPlayerbotAIConfig.rosterGuildSnapshotSeconds)"
    "ObjectMgr::IsValidCharterName(name) && !sObjectMgr.IsReservedName(name)"
    "[RosterGuild] event=plan faction=%s roster=%u target=%u guilds=%u open_charters=%u free_names=%u")
  require_text("${plan}" "${needle}" "roster guild plan")
endforeach()
foreach(forbidden "CharacterDatabase" "PQuery" "shared_mutex" "sObjectMgr.GetPlayer(" "GetGuildByLeader(" "GetGuildById(")
  reject_text("${plan}" "${forbidden}" "per-call SQL, shared_mutex or a live Player*/Guild* in the plan")
endforeach()
# One charter for count, offer and turn-in: every charter item, the one whose petition the bot owns.
text_between("${plan}" "Item* RosterGuildPlan::OwnCharter(Player* bot, PetitionSummary& out, uint32 accountId)" "return found;" own_charter)
foreach(needle
    "bot->ApplyForAllItems("
    "item->GetEntry() == 5863"
    "sGuildMgr.GetPetitionSummaryByCharterGuid(item->GetObjectGuid(), out, accountId)"
    "out.ownerGuid == bot->GetObjectGuid()")
  require_text("${own_charter}" "${needle}" "own charter lookup")
endforeach()
foreach(source_name create values)
  reject_text("${${source_name}}" "GetItemByEntry(5863)" "first charter item instead of the own charter in ${source_name}")
endforeach()

# 5. Purchase (new path): faction target and a free approved name, throttled diagnostic; the name
#    comes from the plan instead of the stock generator (two synchronous queries per call).
text_between("${create}" "bool BuyPetitionAction::canBuyPetition(Player* bot)" "bool PetitionOfferAction::Execute(" can_buy)
require_order("${can_buy}" "if (RosterGuildPlan::UsesRosterPath(ai))" "RosterGuildPlan::MayBuyCharter(bot, reason)" "new-path purchase gate")
require_text("${can_buy}" "RosterGuildPlan::IsDue(ai, \"roster guild buy trace\", HOUR)" "buy_blocked at most once per hour and bot")
require_text("${can_buy}" "[RosterGuild] event=buy_blocked bot=%u reason=%s" "buy_blocked diagnostic")
require_text("${create}" "rosterPath ? RosterGuildPlan::ReserveCharter(bot) : RandomPlayerbotFactory::CreateRandomGuildName()" "approved name for roster purchases")

# 6. Offer (new path): memory instead of the two petition_sign queries; same faction, not full,
#    account not signed. /say only towards real players (owner 02.10., point 7), without switch.
text_between("${create}" "bool PetitionOfferAction::Execute(" "bool PetitionOfferNearbyAction::Execute(" offer)
require_order("${offer}" "if (rosterPath)" "CharacterDatabase.PQuery(" "new-path offer before the stock queries")
require_order("${offer}" "rosterPath ? RosterGuildPlan::OwnCharter(bot, petition, player->GetSession()->GetAccountId()) : petitions.front();" "data << charter->GetObjectGuid();" "offer of the own charter with the account check")
foreach(needle "player->GetTeam() != bot->GetTeam()" "sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS)" "petition.signedByAccount")
  require_text("${offer}" "${needle}" "offer gate")
endforeach()
text_between("${create}" "bool PetitionOfferNearbyAction::Execute(" "bool PetitionTurnInAction::Execute(" offer_nearby)
require_order("${offer_nearby}" "if (sPlayerbotAIConfig.inviteChat && IsRealPlayer(player) &&" "do you want create a guild together?" "/say only towards real players")
# Critic B5.9: the faction gate of "offer petition nearby" sits in isUseful (GuildCreateActions.h).
require_text("${create_h}" "AI_VALUE(uint8, \"petition signs\") < sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS) && RosterGuildPlan::MayOfferNearby(ai);" "offer nearby gated below the target")

# 7. Turn-in (new path): slot and name reserved under the plan's lock before the turn-in (critic
#    B5.3), rename only through the core (B5.4), the slot released afterwards, founded trace.
text_between("${create}" "bool PetitionTurnInAction::Execute(" "bool PetitionTurnInAction::isUseful(" turn_in)
require_order("${turn_in}" "RosterGuildPlan::OwnCharter(bot, summary)" "RosterGuildPlan::ReserveFounding(bot, summary.name, guildName, target, reason)" "own charter before the reservation")
require_order("${turn_in}" "RosterGuildPlan::ReserveFounding(bot, summary.name, guildName, target, reason)" "sGuildMgr.RenamePetition(petition->GetObjectGuid(), bot->GetObjectGuid(), guildName)" "reservation before the rename")
require_order("${turn_in}" "sGuildMgr.RenamePetition(" "bot->GetSession()->HandleTurnInPetitionOpcode(data);" "rename before the turn-in")
require_order("${turn_in}" "bot->GetSession()->HandleTurnInPetitionOpcode(data);" "RosterGuildPlan::FinishFounding(bot, guildName, bot->GetGuildId() != 0);" "slot released after the turn-in")
# No turn-in without a founding slot and an approved name (B5.3): the blocked branch leaves Execute.
text_between("${turn_in}" "if (guildName.empty())" "data << petition->GetObjectGuid();" blocked_turn_in)
require_text("${blocked_turn_in}" "return false;" "no turn-in without a founding slot and an approved name (B5.3)")
require_text("${turn_in}" "[RosterGuild] event=founded bot=%u guild=%u name=%s faction=%s members=%u target=%u" "founded diagnostic")
require_text("${turn_in}" "[RosterGuild] event=found_blocked bot=%u charter=%u reason=%s" "blocked founding diagnostic")
# The trip to a guild master expires the current travel target: at most once per RosterGuildRetrySeconds.
require_order("${turn_in}" "SET_AI_VALUE2(time_t, \"manual time\", \"roster guild travel\", time(nullptr));" "oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "trip throttled before the target is expired")
require_text("${turn_in}" "[RosterGuild] event=travel bot=%u" "travel diagnostic")
text_between("${create}" "bool PetitionTurnInAction::isUseful(" "bool BuyTabardAction::Execute(" turn_in_useful)
require_order("${turn_in_useful}" "return RosterTurnInUseful(ai, bot);" "if (!ChooseTravelTargetAction::isUseful())" "new path without the capital and free-target conditions")
text_between("${create}" "bool RosterTurnInUseful(PlayerbotAI* ai, Player* bot)" "bool RosterGuildPlan::UsesRosterPath(" roster_useful)
foreach(needle
    "RosterGuildPlan::OwnCharter(bot, petition)"
    "uint32(petition.signatureCount) != sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS)"
    "RosterGuildPlan::MayTurnIn(bot, petition.name)"
    "UNIT_NPC_FLAG_PETITIONER"
    "RosterGuildRetrySeconds")
  require_text("${roster_useful}" "${needle}" "new-path turn-in usefulness")
endforeach()
require_order("${roster_useful}" "destination->HasNpcFlag(UNIT_NPC_FLAG_PETITIONER)" "AI_VALUE2(time_t, \"manual time\", \"roster guild travel\")" "trip throttle after the guild-master target check")
reject_text("${roster_useful}" "AREA_FLAG_CAPITAL" "capital condition in the new path")

# 8. Signing (new path): DecideSign with copies; a vanished charter is declined with a trace (B5.2).
#    "Thanks for the invite!" only with InviteChat and towards a real player, without switch.
require_text("${sign}" "sGuildMgr.GetPetitionSummaryByCharterGuid(petitionGuid, offered)" "offered charter as a copy")
require_text("${sign}" "sGuildMgr.GetPetitionSummaryBySigner(bot->GetObjectGuid(), current)" "own signature as a copy")
require_text("${sign}" "[RosterGuild] event=sign_declined bot=%u charter=%u reason=no_petition" "declined vanished charter")
require_order("${sign}" "roster_guild::DecideSign(" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD" "sign decision before the decline path")
require_order("${sign}" "if (sPlayerbotAIConfig.inviteChat && IsRealPlayer(_inviter))" "bot->Say(\"Thanks for the invite!\"" "thanks only towards real players")
string(FIND "${sign}" "if (sPlayerbotAIConfig.inviteChat && IsRealPlayer(_inviter))" gate_at)
string(FIND "${sign}" "bot->Say(\"Thanks for the invite!\"" say_at)
math(EXPR gap "${say_at} - ${gate_at}")
if(gap GREATER 100)
  message(FATAL_ERROR "the thanks /say must directly follow its gate (gap ${gap})")
endif()

# 9. Signature count (critic B5.5): recalculated, from memory in the new path, stock query once.
require_text("${values_h}" "CalculatedValue<uint8>(ai, \"petition signs\", 10)" "petition signs re-read every few seconds")
reject_text("${values_h}" "SingleCalculatedValue<uint8>(ai, \"petition signs\")" "once-only signature count")
text_between("${values}" "uint8 PetitionSignsValue::Calculate()" "bool CanBuyTabard::Calculate()" signs)
require_text("${signs}" "RosterGuildPlan::OwnCharter(bot, petition)" "signature count from the copy of the own petition")
require_text("${signs}" "return petition.signatureCount;" "count of the own petition")
require_order("${signs}" "roster_guild::UsesRosterPath(" "if (stockQueried)" "new path before the stock path")
require_order("${signs}" "if (stockQueried)" "CharacterDatabase.PQuery(" "stock query once per value lifetime")

# 10. Critic B5.6: with BotsPerGuild > 0 the stock nearby invites and the leave of large guilds do
#     not run for roster bots on their own.
require_order("${manage}" "sRandomPlayerbotMgr.IsPersistentRosterMember(player->GetGUIDLow())" "DoSpecificAction(\"guild invite\"" "no nearby invite of roster bots")
require_order("${manage}" "roster_guild::UsesRosterPath(" "DoSpecificAction(\"guild invite\"" "no nearby invite by roster bots")
require_order("${triggers}" "roster_guild::UsesRosterPath(" "GetMaxPreferedGuildSize()" "roster bots never leave by themselves")

# 11. Config: neutral defaults next to RandomBotFormGuild; example names only in comments.
foreach(pair
    "\"AiPlayerbot.RosterGuild.BotsPerGuild\", 0)"
    "\"AiPlayerbot.RosterGuild.NamesAlliance\", \"\")"
    "\"AiPlayerbot.RosterGuild.NamesHorde\", \"\")"
    "\"AiPlayerbot.RosterGuild.SnapshotSeconds\", 60)")
  require_text("${config_source}" "${pair}" "config default")
endforeach()
require_order("${config_source}" "\"AiPlayerbot.RandomBotFormGuild\"" "\"AiPlayerbot.RosterGuild.BotsPerGuild\"" "keys next to RandomBotFormGuild")
# Members without in-class defaults (Initialize() sets them), so playerbot_config_key_usage can still
# tell whether they are read.
foreach(member "uint32 rosterGuildBotsPerGuild;" "uint32 rosterGuildSnapshotSeconds;")
  require_text("${config_header}" "${member}" "member declared like its neighbours")
endforeach()
foreach(member "rosterGuildBotsPerGuild =" "rosterGuildSnapshotSeconds =")
  reject_text("${config_header}" "${member}" "in-class default that counts as a read for playerbot_config_key_usage")
endforeach()
foreach(line
    "AiPlayerbot.RosterGuild.BotsPerGuild = 0"
    "AiPlayerbot.RosterGuild.NamesAlliance = \"\""
    "AiPlayerbot.RosterGuild.NamesHorde = \"\""
    "AiPlayerbot.RosterGuild.SnapshotSeconds = 60"
    "(10-3600, other values are clamped")
  require_text("${config_template}" "${line}" "documented default")
endforeach()
require_order("${config_template}" "# AiPlayerbot.RandomBotFormGuild = 1" "AiPlayerbot.RosterGuild.BotsPerGuild = 0" "documented next to RandomBotFormGuild")
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.NamesAlliance = \"W" "proposed names as a default")
reject_text("${config_template}" "\nAiPlayerbot.RosterGuild.NamesHorde = \"R" "proposed names as a default")

message(STATUS "ROSTER_GUILD_SOURCE_CONTRACT=PASS")
