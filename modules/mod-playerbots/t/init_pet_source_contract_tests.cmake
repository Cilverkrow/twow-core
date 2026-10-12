# twow-repo#541 (roster spikes: "initialize pet" ~70 ms, OB-00 go 10.10.2026): behind AiPlayerbot.InitPet.Cache
# (default 0 = as before) InitPet picks from a tameable list built once (same candidates as the scan), isUseful
# keeps the character_pet answer for 60 s per bot, and a try without a pet is followed by a 60 s cooldown.
# Pure logic: t/init_pet_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 init pet: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 init pet: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotFactory.cpp" factory)
read_source("strategy/actions/GenericActions.cpp" actions)
read_source("strategy/actions/GenericActions.h" actions_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

require_text("${config_h}" "bool initPetCache = false;" "member default off")
require_text("${config_cpp}" "initPetCache = config.GetBoolDefault(\"AiPlayerbot.InitPet.Cache\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.InitPet.Cache = 0" "documented key")

# 1. Static list, built once, the old scan stays as the off path.
string(FIND "${factory}" "void PlayerbotFactory::InitPet()" init_at)
string(SUBSTRING "${factory}" ${init_at} 3500 init_body)
require_order("${init_body}" "if (sPlayerbotAIConfig.initPetCache)" "static ai::init_pet::TameList const tameList = []()" "switch, then the static list")
require_text("${init_body}" "ai::init_pet::CandidatesFor(tameList, bot->GetLevel())" "prefix for the level")
require_order("${init_body}" "else\n#endif\n        for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)" "if (ids.empty())" "old scan as the off path")

# 2./3. isUseful: cooldown and cache only with the switch, before the query; Execute sets the cooldown.
string(FIND "${actions}" "bool InitializePetAction::isUseful()" useful_at)
string(SUBSTRING "${actions}" ${useful_at} 2500 useful)
require_order("${useful}" "if (!hasTamedPet && sPlayerbotAIConfig.initPetCache)" "CharacterDatabase.PQuery(\"SELECT id, entry, owner \"" "cache before the query")
require_order("${useful}" "ai::init_pet::InCooldown(noPetUntil, now)" "ai::init_pet::CacheValid(storedPetCheckedAt, now, ai::init_pet::StoredPetCacheSeconds)" "cooldown, then cache")
require_order("${useful}" "CharacterDatabase.PQuery(" "storedPetCheckedAt = now;" "answer stored after the query")
string(FIND "${actions}" "bool InitializePetAction::Execute(Event& event)" exec_at)
string(SUBSTRING "${actions}" ${exec_at} 900 exec)
require_order("${exec}" "factory.InitPet();" "noPetUntil = uint32(time(nullptr)) + ai::init_pet::NoPetCooldownSeconds;" "cooldown after a try")
require_text("${exec}" "storedPetCheckedAt = 0;" "cache cleared after a try")
require_text("${actions_h}" "uint32 noPetUntil = 0;" "per-bot state")

message(STATUS "init_pet source contract passed")
