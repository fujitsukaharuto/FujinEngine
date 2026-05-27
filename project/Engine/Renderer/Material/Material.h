#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Fujin {

// Raw material parameter data.
// Layout is described by MaterialManager::GetLayout() (CBLayout).
// ParamData.size() == CBLayout::MaterialSize bytes.
struct Material {
    std::string          Name;
    std::string          FilePath;
    std::vector<uint8_t> ParamData;
    std::string          AlbedoTexturePath;
    std::string          NormalTexturePath;
    std::string          OrmTexturePath;
};

} // namespace Fujin
