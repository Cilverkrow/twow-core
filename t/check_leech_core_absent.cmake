if(NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

set(unit_cpp "${TW_CORE_ROOT}/src/game/Objects/Unit.cpp")
set(world_cpp "${TW_CORE_ROOT}/src/game/World.cpp")
set(world_h "${TW_CORE_ROOT}/src/game/World.h")
set(core_conf "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in")
set(script_objects_h "${TW_CORE_ROOT}/src/game/ScriptObjects.h")
set(script_mgr_h "${TW_CORE_ROOT}/src/game/ScriptMgr.h")
set(readme "${TW_CORE_ROOT}/README.md")
set(install_linux "${TW_CORE_ROOT}/INSTALL-LINUX.md")
set(install_windows "${TW_CORE_ROOT}/INSTALL-WINDOWS.md")

foreach(file IN ITEMS
    unit_cpp world_cpp world_h core_conf readme install_linux install_windows
    script_objects_h script_mgr_h)
  file(READ "${${file}}" ${file}_text)
endforeach()

# The configurable feature is platform-owned.  These Core implementation,
# configuration, and documentation files must not retain its keys or enums.
foreach(text IN ITEMS unit_cpp_text world_cpp_text world_h_text core_conf_text
    readme_text install_linux_text install_windows_text)
  string(FIND "${${text}}" "Leech." leech_key)
  if(NOT leech_key EQUAL -1)
    message(FATAL_ERROR "Core still exposes a Leech configuration key in ${text}")
  endif()
endforeach()

foreach(needle
    "CONFIG_FLOAT_LEECH_AMOUNT"
    "CONFIG_BOOL_LEECH_ENABLE"
    "CONFIG_BOOL_LEECH_PVE_ONLY"
    "CONFIG_BOOL_LEECH_REAL_PLAYERS_ONLY"
    "CONFIG_BOOL_LEECH_SOLO_ONLY"
    "CONFIG_BOOL_LEECH_DUNGEON_ONLY"
    "SPELL_LEECH_HEAL"
    "18984")
  string(FIND "${unit_cpp_text}" "${needle}" unit_match)
  string(FIND "${world_cpp_text}" "${needle}" world_cpp_match)
  string(FIND "${world_h_text}" "${needle}" world_h_match)
  if(NOT unit_match EQUAL -1 OR NOT world_cpp_match EQUAL -1 OR NOT world_h_match EQUAL -1)
    message(FATAL_ERROR "Core still owns inline Leech behaviour or state: ${needle}")
  endif()
endforeach()

# The late, read-only hook is the stable module seam.  It must remain between
# final pet-damage adjustment and the ordinary death/health handling.
foreach(needle
    "UNITHOOK_ON_DAMAGE_APPLIED"
    "virtual void OnDamageApplied(Unit* /*attacker*/, Unit* /*victim*/, uint32 /*damage*/) {}")
  string(FIND "${script_objects_h_text}" "${needle}" script_object_match)
  if(script_object_match EQUAL -1)
    message(FATAL_ERROR "Required UnitScript late-damage contract is absent: ${needle}")
  endif()
endforeach()

string(FIND "${unit_cpp_text}" "ScriptRegistry<UnitScript>::ForEachEnabledHook(UNITHOOK_ON_DAMAGE_APPLIED" dispatch_offset)
string(FIND "${unit_cpp_text}" "damage *= 0.5f;" final_adjustment_offset)
string(FIND "${unit_cpp_text}" "if (health <= damage" health_handling_offset)
if(dispatch_offset EQUAL -1 OR final_adjustment_offset EQUAL -1 OR health_handling_offset EQUAL -1)
  message(FATAL_ERROR "Late-damage dispatch placement cannot be verified")
endif()
if(NOT final_adjustment_offset LESS dispatch_offset OR NOT dispatch_offset LESS health_handling_offset)
  message(FATAL_ERROR "Late-damage dispatch no longer surrounds the final damage value")
endif()

# A missing listener must be a no-op: the registry returns before iteration
# when the hook has no registered vector, and invokes callbacks only inside it.
string(FIND "${script_mgr_h_text}" "if (hook >= EnabledHooks.size())\n                return;" empty_hook_return)
string(FIND "${script_mgr_h_text}" "for (TScript* script : EnabledHooks[hook])" enabled_hook_iteration)
if(empty_hook_return EQUAL -1 OR enabled_hook_iteration EQUAL -1 OR empty_hook_return GREATER enabled_hook_iteration)
  message(FATAL_ERROR "No-listener late-damage hook is not a verified no-op")
endif()

message(STATUS "LEECH_CORE_ABSENT_AND_HOOK_PRESENT=PASS")
