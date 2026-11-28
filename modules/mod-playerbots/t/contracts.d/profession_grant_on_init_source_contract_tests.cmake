# twow-repo#333 Berufe GrantOnInit (owner 27.11.2026 via OB-00: roster bots get their professions at start; owner economy
# decision 28.11.2026 via OB-00: starter tools given). AiPlayerbot.ProfessionTraining.GrantOnInit (code default 0; 1 all,
# 2 cohort B, 3 cohort A): a persistent roster bot lacking a primary of its profession_pair learns the apprentice rank at
# login like TrainerAction::Learn (core GREEN state check, learnSpell of the taught spell, no cast, no money) and gets the
# missing starter tools. The rule behaviour is tested in t/profession_grant_policy_tests.cpp; this contract pins the switch
# (dist key, member, read with > 3 = off, sorted [ConfigEcho] entry) and the wiring (login after plan + bags, roster check,
# GREEN gate, learn path, no money, apprentice table from both trainer lists).

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
    message(FATAL_ERROR "#333 GrantOnInit: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 GrantOnInit: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 GrantOnInit: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 GrantOnInit: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 GrantOnInit: region end not found after '${first}': '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
read_source("${PB_SOURCE_DIR}/RandomPlayerbotMgr.h" mgr_h)
read_source("${PB_SOURCE_DIR}/ProfessionGrantPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry.
require_text("${dist}" "\nAiPlayerbot.ProfessionTraining.GrantOnInit = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 professionTrainingGrantOnInit = 0;" "config member, default 0")
require_text("${config_cpp}" "professionTrainingGrantOnInit = config.GetIntDefault(\"AiPlayerbot.ProfessionTraining.GrantOnInit\", 0);\n    if (professionTrainingGrantOnInit > 3)\n    {\n        sLog.outError(\"[ProfessionGrant] config key=GrantOnInit value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", professionTrainingGrantOnInit);\n        professionTrainingGrantOnInit = 0;\n    }" "config read, > 3 = off")
require_text("${config_cpp}" "            {\"PotionFind.BySpell\", Value(potionFindBySpell)},\n            {\"ProfessionTraining.GrantOnInit\", Value(professionTrainingGrantOnInit)},\n            {\"ProfessionTraining.NearRadius\"" "parsed mode in [ConfigEcho], sorted")

# 1. Login: after the plan backfill and the bags (room for the tools), inside the roster branch.
between("${mgr}" "void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)" "void RandomPlayerbotMgr::ProvisionPersistentRosterStarterOutfit(" login)
require_order("${login}" "PlayerbotFactory(bot, bot->GetLevel()).EnsureProfessionPairPlan();" "GrantPersistentRosterProfessions(bot, true);" "grant after the plan")
require_order("${login}" "ProvisionPersistentRosterBags(bot);" "GrantPersistentRosterProfessions(bot, true);" "grant after the bags")
require_order("${login}" "if (IsPersistentRosterMember(bot->GetGUIDLow()))" "GrantPersistentRosterProfessions(bot, true);" "grant in the roster branch")

# 2. Periodic roster check (level-gated ranks): in the online roster branch of ProcessBot(uint32), after RecordOnline.
between("${mgr}" "bool RandomPlayerbotMgr::ProcessBot(uint32 bot)" "bool RandomPlayerbotMgr::ProcessBot(Player* player)" process)
require_order("${process}" "persistentRoster->RecordOnline(bot);" "GrantPersistentRosterProfessions(player, false);" "tick check after RecordOnline")
require_text("${mgr_h}" "void GrantPersistentRosterProfessions(Player* bot, bool login);" "declaration")

# 3. The grant: mode first, persisted pair, both-skills early return, the core GREEN state check, the TrainerAction::Learn
#    path (learnSpell of each LEARN_SPELL trigger), HasSkill afterwards, tools only on ok, no money and no cast.
between("${mgr}" "void RandomPlayerbotMgr::GrantPersistentRosterProfessions(Player* bot, bool login)" "Player* RandomPlayerbotMgr::GetRandomPlayer()" grant)
require_order("${grant}" "ai::profession_grant::Applies(sPlayerbotAIConfig.professionTrainingGrantOnInit, bot->GetGUIDLow())" "GetProfessionPair(guid)" "mode before any work")
require_text("${grant}" "if (bot->HasSkill(plan->first) && bot->HasSkill(plan->second))\n        return;" "early return with both skills")
require_text("${grant}" "if (bot->HasSkill(skill) || professionGrantFinal.count({ guid, skill }))\n            continue;" "idempotent: a learned skill is never granted again (relog, tick); a final result is not retried before the next login")
require_order("${grant}" "ai::profession_grant::Applies(sPlayerbotAIConfig.professionTrainingGrantOnInit, bot->GetGUIDLow())" "auto const& table = ProfessionGrantTable();" "key 0: the trainer table is never built")
require_text("${grant}" "bot->StoreNewItem(dest, item, true, Item::GenerateItemRandomPropertyId(item)) != nullptr" "store only after CanStoreNewItem said OK (a full bag = no item and a store_failed line)")
require_text("${grant}" "bot->GetTrainerSpellState(&tSpell, reqLevel) != TRAINER_SPELL_GREEN" "core GREEN state check")
require_text("${grant}" "if (proto->Effect[j] == SPELL_EFFECT_LEARN_SPELL)\n                        bot->learnSpell(proto->EffectTriggerSpell[j], false);" "learn path of TrainerAction::Learn")
require_text("${grant}" "result = bot->HasSkill(skill) ? Result::Ok : Result::LearnFailed;" "skill check after learning")
require_order("${grant}" "if (result == Result::Ok)" "ai::profession_grant::StarterTools(skill)" "tools only after a grant")
require_text("${grant}" "bot->HasItemCount(item, 1)" "only missing tools")
require_text("${grant}" "bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, item, 1) == EQUIP_ERR_OK" "tools into the bags")
forbid_text("${grant}" "StoreNewItemInBestSlots" "equipping store (a tool must never land in a weapon slot)")
require_text("${grant}" "\"[ProfessionGrant] guid=%u level=%u pair=%u skill=%u spell=%u source=%s result=%s value=%u tools=%s\"" "the grant line")
forbid_text("${grant}" "ModifyMoney" "money change in the grant")
forbid_text("${grant}" "CastSpell" "cast in the grant (the trainer path learns, it does not cast)")
forbid_text("${grant}" "SetSkill" "direct skill write in the grant")

# 4. The apprentice table: every tradeskill trainer, both lists, candidate rule from the policy, ambiguity = no grant.
between("${mgr}" "std::map<uint32, ProfessionGrantEntry> const& ProfessionGrantTable()" "void RandomPlayerbotMgr::GrantPersistentRosterProfessions(" table)
require_text("${table}" "info->TrainerType != TRAINER_TYPE_TRADESKILLS" "only tradeskill trainers")
require_text("${table}" "collect(sObjectMgr.GetNpcTrainerSpells(info->Entry));" "npc_trainer list")
require_text("${table}" "collect(sObjectMgr.GetNpcTrainerTemplateSpells(info->TrainerTemplateId));" "npc_trainer_template list (Jewelcrafting)")
require_text("${table}" "ai::profession_grant::IsApprenticeCandidate(tSpell.reqSkill, teachesFirstRank, skill)" "candidate rule")
require_text("${table}" "RosterProfessionTrainerAction::GetTrainerSpellSkill(&tSpell)" "skill identity of the trainer path")
require_text("${table}" "ai::profession_grant::ResolveApprentice(skillSpells)" "resolver")
require_text("${table}" "for (uint32 const skill : ai::profession_grant::kPrimaries)" "table line names every primary")
require_text("${table}" "\"[ProfessionGrant] table skills=%u ambiguous=%u missing=%u%s\"" "table line with skipped counts")
require_text("${table}" "summary << ' ' << skill << '=' << found->second << \"@L\" << entry.spell.reqLevel;" "strictest row level in the table line")
require_text("${table}" "if (!inserted && tSpell.reqLevel > row->second.reqLevel)\n                    row->second = tSpell;" "strictest trainer row per teaching spell")

# 5. Policy: starter tools and modes as decided; the factory stays plan-only.
require_text("${policy}" "case profession::kEngineering:    return { kBlacksmithHammer, kArclightSpanner };" "engineering tools")
require_text("${policy}" "constexpr std::uint32_t kJewelersKit = 55155;" "Jewelers Kit")
require_text("${policy}" "return mode > cohort::ModeOnlyA ? cohort::ModeOff : mode;" "unknown mode off")
require_text("${factory}" "Factory owns plan selection only" "factory stays plan-only")
