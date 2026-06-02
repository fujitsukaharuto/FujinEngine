#include "LightComponent.h"

namespace Fujin {

static const char* LightTypeToStr(LightType t) {
    switch (t) {
        case LightType::Directional: return "Directional";
        case LightType::Spot:        return "Spot";
        default:                     return "Point";
    }
}

static LightType StrToLightType(const std::string& s) {
    if (s == "Directional") return LightType::Directional;
    if (s == "Spot")        return LightType::Spot;
    return LightType::Point;
}

static const bool s_lightRegistered = []() {
    ComponentRegistry::Get().Register("LightComponent", []() {
        return std::make_unique<LightComponent>();
    });
    return true;
}();

void LightComponent::ToJson(nlohmann::json& j) const {
    j["lightType"] = LightTypeToStr(Type);
    j["color"]     = { Color.x, Color.y, Color.z };
    j["intensity"] = Intensity;
    j["range"]     = Range;
    j["spotAngle"] = SpotAngle;
    j["castShadows"] = CastShadows;
}

void LightComponent::FromJson(const nlohmann::json& j) {
    Type      = StrToLightType(j.value("lightType", "Point"));
    if (j.contains("color")) {
        auto& c = j["color"];
        Color = { c[0].get<float>(), c[1].get<float>(), c[2].get<float>() };
    }
    Intensity = j.value("intensity", 1.0f);
    Range     = j.value("range",     10.0f);
    SpotAngle = j.value("spotAngle", 45.0f);
    CastShadows = j.value("castShadows", false);
}

} // namespace Fujin
