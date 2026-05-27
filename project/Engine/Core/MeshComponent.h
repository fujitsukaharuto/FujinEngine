#pragma once
#include "Component.h"
#include <string>

namespace Fujin {

class MeshComponent : public Component {
public:
    std::string MeshPath;
    std::string MaterialPath;
    bool DoubleSided = false;
    bool AlphaClip   = false;

    const char* GetTypeName() const override { return "MeshComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
