#pragma once
// twow-repo#541 (audit A25, AiPlayerbot.Perf.PartyTargetMemo, default 0).
//
// One bot's "same decision" window. Engine::DoNextAction opens it on the thread that updates the
// bot, Engine::ListenAndExecute advances it after every action Execute, and it closes when
// DoNextAction returns. Epoch() is non-zero only inside an open window and only on the thread that
// opened it. Everywhere else - chat and packet handlers outside DoNextAction, reactions, any other
// thread - it is 0, and a value that keys on it recomputes exactly as before.
//
// No lock, no shared container, nothing process-wide: one instance per AiObjectContext (= per bot).
// `nest` is only touched by the owner thread; `owner` is atomic so a foreign reader gets a clean
// "not mine" instead of a torn id; `epoch` is atomic so an Advance() from anywhere still
// invalidates. Header-only and std-only so t/value_memo_window_tests.cpp can test it directly.
#include <atomic>
#include <cstdint>
#include <thread>

namespace ai
{
    class ValueMemoWindow
    {
    public:
        ValueMemoWindow() = default;
        ValueMemoWindow(const ValueMemoWindow&) = delete;
        ValueMemoWindow& operator=(const ValueMemoWindow&) = delete;

        void Open()
        {
            std::thread::id const self = std::this_thread::get_id();
            if (owner.load(std::memory_order_acquire) == self)
            {
                ++nest;
                Advance();
                return;
            }
            nest = 1;
            Advance();
            owner.store(self, std::memory_order_release);
        }

        void Close()
        {
            if (owner.load(std::memory_order_acquire) != std::this_thread::get_id())
                return;
            Advance();
            if (--nest == 0)
                owner.store(std::thread::id(), std::memory_order_release);
        }

        void Advance() { epoch.fetch_add(1, std::memory_order_relaxed); }

        // 0 = no window on this thread (also after a uint32 wrap: one extra recompute, harmless).
        uint32_t Epoch() const
        {
            if (owner.load(std::memory_order_acquire) != std::this_thread::get_id())
                return 0;
            return epoch.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<std::thread::id> owner{ std::thread::id() };
        std::atomic<uint32_t> epoch{ 0 };
        uint32_t nest = 0;
    };

    // RAII: a null window (switch off) does nothing.
    class ValueMemoWindowScope
    {
    public:
        explicit ValueMemoWindowScope(ValueMemoWindow* w) : window(w) { if (window) window->Open(); }
        ~ValueMemoWindowScope() { if (window) window->Close(); }
        ValueMemoWindowScope(const ValueMemoWindowScope&) = delete;
        ValueMemoWindowScope& operator=(const ValueMemoWindowScope&) = delete;

    private:
        ValueMemoWindow* window;
    };
}
