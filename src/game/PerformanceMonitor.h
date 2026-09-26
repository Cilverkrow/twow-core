#pragma once

#include "Common.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "AllocatorWithCategory.h"

#include <vector>

class ChatHandler;

struct PerformanceMonitor : public IPerfMonitor
{
	PerformanceMonitor();

	void Initialize();
	void FrameStart();
	void FrameEnd(uint32 delta);

	void SetReportInterval(uint32 IntervalInSeconds);

	// twow-repo#351: work time of one world tick (without the sleep) and the
	// real time since the previous tick. With PerformanceLog.TickStats on,
	// writes one aggregated perf.log line per PerformanceLog.TickStatsInterval.
	void RecordTick(uint32 UpdateMs, uint32 DiffMs);

	void ReportCPU(ChatHandler& Handler);
	void ReportMemory(ChatHandler& Handler);

	virtual void ReportAlloc(const char* Category, size_t Bytes) override;
	virtual void ReportDealloc(const char* Category, size_t Bytes) override;

	XStatTimer Tick;
	XStatTimer WorldSleep;
	XStatTimer WorldTick;
	XStatTimer UpdateSession;
	XStatTimer MapManager;
	XStatTimer TEST0;

protected:

	uint32 QPC_Counter = 0;

	void ReportPerformanceToDB();

	IntervalTimer IntervalReport;

	std::vector<uint32> TickSamples;
	uint32 TickStatsElapsedMs = 0;

	using MemBytesMap = std::unordered_map<const char*, int64>;
	MemBytesMap MemBytes;
	std::mutex MemBytesGuard;
};

extern PerformanceMonitor sPerfMonitor;