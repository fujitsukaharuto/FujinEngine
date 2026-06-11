#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <utility>
#include <algorithm>

namespace Fujin {

// Identifies one subscription to a MulticastDelegate. Returned by Add, passed back to Remove.
struct DelegateHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    void Reset() { id = 0; }
    bool operator==(const DelegateHandle& o) const { return id == o.id; }
};

// UE5-style multicast delegate ("event"): many listeners, fire-and-forget Broadcast. This is the
// engine's analog of DECLARE_MULTICAST_DELEGATE — gameplay code exposes one of these as a public
// field (e.g. `MulticastDelegate<Actor*> OnDied;`) and other systems subscribe with Add().
//
// Re-entrancy safe: a listener may Add/Remove/Clear (even its own subscription) during a Broadcast.
// Broadcast walks a snapshot of the listeners present when it began, skips any removed mid-call, and
// does NOT invoke listeners added mid-call (they run on the next Broadcast — matching UE5).
template<typename... Args>
class MulticastDelegate {
public:
    using FuncType = std::function<void(Args...)>;

    // Subscribe. The returned handle can later be passed to Remove.
    DelegateHandle Add(FuncType fn) {
        DelegateHandle h{ ++m_nextId };
        m_listeners.push_back({ h.id, std::move(fn), false });
        return h;
    }

    // Unsubscribe the listener identified by `h` (and invalidate the handle). No-op if not found.
    bool Remove(DelegateHandle& h) {
        if (!h.IsValid()) return false;
        bool removed = false;
        for (auto& e : m_listeners)
            if (e.id == h.id) { e.removed = true; removed = true; break; }
        h.Reset();
        if (m_broadcasting == 0) Compact();
        return removed;
    }

    // Drop every listener.
    void Clear() {
        if (m_broadcasting > 0) { for (auto& e : m_listeners) e.removed = true; }
        else                    m_listeners.clear();
    }

    bool IsBound() const {
        for (auto& e : m_listeners) if (!e.removed) return true;
        return false;
    }

    // Invoke every currently-bound listener with `args`.
    void Broadcast(Args... args) {
        ++m_broadcasting;
        // Snapshot the count: listeners appended mid-call land past `count` and are skipped this pass.
        // Index access re-reads the vector each step, so a re-entrant Add that reallocates is safe.
        const size_t count = m_listeners.size();
        for (size_t i = 0; i < count; ++i) {
            if (!m_listeners[i].removed && m_listeners[i].fn)
                m_listeners[i].fn(args...);
        }
        if (--m_broadcasting == 0) Compact();
    }

private:
    struct Entry { uint64_t id; FuncType fn; bool removed; };

    void Compact() {
        m_listeners.erase(
            std::remove_if(m_listeners.begin(), m_listeners.end(),
                [](const Entry& e) { return e.removed; }),
            m_listeners.end());
    }

    std::vector<Entry> m_listeners;
    uint64_t           m_nextId = 0;
    int                m_broadcasting = 0;   // re-entrancy depth (deferred compaction while > 0)
};

// UE5-style single-cast delegate: at most one bound callable, with an explicit return-less invoke.
// Use when exactly one owner should respond (e.g. a callback slot) rather than a broadcast.
template<typename... Args>
class Delegate {
public:
    using FuncType = std::function<void(Args...)>;

    void Bind(FuncType fn) { m_fn = std::move(fn); }
    void Unbind()          { m_fn = nullptr; }
    bool IsBound() const   { return static_cast<bool>(m_fn); }

    void Execute(Args... args) const        { m_fn(args...); }
    void ExecuteIfBound(Args... args) const { if (m_fn) m_fn(args...); }

private:
    FuncType m_fn;
};

} // namespace Fujin
