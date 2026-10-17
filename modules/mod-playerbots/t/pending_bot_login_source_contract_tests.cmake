# twow-repo#595 (4) (CLI-DEEPDIVE, 16.10.2026): the .bot add login callback is not bound to the per-master
# PlayerbotHolder any more; ~PlayerbotHolder orphans its pending logins, an orphaned login is dropped.

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
    message(FATAL_ERROR "#595 pending bot login: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#595 pending bot login: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#595 pending bot login: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#595 pending bot login: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotMgr.cpp" mgr)
read_source("PlayerbotMgr.h" mgr_h)

# The callback is static; nothing queues a login bound to `this` any more.
require_text("${mgr_h}" "static void HandlePendingBotLogin(QueryResult* dummy, SqlQueryHolder* holder, uint32 unused);" "static callback")
require_text("${mgr}" "CharacterDatabase.DelayQueryHolderUnsafe(&PlayerbotHolder::HandlePendingBotLogin, holder, uint32(0))" "queued static, main thread")
forbid_text("${mgr}" "DelayQueryHolderUnsafe(this, &PlayerbotHolder::HandlePlayerBotLoginCallback" "a callback bound to the holder")
require_text("${mgr}" "m_pendingBotLogins[holder] = { botGuid, masterAccountId, this };" "every pending login names its owner")

# The destructor orphans; the static callback drops orphans and frees their holder.
between("${mgr}" "PlayerbotHolder::~PlayerbotHolder()" "\n}\n" dtor)
require_text("${dtor}" "ai::pending_login::Orphan(m_pendingBotLogins, this);" "destructor orphans its logins")
between("${mgr}" "void PlayerbotHolder::HandlePendingBotLogin(" "\n}\n" cb)
require_text("${cb}" "case ai::pending_login::Route::Orphaned:" "orphan branch")
between("${cb}" "case ai::pending_login::Route::Orphaned:" "case ai::pending_login::Route::Deliver:" orphan)
require_text("${orphan}" "m_pendingBotLogins.erase(it);" "orphan erased")
require_text("${orphan}" "delete holder;" "orphan's query holder freed")
forbid_text("${orphan}" "->HandlePlayerBotLoginCallback" "an orphan reaching any holder")
require_text("${cb}" "m_pendingBotLogins.find(holder)->second.owner->HandlePlayerBotLoginCallback(dummy, holder);" "others go to their owner")

message(STATUS "pending_bot_login source contract passed")
