if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#485 (critic B5.1/B5.4): bots run on map threads. A Petition* from GetPetitionBy*()
# can be deleted by a turn-in on another map thread (HandleTurnInPetitionOpcode -> DeletePetition)
# as soon as the shared lock is released, so bot code reads petitions and guilds only as copies
# taken under the manager's lock, and renames a charter (UPDATE petition SET name) only under the
# exclusive petition lock, after the same name checks as MSG_PETITION_RENAME.
file(READ "${TW_CORE_ROOT}/src/game/Guild/GuildMgr.h" header)
file(READ "${TW_CORE_ROOT}/src/game/Guild/GuildMgr.cpp" source)

# Text from the definition to its closing brace at column 0.
function(definition_body text signature out)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: missing ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}" stop)
  if (stop EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: no end of ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${stop} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# needle_a must appear in body, and before needle_b.
function(require_before body needle_a needle_b description)
  string(FIND "${body}" "${needle_a}" a)
  string(FIND "${body}" "${needle_b}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "Guild petition summary: ${description} (${needle_a} before ${needle_b})")
  endif()
endfunction()

foreach (required
    "struct PetitionSummary"
    "struct GuildSummary"
    "bool GetPetitionSummaryByCharterGuid(ObjectGuid const& charterGuid, PetitionSummary& out, uint32 accountId = 0, ObjectGuid const& player = ObjectGuid());"
    "bool GetPetitionSummaryBySigner(ObjectGuid const& signerGuid, PetitionSummary& out);"
    "void CollectPetitionSummaries(std::vector<PetitionSummary>& out);"
    "void CollectGuildSummaries(std::vector<GuildSummary>& out) const;"
    "bool RenamePetition(ObjectGuid const& charterGuid, ObjectGuid const& ownerGuid, std::string const& newName);")
  string(FIND "${header}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: missing in GuildMgr.h: ${required}")
  endif()
endforeach()

# The copies hold values only - no pointer into GuildMgr's maps leaves the lock.
definition_body("${header}" "struct PetitionSummary" petition_summary)
definition_body("${header}" "struct GuildSummary" guild_summary)
foreach (body_name petition_summary guild_summary)
  string(FIND "${${body_name}}" "*" pointer_at)
  if (NOT pointer_at EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: ${body_name} must not hold a pointer")
  endif()
endforeach()

set(shared_petitions "std::shared_lock<std::shared_mutex> guard(m_petitionsMutex);")
definition_body("${source}" "bool GuildMgr::GetPetitionSummaryByCharterGuid(" by_charter)
require_before("${by_charter}" "${shared_petitions}" "CopyPetitionSummary(petition, out, accountId, player);" "charter lookup copies under the petition lock")
definition_body("${source}" "bool GuildMgr::GetPetitionSummaryBySigner(" by_signer)
require_before("${by_signer}" "${shared_petitions}" "CopyPetitionSummary(petition, out, 0, signerGuid);" "signer lookup copies under the petition lock")
definition_body("${source}" "void GuildMgr::CollectPetitionSummaries(" all_petitions)
require_before("${all_petitions}" "${shared_petitions}" "CopyPetitionSummary(iter.second, summary, 0, ObjectGuid());" "petition list copied under the petition lock")
definition_body("${source}" "void GuildMgr::CollectGuildSummaries(" all_guilds)
require_before("${all_guilds}" "std::shared_lock<std::shared_mutex> guard(m_guildMutex);" "summary.name = itr.second->GetName();" "guild list copied under the guild lock")

# The copy itself: count from the core's memory, both signature checks inside the lock.
definition_body("${source}" "static void CopyPetitionSummary(" copy)
foreach (required
    "out.signatureCount = petition->GetSignatureCount();"
    "out.signedByAccount = accountId && petition->GetSignatureForAccount(accountId);"
    "out.signedByPlayer = !player.IsEmpty() && petition->GetSignatureForPlayerGuid(player);"
    "out.name = petition->GetName();")
  string(FIND "${copy}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: CopyPetitionSummary misses ${required}")
  endif()
endforeach()

# Rename: name checks first (they take the guild lock), then the exclusive petition lock, owner
# checked, and only then the DB write in Petition::Rename.
definition_body("${source}" "bool GuildMgr::RenamePetition(" rename)
require_before("${rename}" "ObjectMgr::IsValidCharterName(newName)" "std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);" "rename validates the name before the lock")
require_before("${rename}" "sObjectMgr.IsReservedName(newName)" "std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);" "rename refuses reserved names")
require_before("${rename}" "GetGuildByName(newName)" "std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);" "rename refuses a guild's name")
require_before("${rename}" "std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);" "petition->Rename(name)" "rename under the exclusive petition lock")
require_before("${rename}" "if (petition->GetOwnerGuid() != ownerGuid)" "petition->Rename(name)" "rename only for the charter's owner")

# The DB write the rename stands for (named in the PR and the owner approval).
string(FIND "${source}" "CharacterDatabase.PExecute(\"UPDATE petition SET name = '%s' WHERE petitionguid = '%u'\"," update_at)
if (update_at EQUAL -1)
  message(FATAL_ERROR "Guild petition summary: Petition::Rename no longer writes UPDATE petition SET name - update the PR text and this contract")
endif()


# OB-30 review core#281 (finding 1): every change of a Petition in m_petitionMap runs under the
# exclusive m_petitionsMutex. A signature move (old one removed and deleted, new one added) is one
# GuildMgr method under ONE lock, so no shared reader iterates a list while a signature is deleted
# and no reader sees the signer signed nowhere.
set(exclusive_petitions "std::lock_guard<std::shared_mutex> guard(m_petitionsMutex);")
foreach (required
    "bool AddPetitionSignature(uint32 petitionId, Player* signer);"
    "void RemovePetitionSignature(ObjectGuid const& signerGuid);"
    "bool GetPetitionSummaryById(uint32 petitionId, PetitionSummary& out, uint32 accountId = 0, ObjectGuid const& player = ObjectGuid());"
    "bool GetPetitionSignerGuids(uint32 petitionId, std::vector<ObjectGuid>& out);")
  string(FIND "${header}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: missing in GuildMgr.h: ${required}")
  endif()
endforeach()

definition_body("${source}" "bool GuildMgr::AddPetitionSignature(" add_signature)
require_before("${add_signature}" "${exclusive_petitions}" "petition->GetSignatureForPlayer(signer)" "sign checks repeated under the exclusive lock")
require_before("${add_signature}" "${exclusive_petitions}" "signature->DeleteFromDB();" "old signature removed under the exclusive lock")
require_before("${add_signature}" "signature->DeleteFromDB();" "previous->DeleteSignature(signature);" "old signature row deleted before the object")
require_before("${add_signature}" "previous->DeleteSignature(signature);" "return petition->AddNewSignature(signer);" "move = remove then add under the same lock")
string(FIND "${add_signature}" "shared_lock" add_shared_at)
if (NOT add_shared_at EQUAL -1)
  message(FATAL_ERROR "Guild petition summary: AddPetitionSignature must hold only the exclusive lock")
endif()

definition_body("${source}" "void GuildMgr::RemovePetitionSignature(" remove_signature)
require_before("${remove_signature}" "${exclusive_petitions}" "signature->DeleteFromDB();" "signature removal under the exclusive lock")
require_before("${remove_signature}" "signature->DeleteFromDB();" "petition->DeleteSignature(signature);" "signature row deleted before the object")

definition_body("${source}" "bool GuildMgr::GetPetitionSummaryById(" by_id)
require_before("${by_id}" "${shared_petitions}" "CopyPetitionSummary(iter->second, out, accountId, player);" "id lookup copies under the petition lock")
definition_body("${source}" "bool GuildMgr::GetPetitionSignerGuids(" signer_guids)
require_before("${signer_guids}" "${shared_petitions}" "out.push_back(signature->GetSignatureGuid());" "signer list copied under the petition lock")

# No direct Petition mutation outside GuildMgr: the handlers (map threads), guild founding and
# character deletion call the GuildMgr methods above.
file(READ "${TW_CORE_ROOT}/src/game/Handlers/PetitionsHandler.cpp" handler)
file(READ "${TW_CORE_ROOT}/src/game/Guild/Guild.cpp" guild_source)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player_source)
foreach (file_name handler guild_source player_source)
  foreach (forbidden "AddNewSignature(" "DeleteSignature(" "AddSignature(" "->Rename(")
    string(FIND "${${file_name}}" "${forbidden}" forbidden_at)
    if (NOT forbidden_at EQUAL -1)
      message(FATAL_ERROR "Guild petition summary: ${file_name} mutates a petition directly (${forbidden}) - use the GuildMgr methods")
    endif()
  endforeach()
endforeach()
foreach (required
    "sGuildMgr.AddPetitionSignature(petition.id, _player)"
    "sGuildMgr.RenamePetition(charter->GetObjectGuid(), _player->GetObjectGuid(), newname)")
  string(FIND "${handler}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Guild petition summary: PetitionsHandler.cpp misses ${required}")
  endif()
endforeach()
string(FIND "${guild_source}" "sGuildMgr.RemovePetitionSignature(plGuid);" guild_remove_at)
string(FIND "${player_source}" "sGuildMgr.RemovePetitionSignature(playerguid);" player_remove_at)
if (guild_remove_at EQUAL -1 OR player_remove_at EQUAL -1)
  message(FATAL_ERROR "Guild petition summary: guild join and character deletion must remove signatures via GuildMgr::RemovePetitionSignature")
endif()
string(FIND "${guild_source}" "sGuildMgr.GetPetitionSignerGuids(petition->GetId(), signers);" founding_at)
if (founding_at EQUAL -1)
  message(FATAL_ERROR "Guild petition summary: Guild::Create must copy the signers under the petition lock")
endif()

message(STATUS "GUILD_PETITION_SUMMARY_CONTRACT=PASS")
