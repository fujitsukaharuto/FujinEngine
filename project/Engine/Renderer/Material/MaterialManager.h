#pragma once
#include "Material.h"
#include "Engine/Graphics/ShaderReflectionTypes.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace Fujin {

class MaterialManager {
public:
    // Reflect the GBuffer pixel shader to establish the CBLayout.
    // Must be called once before LoadOrCreate / Save.
    void InitLayout();

    // Load from .mat.json if it exists, or build a default in-memory material.
    // Subsequent calls with the same path return the cached pointer.
    Material* LoadOrCreate(const std::string& path);

    // Return a cached material (nullptr if not loaded).
    Material* Get(const std::string& path) const;

    // Serialize mat to mat.FilePath. Returns false on failure.
    bool Save(const Material& mat) const;

    const CBLayout& GetLayout()  const { return m_layout;  }
    const Material& GetDefault() const { return m_default; }

private:
    CBLayout m_layout;
    Material m_default;
    std::unordered_map<std::string, std::unique_ptr<Material>> m_cache;

    void ApplyDefaults(Material& mat) const;
    void ReadFromJson(Material& mat, const std::string& jsonText) const;
    std::string WriteToJson(const Material& mat) const;
};

} // namespace Fujin
