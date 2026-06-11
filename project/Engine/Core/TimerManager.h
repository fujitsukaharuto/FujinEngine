#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace Fujin {

// Lightweight handle to a scheduled timer. Default-constructed handles are invalid.
struct TimerHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    void Invalidate() { id = 0; }
    bool operator==(const TimerHandle& o) const { return id == o.id; }
};

// UE5 FTimerManager analog. Owned by the world (SceneManager) and ticked once per gameplay frame
// (PrePhysics, before component Update). Schedule delayed/looping callbacks without writing your own
// countdown in Update:
//   handle = timers.SetTimer(2.0f, /*looping*/false, []{ ... });   // fire once after 2s
//   timers.SetTimer(handle, 0.5f, true, [this]{ Pulse(); });        // fire every 0.5s, replaces handle
//   timers.SetTimerForNextTick([]{ ... });                          // run once on the next tick
//
// Re-entrancy safe: a callback may set/clear timers (including its own). Changes requested during a
// Tick are queued and applied once the Tick completes, so the in-progress sweep is never disturbed.
class TimerManager {
public:
    // Fire `fn` after `rate` seconds; if `looping`, repeat every `rate`. `firstDelay` < 0 uses `rate`
    // for the first interval too. A looping timer needs rate > 0 (else it would fire every frame);
    // rate <= 0 is coerced to a single shot on the next tick.
    TimerHandle SetTimer(float rate, bool looping, std::function<void()> fn, float firstDelay = -1.0f) {
        TimerHandle h{ ++m_nextId };
        Timer t;
        t.id        = h.id;
        t.rate      = rate;
        t.remaining = (firstDelay >= 0.0f) ? firstDelay : rate;
        t.looping   = looping && rate > 0.0f;
        t.fn        = std::move(fn);
        (m_ticking ? m_pendingAdd : m_timers).push_back(std::move(t));
        return h;
    }

    // UE5-style overload: clears whatever `h` previously referenced, then sets a fresh timer into it.
    void SetTimer(TimerHandle& h, float rate, bool looping, std::function<void()> fn, float firstDelay = -1.0f) {
        ClearTimer(h);
        h = SetTimer(rate, looping, std::move(fn), firstDelay);
    }

    // Run `fn` exactly once on the next Tick.
    TimerHandle SetTimerForNextTick(std::function<void()> fn) {
        return SetTimer(0.0f, false, std::move(fn), 0.0f);
    }

    void ClearTimer(TimerHandle& h) {
        if (h.IsValid()) { MarkCleared(h.id); h.Invalidate(); }
    }

    void ClearAllTimers() {
        if (m_ticking) { for (auto& t : m_timers) t.cleared = true; m_pendingAdd.clear(); }
        else           { m_timers.clear(); }
    }

    bool  IsTimerActive(const TimerHandle& h) const { const Timer* t = Find(h.id); return t && !t->paused; }
    bool  IsTimerPaused(const TimerHandle& h) const { const Timer* t = Find(h.id); return t &&  t->paused; }
    bool  TimerExists(const TimerHandle& h)   const { return Find(h.id) != nullptr; }
    float GetTimerRate(const TimerHandle& h)      const { const Timer* t = Find(h.id); return t ? t->rate : -1.0f; }
    float GetTimerRemaining(const TimerHandle& h) const { const Timer* t = Find(h.id); return t ? t->remaining : -1.0f; }
    float GetTimerElapsed(const TimerHandle& h)   const { const Timer* t = Find(h.id); return t ? (t->rate - t->remaining) : -1.0f; }

    void PauseTimer(const TimerHandle& h)   { if (Timer* t = Find(h.id)) t->paused = true;  }
    void UnPauseTimer(const TimerHandle& h) { if (Timer* t = Find(h.id)) t->paused = false; }

    // Advance all timers by `dt` and fire any that come due. Call once per gameplay frame.
    void Tick(float dt) {
        m_ticking = true;
        // Index loop: re-entrant SetTimer goes to m_pendingAdd (not m_timers), so m_timers never
        // reallocates here and the references below stay valid for the duration of the sweep.
        for (size_t i = 0; i < m_timers.size(); ++i) {
            Timer& t = m_timers[i];
            if (t.cleared || t.paused) continue;
            t.remaining -= dt;
            int guard = 0;   // bound catch-up so a huge dt can't fire a fast loop unboundedly
            while (t.remaining <= 0.0f && !t.cleared) {
                auto fn = t.fn;   // copy first: the callback may clear or replace this very timer
                if (fn) fn();
                if (t.cleared) break;
                if (!t.looping) { t.cleared = true; break; }
                t.remaining += t.rate;
                if (++guard >= 8) { if (t.remaining < 0.0f) t.remaining = t.rate; break; }
            }
        }
        m_ticking = false;

        // Apply deferred clears, then deferred adds.
        if (!m_timers.empty())
            m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(),
                [](const Timer& t) { return t.cleared; }), m_timers.end());
        if (!m_pendingAdd.empty()) {
            for (auto& t : m_pendingAdd) m_timers.push_back(std::move(t));
            m_pendingAdd.clear();
        }
    }

private:
    struct Timer {
        uint64_t              id = 0;
        float                 rate = 0.0f;       // interval in seconds
        float                 remaining = 0.0f;  // time left until the next fire
        bool                  looping = false;
        bool                  paused = false;
        bool                  cleared = false;   // tombstone (compacted at end of Tick)
        std::function<void()> fn;
    };

    Timer* Find(uint64_t id) {
        for (auto& t : m_timers)     if (t.id == id && !t.cleared) return &t;
        for (auto& t : m_pendingAdd) if (t.id == id && !t.cleared) return &t;
        return nullptr;
    }
    const Timer* Find(uint64_t id) const {
        for (auto& t : m_timers)     if (t.id == id && !t.cleared) return &t;
        for (auto& t : m_pendingAdd) if (t.id == id && !t.cleared) return &t;
        return nullptr;
    }
    void MarkCleared(uint64_t id) {
        for (auto& t : m_timers) if (t.id == id) { t.cleared = true; return; }
        for (auto it = m_pendingAdd.begin(); it != m_pendingAdd.end(); ++it)
            if (it->id == id) {
                if (m_ticking) it->cleared = true; else m_pendingAdd.erase(it);
                return;
            }
    }

    std::vector<Timer> m_timers;
    std::vector<Timer> m_pendingAdd;   // timers scheduled during a Tick, merged in afterwards
    uint64_t           m_nextId = 0;
    bool               m_ticking = false;
};

} // namespace Fujin
