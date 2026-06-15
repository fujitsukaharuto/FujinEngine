#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
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
    bool      CastShadows = false;   // opt-in shadow casting (spot in Stage A)

    const char* GetTypeName() const override { return "LightComponent"; }

    // Hybrid (like FootIK): Reflect drives the Inspector, but ToJson/FromJson are kept (below) because
    // lightType is serialized as a STRING ("Point"/"Spot"/"Directional"), not the int the Enum visitor
    // would write — so the bespoke serializers stay to preserve existing save files. Range/SpotAngle/
    // CastShadows are always shown here (the old draw hid them for Directional/Point).
    void Reflect(IPropertyVisitor& v) override {
        static const char* kTypeNames[] = { "Directional", "Point", "Spot" };
        int type = static_cast<int>(Type);
        v.Enum("lightType", "Type", &type, kTypeNames, 3);
        Type = static_cast<LightType>(type);

        v.Color("color",       "Color",        &Color);
        v.Float("intensity",   "Intensity",    &Intensity, 0.1f, 0.0f, 100.0f);
        v.Float("range",       "Range",        &Range,     0.5f, 0.0f, 1000.0f);
        v.Float("spotAngle",   "Spot Angle",   &SpotAngle, 0.5f, 1.0f, 179.0f);
        v.Bool ("castShadows", "Cast Shadows", &CastShadows);
    }

    // Kept intentionally (string lightType + array color) — see Reflect note above.
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
