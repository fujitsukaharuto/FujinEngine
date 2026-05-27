#pragma once
#include "Component.h"
#include "Engine/Math/Vector.h"

namespace Fujin {

enum class LightType { Directional = 0, Point = 1, Spot = 2 };

class LightComponent : public Component {
public:
    LightType Type      = LightType::Point;
    Vector3   Color     = Vector3(1.0f, 1.0f, 1.0f);
    float     Intensity = 1.0f;
    float     Range     = 10.0f;
    float     SpotAngle = 45.0f;

    const char* GetTypeName() const override { return "LightComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
