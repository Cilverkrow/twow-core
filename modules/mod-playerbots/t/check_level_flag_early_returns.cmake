# Registered as the ctest `playerbot_level_flag_early_return_guard`. Run with
#   cmake -DPB_MODULE_DIR=<modules/mod-playerbots> -P this-file
#
# What this prevents
# ------------------
# AiPlayerbot.DisableRandomLevels reads as "do not roll a random level". It was
# implemented as three blanket early returns that each skipped very much more:
#
#   PlayerbotFactory::Randomize      spells, skills, professions, talents,
#                                    mounts, reputations, quest rewards
#   RandomPlayerbotMgr::RandomizeFirst   the whole factory
#   RandomPlayerbotMgr::Refresh      repair, heal, power, consumables, money
#
# The result was self-defeating on the setting's own terms. Its premise is that
# bots start low and work their way up by killing things; a bot with no class
# spells cannot kill anything, so it never levels. Measured on the realm where
# this was found, before the fix: 5,021 of 5,039 characters at level 1, 8 with
# any spell at all, 16 with any money, 2 with a profession.
#
# Nothing caught it. A blanket `return` compiles cleanly, links cleanly, runs
# cleanly and produces a population that looks alive from every angle except a
# database query nobody was running. There is no crash, no error, no log line and
# no failing test -- so grepping is the only mechanical thing that catches it,
# and grepping is what this does.
#
# Why a BARE return specifically
# ------------------------------
# `return;` in a void initialisation path is the defect. `return <expr>;` is not,
# and the difference is load-bearing rather than stylistic:
#
#   PlayerbotLoginMgr.cpp:48-49
#       if (!isNew || sPlayerbotAIConfig.disableRandomLevels)
#           return level;
#
# That function computes a level, and returning the level it already has is
# exactly what the flag should make it do. A guard that failed on it would be
# wrong, and worse, it would teach the next reader that the guard is noise. So
# the pattern requires nothing but whitespace between `return` and `;`.
#
# Negated uses are likewise fine and are not matched, because the flag is then
# followed by an operator rather than the closing parenthesis:
#
#   RandomPlayerbotMgr.cpp:3519   if (!sPlayerbotAIConfig.disableRandomLevels &&
#   RandomPlayerbotFactory.cpp    if (sPlayerbotAIConfig.disableRandomLevels && ...)
#
# If a bare return under this flag is ever genuinely correct
# ---------------------------------------------------------
# It would have to be in a function that does nothing but decide a level. Adding
# it means changing this guard in the same commit, with the reasoning written
# down -- which is the point. The cost of this test is that the decision becomes
# explicit, not that it becomes impossible.

if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "PB_MODULE_DIR is required")
endif()

file(GLOB_RECURSE runtime_files
  "${PB_MODULE_DIR}/src/*.cpp"
  "${PB_MODULE_DIR}/src/*.h")

if(NOT runtime_files)
  # An empty glob would make this test pass by scanning nothing, which is the one
  # failure mode a guard must never have.
  message(FATAL_ERROR
    "No sources found under ${PB_MODULE_DIR}/src -- the level-flag early return "
    "guard scanned nothing and cannot report a pass.")
endif()

# Whitespace is collapsed before matching so the pattern does not have to model
# brace style, line breaks or indentation. Matching is done against the
# UPPERCASED text, so the pattern is uppercase and the check is case-insensitive.
# The parenthesis and brace go in character classes rather than being escaped:
# CMake's regex engine rejects a backslash-escaped ")" outright.
set(bare_return_pattern "DISABLERANDOMLEVELS *[)] *[{]? *RETURN *;")

set(hits "")
foreach(path IN LISTS runtime_files)
  file(READ "${path}" content)
  string(TOUPPER "${content}" content)
  string(REGEX REPLACE "[ \t\r\n]+" " " content "${content}")
  string(REGEX MATCH "${bare_return_pattern}" forbidden "${content}")
  if(forbidden)
    list(APPEND hits "${path}")
  endif()
endforeach()

if(hits)
  string(REPLACE ";" "\n  " hits_pretty "${hits}")
  message(FATAL_ERROR
    "DisableRandomLevels used as a blanket early return in:\n  ${hits_pretty}\n"
    "That flag disables the level ROLL. Everything else -- spells, skills, "
    "professions, talents, mounts, reputations, repair, money -- must still "
    "happen, or bots are created unable to play and never level, which is the "
    "opposite of what the setting exists to do. Gate the level decision itself "
    "instead of returning.")
endif()

list(LENGTH runtime_files runtime_file_count)
message(STATUS
  "PlayerBot level-flag early return guard passed (${runtime_file_count} files scanned)")
