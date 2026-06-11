#pragma once
// Data-driven animation graph: a 1D blend space and a state machine over clips/blend spaces.
// These are pure config (no runtime/GPU state) — gameplay sets named float Params on the
// AnimationComponent and the evaluator (SceneRenderer::UpdateAnimations) advances/blends. This is
// the engine's analog of a UE5 AnimBlueprint's state machine + blend spaces, authored in C++/JSON.
#include <string>
#include <vector>

namespace Fujin {

// One clip placed on a 1D blend space at the parameter value where it plays at full weight.
struct BlendSample {
    std::string ClipName;
    float       Threshold = 0.0f;   // param value at which this clip dominates (e.g. Speed)
};

// Blend between clips along a single parameter (e.g. "Speed": Idle@0 / Walk@2 / Run@6). Samples are
// kept sorted by Threshold; the two bracketing the current param value are phase-synced and blended.
struct BlendSpace1D {
    std::string              Param = "Speed";
    std::vector<BlendSample> Samples;
};

// A state plays either a single looping clip or a blend space.
enum class AnimNodeType { Clip, BlendSpace };

struct AnimState {
    std::string  Name;
    AnimNodeType Type     = AnimNodeType::Clip;
    std::string  ClipName;             // when Type==Clip
    BlendSpace1D Blend;                // when Type==BlendSpace
    bool         Loop     = true;
    float        Speed    = 1.0f;      // playback rate multiplier
};

enum class CompareOp { Greater, Less, GreaterEqual, LessEqual };

// Fires when its parameter satisfies the comparison; From<0 means "any state" (global transition).
struct AnimTransition {
    int         From      = -1;        // source state index, -1 = AnyState
    int         To        = 0;         // destination state index
    std::string Param;                 // parameter compared (empty = always true → unconditional)
    CompareOp   Op        = CompareOp::Greater;
    float       Value     = 0.0f;
    float       BlendTime = 0.2f;      // cross-fade duration in seconds
};

struct AnimStateMachine {
    std::vector<AnimState>      States;
    std::vector<AnimTransition> Transitions;
    int                         DefaultState = 0;
};

} // namespace Fujin
