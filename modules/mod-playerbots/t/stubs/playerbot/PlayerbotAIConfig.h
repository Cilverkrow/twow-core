// Test double for playerbot/PlayerbotAIConfig.h.
//
// PlayerbotDatabaseContract.h reads the event-store table name out of
// sPlayerbotAIConfig, so it includes "playerbot/PlayerbotAIConfig.h" -- and
// that header pulls in Config/Config.h, SharedDefines.h, SystemConfig.h and
// Talentspec.h, while sPlayerbotAIConfig itself is a
// MaNGOS::Singleton<PlayerbotAIConfig> whose constructor lives in
// PlayerbotAIConfig.cpp. Compiling that one .cpp drags in the whole playerbot
// module and the game library behind it, which is not a price a two-file SQL
// builder test should pay.
//
// So the two event-store suites are given this directory ahead of the module
// sources on their include path, and are given `${PB_MODULE_DIR}/src/playerbot`
// but NOT `${PB_MODULE_DIR}/src`. The real header is spelled
// "playerbot/PlayerbotAIConfig.h" and is therefore unreachable from those two
// targets: there is no shadowing and no ambiguity about which one is in play,
// the real one simply is not on the path. Every other target in the tree keeps
// the real config.
//
// The field below is the entire surface PlayerbotDatabaseContract.h touches.
// If it ever touches more, this file fails to compile, which is the intended
// signal -- not a reason to widen it silently.

#pragma once

#include <string>

struct PlayerbotAIConfigTestDouble
{
    // Core's default table, from PlayerbotAIConfig.cpp:
    //   config.GetStringDefault("AiPlayerbot.EventStoreTable",
    //                           "ai_playerbot_random_bots")
    // Backquoted here because the builders splice this value straight into SQL
    // and a test that skipped the quoting would be testing a spelling no
    // deployment uses. Both suites override it anyway -- the contract suite per
    // assertion, the database suite from argv.
    std::string eventStoreTable = "`ai_playerbot_random_bots`";
};

inline PlayerbotAIConfigTestDouble& PlayerbotAIConfigTestInstance()
{
    static PlayerbotAIConfigTestDouble instance;
    return instance;
}

#define sPlayerbotAIConfig PlayerbotAIConfigTestInstance()
