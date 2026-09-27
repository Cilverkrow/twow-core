// Test double for PlayerbotAI.h, for the perfmon collection suite.
//
// Only one line of PerformanceMonitor.cpp needs it -- the
// start(metric, name, PlayerbotAI*) overload, which reaches
// ai->GetAiObjectContext()->performanceStack and ai->GetBot()->GetMapId() /
// GetInstanceId(). Those four calls are the entire surface, and the real
// PlayerbotAI.h is 700+ lines that include the game library.
//
// The overload is exercised by the suite, so these are real objects rather than
// forward declarations: a bot on a named map with its own performance stack.

#pragma once

#include "Common.h"
#include <vector>
#include <string>

class Player
{
public:
    Player(uint32 mapId, uint32 instanceId) : m_mapId(mapId), m_instanceId(instanceId) {}

    uint32 GetMapId() const { return m_mapId; }
    uint32 GetInstanceId() const { return m_instanceId; }

private:
    uint32 m_mapId;
    uint32 m_instanceId;
};

class AiObjectContext
{
public:
    std::vector<std::string> performanceStack;
};

class PlayerbotAI
{
public:
    PlayerbotAI(Player* bot, AiObjectContext* context) : m_bot(bot), m_context(context) {}

    Player* GetBot() { return m_bot; }
    AiObjectContext* GetAiObjectContext() { return m_context; }

private:
    Player* m_bot;
    AiObjectContext* m_context;
};
