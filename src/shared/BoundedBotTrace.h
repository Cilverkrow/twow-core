#pragma once
#include <array>
#include <cstdint>

// Temporary diagnostic admission only. Caller serializes access; no entity pointers.
class BoundedBotTrace
{
public:
    bool Take(uint32_t now, uint32_t guid)
    {
        if (finished || !guid) return false;
        if (!started) { started = true; start = now; }
        if (uint32_t(now - start) >= 600000 || emitted >= 8000)
        { finished = true; return false; }
        Slot* selected = nullptr;
        Slot* available = nullptr;
        for (auto& slot : slots)
        {
            if (slot.guid == guid) { selected = &slot; break; }
            if (!available && (!slot.guid || uint32_t(now - slot.lastSeen) >= 30000)) available = &slot;
        }
        if (!selected)
        {
            if (!available) { ++suppressed; return false; }
            *available = Slot{}; available->guid = guid; selected = available;
        }
        selected->lastSeen = now;
        uint32_t second = now / 1000;
        if (selected->second != second) { selected->second = second; selected->count = 0; }
        if (selected->count >= 8) { ++suppressed; return false; }
        ++selected->count; ++emitted;
        return true;
    }
    bool Finished() const { return finished; }
    uint32_t Emitted() const { return emitted; }
    uint64_t Suppressed() const { return suppressed; }
private:
    struct Slot { uint32_t guid=0, lastSeen=0, second=0, count=0; };
    std::array<Slot, 12> slots{};
    bool started=false, finished=false;
    uint32_t start=0, emitted=0;
    uint64_t suppressed=0;
};
