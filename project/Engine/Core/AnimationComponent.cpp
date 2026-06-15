#include "AnimationComponent.h"

namespace Fujin {

static const bool s_animRegistered = []() {
    ComponentRegistry::Get().Register("AnimationComponent", []() {
        return std::make_unique<AnimationComponent>();
    });
    return true;
}();

// ── Blend space / state machine (de)serialization ──────────────────────────────
// These configs are pure data (no runtime/GPU state), so they round-trip directly. This lets the
// scene asset fully capture an actor's animation graph (e.g. the Fox's Speed-driven blend space)
// instead of it living only in SetupTestScene code.
static void BlendSpaceToJson(const BlendSpace1D& bs, nlohmann::json& j) {
    j["param"]   = bs.Param;
    j["samples"] = nlohmann::json::array();
    for (auto& s : bs.Samples)
        j["samples"].push_back({ { "clip", s.ClipName }, { "threshold", s.Threshold } });
}

static void BlendSpaceFromJson(BlendSpace1D& bs, const nlohmann::json& j) {
    bs.Param = j.value("param", bs.Param);
    bs.Samples.clear();
    if (j.contains("samples") && j["samples"].is_array()) {
        for (auto& sj : j["samples"]) {
            BlendSample s;
            s.ClipName  = sj.value("clip",      std::string());
            s.Threshold = sj.value("threshold", 0.0f);
            bs.Samples.push_back(std::move(s));
        }
    }
}

static void StateMachineToJson(const AnimStateMachine& sm, nlohmann::json& j) {
    j["defaultState"] = sm.DefaultState;
    j["states"]       = nlohmann::json::array();
    for (auto& st : sm.States) {
        nlohmann::json sj;
        sj["name"]     = st.Name;
        sj["type"]     = (int)st.Type;
        sj["clipName"] = st.ClipName;
        sj["loop"]     = st.Loop;
        sj["speed"]    = st.Speed;
        BlendSpaceToJson(st.Blend, sj["blend"]);
        j["states"].push_back(std::move(sj));
    }
    j["transitions"] = nlohmann::json::array();
    for (auto& tr : sm.Transitions) {
        j["transitions"].push_back({
            { "from", tr.From }, { "to", tr.To }, { "param", tr.Param },
            { "op", (int)tr.Op }, { "value", tr.Value }, { "blendTime", tr.BlendTime },
        });
    }
}

static void StateMachineFromJson(AnimStateMachine& sm, const nlohmann::json& j) {
    sm.DefaultState = j.value("defaultState", 0);
    sm.States.clear();
    if (j.contains("states") && j["states"].is_array()) {
        for (auto& sj : j["states"]) {
            AnimState st;
            st.Name     = sj.value("name",     std::string());
            st.Type     = (AnimNodeType)sj.value("type", 0);
            st.ClipName = sj.value("clipName", std::string());
            st.Loop     = sj.value("loop",     true);
            st.Speed    = sj.value("speed",    1.0f);
            if (sj.contains("blend")) BlendSpaceFromJson(st.Blend, sj["blend"]);
            sm.States.push_back(std::move(st));
        }
    }
    sm.Transitions.clear();
    if (j.contains("transitions") && j["transitions"].is_array()) {
        for (auto& tj : j["transitions"]) {
            AnimTransition tr;
            tr.From      = tj.value("from",      -1);
            tr.To        = tj.value("to",        0);
            tr.Param     = tj.value("param",     std::string());
            tr.Op        = (CompareOp)tj.value("op", 0);
            tr.Value     = tj.value("value",     0.0f);
            tr.BlendTime = tj.value("blendTime", 0.2f);
            sm.Transitions.push_back(std::move(tr));
        }
    }
}

void AnimationComponent::ToJson(nlohmann::json& j) const {
    j["clipName"]   = ClipName;
    j["speed"]      = Speed;
    j["timeOffset"] = TimeOffset;
    j["loop"]       = Loop;
    j["playing"]    = Playing;

    j["useBlendSpace"] = UseBlendSpace;
    if (UseBlendSpace) BlendSpaceToJson(Blend, j["blend"]);
    j["useStateMachine"] = UseStateMachine;
    if (UseStateMachine) StateMachineToJson(StateMachine, j["stateMachine"]);
}

void AnimationComponent::FromJson(const nlohmann::json& j) {
    ClipName   = j.value("clipName",   "");
    Speed      = j.value("speed",      1.0f);
    TimeOffset = j.value("timeOffset", 0.0f);
    Loop       = j.value("loop",       true);
    Playing    = j.value("playing",    true);

    UseBlendSpace = j.value("useBlendSpace", false);
    if (j.contains("blend")) BlendSpaceFromJson(Blend, j["blend"]);
    UseStateMachine = j.value("useStateMachine", false);
    if (j.contains("stateMachine")) StateMachineFromJson(StateMachine, j["stateMachine"]);
}

} // namespace Fujin
