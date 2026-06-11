#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Engine/Math/Math.h"
#include <string>
#include <vector>

namespace Fujin {

// One IK leg chain: three bones root (hip/upper) → mid (knee/elbow) → end (foot/paw). Two-bone IK
// rotates root & mid so `end` reaches a ground-traced target. `Offset` adds to the component's global
// FootOffset for this leg only — e.g. a quadruped's front paws (the End bone IS the contact) need a
// different lift than the rear feet (End is the ankle, with the toe bone below it).
struct IKLeg { std::string Root, Mid, End; float Offset = 0.0f; };

// Foot-placement IK (the engine's UE5 foot-IK analog). Driven in SceneRenderer right after the
// animation pose is sampled: each leg's foot is ray-traced down to the ground and planted via
// two-bone IK so the character's feet follow steps / slopes instead of floating or clipping. Feet
// already above the ground (swing phase) are left untouched. Legs are populated in code (like a
// blend space's samples — not serialized); the scalars below are tunable in the editor and saved.
class FootIKComponent : public Component {
public:
    bool  Enabled    = true;
    float TraceUp    = 0.5f;   // start the down-trace this far above the animated foot (world units)
    float TraceDown  = 0.8f;   // ...and reach this far below it
    float FootOffset = 0.0f;   // keep the foot this far above the traced ground
    float MaxRaise   = 0.5f;   // clamp how far a foot may be lifted toward higher ground
    float Weight     = 1.0f;   // overall blend [0..1] (0 = no IK)
    float FootSmoothness = 12.0f; // ease the per-foot IK correction in/out (1/s) so plant↔swing doesn't pop
    bool  AlignToNormal = true;// rotate each planted foot to match the ground's surface normal (slopes)

    // Pelvis (hip) adaptation: lower the body so the foot that needs to drop most can still reach the
    // ground, instead of that leg over-extending / the foot floating. Empty PelvisBone = disabled.
    std::string PelvisBone;            // e.g. "b_Hip_01" — the bone all legs hang from
    float       MaxPelvisDrop = 0.25f; // clamp how far the pelvis may be lowered (world units)
    float       PelvisSmoothness = 10.0f; // how fast the pelvis offset eases (1/s); 0 = instant

    // Body lean: rotate the pelvis (whole torso) toward the ground slope so the body follows ramps /
    // stairs instead of staying level. Slope is the best-fit plane through the planted feet (so flat
    // stairs still lean from front/back foot-height differences). Needs PelvisBone set.
    bool  BodyTilt           = true;
    float BodyTiltPitch      = 0.7f;   // how much the body pitches to the slope when facing up/down it
    float BodyTiltRoll       = 0.7f;   // how much the body rolls (one side drops) when traversing across
    float BodyTiltSmoothness = 10.0f;  // how fast the lean eases (1/s); 0 = instant

    // Foot lock (anti-slide): while a foot is planted (its animated height stays under LockHeight) its
    // world position is frozen so it doesn't skate as the body moves over it; released during swing.
    bool  FootLock       = true;
    float LockHeight     = 0.08f;  // animated foot height above ground (world) below which it's "planted"
    float LockSmoothness = 14.0f;  // how fast the lock blends in/out (1/s)

    std::vector<IKLeg> Legs;
    // ── runtime (not serialized): smoothed values + their SmoothDamp velocities + per-leg knee axis ──
    float              m_pelvisOffset = 0.0f;            // smoothed pelvis drop
    float              m_pelvisVel    = 0.0f;
    Vector3            m_bodyNormal   = Vector3(0,1,0);  // smoothed ground normal for body lean
    Vector3            m_bodyNormalVel= Vector3(0,0,0);
    std::vector<float> m_footRaise;                      // per-leg smoothed IK lift (eases plant/swing)
    std::vector<float> m_footRaiseVel;
    std::vector<Vector3> m_kneeAxis;                     // per-leg last good knee-bend axis (stabilises)
    std::vector<Vector3> m_lockPos;                      // per-leg frozen world foot position while planted
    std::vector<float>   m_lockAlpha;                    // per-leg lock blend [0..1]
    std::vector<float>   m_lockAlphaVel;

    const char* GetTypeName() const override { return "FootIKComponent"; }
    void ToJson(nlohmann::json& j) const override {
        j["enabled"]    = Enabled;    j["traceUp"]  = TraceUp;  j["traceDown"] = TraceDown;
        j["footOffset"] = FootOffset; j["maxRaise"] = MaxRaise; j["weight"]    = Weight;
        j["footSmoothness"] = FootSmoothness;
        j["alignToNormal"] = AlignToNormal;
        j["pelvisBone"] = PelvisBone; j["maxPelvisDrop"] = MaxPelvisDrop;
        j["pelvisSmoothness"] = PelvisSmoothness;
        j["bodyTilt"] = BodyTilt; j["bodyTiltPitch"] = BodyTiltPitch; j["bodyTiltRoll"] = BodyTiltRoll;
        j["bodyTiltSmoothness"] = BodyTiltSmoothness;
        j["footLock"] = FootLock; j["lockHeight"] = LockHeight; j["lockSmoothness"] = LockSmoothness;
    }
    void FromJson(const nlohmann::json& j) override {
        Enabled         = j.value("enabled",         Enabled);
        TraceUp         = j.value("traceUp",         TraceUp);
        TraceDown       = j.value("traceDown",       TraceDown);
        FootOffset      = j.value("footOffset",      FootOffset);
        MaxRaise        = j.value("maxRaise",        MaxRaise);
        Weight          = j.value("weight",          Weight);
        FootSmoothness  = j.value("footSmoothness",  FootSmoothness);
        AlignToNormal   = j.value("alignToNormal",   AlignToNormal);
        PelvisBone      = j.value("pelvisBone",      PelvisBone);
        MaxPelvisDrop   = j.value("maxPelvisDrop",   MaxPelvisDrop);
        PelvisSmoothness= j.value("pelvisSmoothness",PelvisSmoothness);
        BodyTilt        = j.value("bodyTilt",        BodyTilt);
        const float legacyTilt = j.value("bodyTiltWeight", -1.0f);   // migrate old single-weight saves
        BodyTiltPitch   = j.value("bodyTiltPitch",   legacyTilt >= 0.0f ? legacyTilt : BodyTiltPitch);
        BodyTiltRoll    = j.value("bodyTiltRoll",    legacyTilt >= 0.0f ? legacyTilt : BodyTiltRoll);
        BodyTiltSmoothness = j.value("bodyTiltSmoothness", BodyTiltSmoothness);
        FootLock        = j.value("footLock",        FootLock);
        LockHeight      = j.value("lockHeight",      LockHeight);
        LockSmoothness  = j.value("lockSmoothness",  LockSmoothness);
    }
    void Reflect(IPropertyVisitor& v) override {
        v.Bool ("Enabled",         &Enabled);
        v.Float("Trace Up",        &TraceUp,    0.05f,  0.0f, 5.0f);
        v.Float("Trace Down",      &TraceDown,  0.05f,  0.0f, 5.0f);
        v.Float("Foot Offset",     &FootOffset, 0.01f, -1.0f, 1.0f);
        v.Float("Max Raise",       &MaxRaise,   0.05f,  0.0f, 5.0f);
        v.Float("Weight",          &Weight,     0.05f,  0.0f, 1.0f);
        v.Float("Foot Smooth",     &FootSmoothness, 0.5f, 0.0f, 60.0f);
        v.Bool ("Align To Normal", &AlignToNormal);
        v.Float("Max Pelvis Drop", &MaxPelvisDrop,    0.01f, 0.0f, 2.0f);
        v.Float("Pelvis Smooth",   &PelvisSmoothness, 0.5f,  0.0f, 60.0f);
        v.Bool ("Body Tilt",        &BodyTilt);
        v.Float("Body Tilt Pitch",  &BodyTiltPitch,      0.05f, 0.0f, 1.0f);
        v.Float("Body Tilt Roll",   &BodyTiltRoll,       0.05f, 0.0f, 1.0f);
        v.Float("Body Tilt Smooth", &BodyTiltSmoothness, 0.5f,  0.0f, 60.0f);
        v.Bool ("Foot Lock",        &FootLock);
        v.Float("Lock Height",      &LockHeight,     0.01f, 0.0f, 2.0f);
        v.Float("Lock Smooth",      &LockSmoothness, 0.5f,  0.0f, 60.0f);
    }
};

inline const bool s_footIKRegistered = []() {
    ComponentRegistry::Get().Register("FootIKComponent",
        []() { return std::make_unique<FootIKComponent>(); });
    return true;
}();

} // namespace Fujin
