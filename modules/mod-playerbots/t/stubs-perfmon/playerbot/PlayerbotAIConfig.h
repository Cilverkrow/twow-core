// Test double for playerbot/PlayerbotAIConfig.h, for the perfmon collection
// suite. The real sPlayerbotAIConfig is a MaNGOS::Singleton whose constructor
// lives in PlayerbotAIConfig.cpp and reads the whole aiplayerbot.conf; the file
// under test touches exactly one field of it.
//
// Same arrangement as t/stubs/playerbot/PlayerbotAIConfig.h: the suite is given
// t/stubs-perfmon but NOT ${PB_MODULE_DIR}/src, so the spelling
// "playerbot/PlayerbotAIConfig.h" has only this candidate.

#pragma once

struct PerfMonTestConfig
{
    // The toggle `.perfmon toggle` flips and every probe in the monitor reads.
    bool perfMonEnabled = false;
};

inline PerfMonTestConfig& PerfMonTestConfigInstance()
{
    static PerfMonTestConfig instance;
    return instance;
}

#define sPlayerbotAIConfig PerfMonTestConfigInstance()
