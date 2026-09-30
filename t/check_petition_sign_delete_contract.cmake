if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# A removed petition signature deletes exactly its own row (petition + signer), never the
# signatures on the signer's own charter (ownerguid) - that left the row in place and made
# the next re-sign fail with a duplicate key (bots, train 7.5 smoke).
file(READ "${TW_CORE_ROOT}/src/game/Guild/GuildMgr.cpp" mgr)
string(FIND "${mgr}" "DELETE FROM petition_sign WHERE petitionguid = '%u' AND playerguid = '%u'" at)
if (at EQUAL -1)
  message(FATAL_ERROR "PetitionSignature::DeleteFromDB must delete by petitionguid and playerguid")
endif()
string(FIND "${mgr}" "DELETE FROM petition_sign WHERE ownerguid = '%u'\", m_playerGuid" at)
if (NOT at EQUAL -1)
  message(FATAL_ERROR "PetitionSignature::DeleteFromDB must not delete by ownerguid = signer")
endif()

message(STATUS "PETITION_SIGN_DELETE_CONTRACT=PASS")
