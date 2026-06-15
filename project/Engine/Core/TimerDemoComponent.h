#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "SceneManager.h"
#include "TransformComponent.h"
#include "Event.h"
#include "TimerManager.h"

namespace Fujin {

// Demo of the Event + Timer foundation (the engine's analog of a UE5 C++ component that uses
// FTimerManager + a multicast delegate). While the editor is in Play, a looping world timer fires
// every `Interval` seconds and Broadcasts OnPulse; a listener subscribed in BeginPlay reacts by
// "popping" the owner's scale, so the cube blinks bigger/smaller in discrete steps — visibly
// timer-driven (not a smooth per-frame Update). Remove the actor (and the include in main.cpp) to
// drop it; the systems it exercises live in Event.h / TimerManager.h and SceneManager.
class TimerDemoComponent : public Component {
public:
    float Interval = 0.5f;   // seconds between pulses
    float PopScale = 1.4f;   // multiplier applied to the base scale on every other pulse

    // Multicast event fired on each timer pulse (carries the running pulse count). Other systems
    // could subscribe via OnPulse.Add(...) — here BeginPlay binds one listener that pops the scale.
    MulticastDelegate<int> OnPulse;

    const char* GetTypeName() const override { return "TimerDemoComponent"; }
    // Reflect is the single source of truth (Inspector + default ToJson/FromJson).
    void Reflect(IPropertyVisitor& v) override {
        v.Float("interval", "Interval (s)", &Interval, 0.05f, 0.05f, 10.0f);
        v.Float("popScale", "Pop Scale",    &PopScale, 0.05f, 1.0f, 4.0f);
    }

    void BeginPlay() override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        if (tc) m_baseScale = tc->Scale;
        m_popped = false;
        m_pulseCount = 0;

        // React to each pulse: toggle the owner's scale between base and base*PopScale.
        m_pulseHandle = OnPulse.Add([this](int /*count*/) {
            auto* t = GetOwner()->GetComponent<TransformComponent>();
            if (!t) return;
            m_popped = !m_popped;
            t->Scale = m_popped ? m_baseScale * PopScale : m_baseScale;
        });

        // Drive the pulses from a looping world timer.
        if (auto* scene = GetOwner()->GetScene())
            scene->GetTimerManager().SetTimer(m_timer, Interval, /*looping*/true,
                [this]() { OnPulse.Broadcast(++m_pulseCount); });
    }

    void EndPlay() override {
        if (auto* scene = GetOwner()->GetScene())
            scene->GetTimerManager().ClearTimer(m_timer);
        OnPulse.Remove(m_pulseHandle);
        // Transform is restored from the play-start snapshot by main.cpp on Stop, so no reset here.
    }

private:
    TimerHandle    m_timer;
    DelegateHandle m_pulseHandle;
    Vector3        m_baseScale{ 1.0f, 1.0f, 1.0f };
    bool           m_popped = false;
    int            m_pulseCount = 0;
};

// Register for serialization so a TimerDemo added via the editor round-trips through save/load.
inline const bool s_timerDemoRegistered = []() {
    ComponentRegistry::Get().Register("TimerDemoComponent",
        []() { return std::make_unique<TimerDemoComponent>(); });
    return true;
}();

} // namespace Fujin
