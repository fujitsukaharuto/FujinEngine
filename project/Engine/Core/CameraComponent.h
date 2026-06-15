#pragma once
#include "Component.h"
#include "PropertyVisitor.h"

namespace Fujin {

class CameraComponent : public Component {
public:
    float FOV      = 60.0f;
    float NearClip = 0.1f;
    float FarClip  = 1000.0f;
    bool  IsActive = true;

    const char* GetTypeName() const override { return "CameraComponent"; }
    // Reflect drives Inspector + save/load (Component's default ToJson/FromJson). Keys verbatim from
    // the old hand-written serializer (fov/nearClip/farClip/isActive) so existing scenes still load.
    void Reflect(IPropertyVisitor& v) override {
        v.Float("fov",      "FOV",       &FOV,      0.5f,   1.0f, 179.0f);
        v.Float("nearClip", "Near Clip", &NearClip, 0.001f, 0.001f, 10.0f);
        v.Float("farClip",  "Far Clip",  &FarClip,  1.0f,   1.0f, 10000.0f);
        v.Bool ("isActive", "Active",    &IsActive);
    }
};

} // namespace Fujin
