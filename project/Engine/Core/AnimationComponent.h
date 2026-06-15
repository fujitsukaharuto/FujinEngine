#pragma once
#include "Component.h"
#include "Engine/Animation/AnimationTypes.h"
#include "Engine/Animation/AnimStateMachine.h"
#include <array>
#include <string>
#include <vector>
#include <unordered_map>

namespace Fujin {

class AnimationComponent : public Component {
public:
    std::string ClipName;          // "" → play the first clip found
    float       Speed    = 1.0f;
    bool        Loop     = true;
    bool        Playing  = true;

    float       TimeOffset = 0.0f;  // phase offset in seconds (shifts start point)

    // ── Higher-level animation (optional, opt-in). Evaluation order in SceneRenderer:
    //   UseStateMachine → run the state machine (states may be clips or blend spaces, cross-fades)
    //   else UseBlendSpace → evaluate a single 1D blend space
    //   else → play the single clip above (legacy path, unchanged).
    bool         UseBlendSpace   = false;
    BlendSpace1D Blend;            // 1D blend space config (when UseBlendSpace)
    bool         UseStateMachine  = false;
    AnimStateMachine StateMachine; // state machine config (when UseStateMachine)

    // Gameplay blackboard: named floats gameplay sets each frame (e.g. SetParam("Speed", vel)).
    // Drives blend spaces and state-machine transitions. Runtime only (not serialized).
    std::unordered_map<std::string, float> Params;
    void  SetParam(const std::string& name, float v) { Params[name] = v; }
    float GetParam(const std::string& name) const {
        auto it = Params.find(name);
        return it != Params.end() ? it->second : 0.0f;
    }

    // Runtime state (managed by SceneRenderer — do not set manually).
    float       Time     = 0.0f;   // elapsed playback time in seconds (single-clip path)
    float       Phase    = 0.0f;   // normalised loop phase [0,1) for blend spaces (foot-sync)
    int         CurState = -1;     // active state machine state (-1 = uninitialised → DefaultState)
    float       StateTime = 0.0f;  // time spent in the current state
    int         PrevState = -1;    // state we are cross-fading FROM (-1 = none)
    float       BlendElapsed = 0.0f; // elapsed cross-fade time
    float       BlendDuration = 0.0f; // total cross-fade time of the active transition
    std::vector<Transform> BlendFromPose;   // pose snapshot we cross-fade FROM on a state transition
    std::array<Matrix4x4, MAX_BONES> BonePalette;
    std::array<Matrix4x4, MAX_BONES> PrevBonePalette;   // last frame's palette (skeletal motion vectors)
    bool        PaletteReady = false;

    // ── Bone sockets (UE5-style attach-to-bone). Filled each frame at pose finalize so a child actor
    // can attach to a named bone via TransformComponent::AttachSocket. Runtime only (not serialized). ──
    std::array<Matrix4x4, MAX_BONES> BoneModelGlobals;          // mesh-space posed bone globals (pre inverse-bind)
    std::unordered_map<std::string, uint32_t> BoneNameToIndex;  // copied once from the skeleton's JointMap
    bool        SocketsReady = false;

    // Mesh-space transform of a named bone (socket): true iff the pose is built and the bone exists.
    bool TryGetSocketModelTransform(const std::string& bone, Matrix4x4& out) const {
        if (!SocketsReady) return false;
        auto it = BoneNameToIndex.find(bone);
        if (it == BoneNameToIndex.end() || it->second >= MAX_BONES) return false;
        out = BoneModelGlobals[it->second];
        return true;
    }

    AnimationComponent() {
        BonePalette.fill(Matrix4x4::Identity);
        PrevBonePalette.fill(Matrix4x4::Identity);
        BoneModelGlobals.fill(Matrix4x4::Identity);
    }

    const char* GetTypeName() const override { return "AnimationComponent"; }
    void ToJson(nlohmann::json& j)        const override;
    void FromJson(const nlohmann::json& j)      override;
};

} // namespace Fujin
