#pragma once
#include "Component.h"

namespace Fujin {

class CameraComponent : public Component {
public:
    float FOV      = 60.0f;
    float NearClip = 0.1f;
    float FarClip  = 1000.0f;
    bool  IsActive = true;

    const char* GetTypeName() const override { return "CameraComponent"; }
    void ToJson(nlohmann::json& j)        const override;
    void FromJson(const nlohmann::json& j)      override;
};

} // namespace Fujin
