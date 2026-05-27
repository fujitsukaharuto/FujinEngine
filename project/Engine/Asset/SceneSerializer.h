#pragma once
#include <string>

namespace Fujin {

class SceneManager;

class SceneSerializer {
public:
    static bool Save(const SceneManager& scene, const std::string& filePath);
    static bool Load(SceneManager& scene, const std::string& filePath);
};

} // namespace Fujin
