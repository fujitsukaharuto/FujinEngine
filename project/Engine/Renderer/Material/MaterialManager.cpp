#include "MaterialManager.h"
#include <d3d12.h>                         // Windows types before dxcapi.h
#include "Engine/Graphics/ShaderReflection.h"
#include "Engine/Graphics/DxcHelper.h"
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace Fujin {

// ─── InitLayout ──────────────────────────────────────────────────────────────

void MaterialManager::InitLayout() {
    try {
        auto blob = LoadOrCompileShader(L"Resource/Shaders/GBufferPass.PS.hlsl", L"ps_6_0");
        m_layout  = ReflectShaderCB(blob.Get());
    } catch (...) {
        m_layout = GBufferPassFallbackLayout();
    }

    // Prime the default material with layout-sized ParamData.
    m_default.Name     = "Default";
    m_default.FilePath = "";
    m_default.ParamData.assign(m_layout.MaterialSize, 0);
    ApplyDefaults(m_default);
}

// ─── ApplyDefaults ───────────────────────────────────────────────────────────

void MaterialManager::ApplyDefaults(Material& mat) const {
    if (mat.ParamData.size() < m_layout.MaterialSize)
        mat.ParamData.assign(m_layout.MaterialSize, 0);

    for (const auto& param : m_layout.Params) {
        if (param.ByteOffset + param.Cols * 4 > mat.ParamData.size()) continue;
        float* ptr = reinterpret_cast<float*>(mat.ParamData.data() + param.ByteOffset);

        switch (param.Widget) {
        case ParamWidget::Color3:
        case ParamWidget::Color4:
            for (uint32_t c = 0; c < param.Cols; ++c) ptr[c] = 1.0f;
            break;
        case ParamWidget::Slider01: {
            std::string lower = param.Name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (lower == "roughness") ptr[0] = 0.5f;
            else if (lower == "ao")   ptr[0] = 1.0f;
            else                      ptr[0] = 0.0f;
            break;
        }
        default:
            for (uint32_t c = 0; c < param.Cols; ++c) ptr[c] = 0.0f;
            break;
        }
    }
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────

void MaterialManager::ReadFromJson(Material& mat, const std::string& text) const {
    try {
        nlohmann::json j = nlohmann::json::parse(text);

        mat.Name              = j.value("name", mat.Name);
        mat.AlbedoTexturePath = j.value("albedoTexturePath", "");
        mat.NormalTexturePath = j.value("normalTexturePath", "");
        mat.OrmTexturePath    = j.value("ormTexturePath", "");

        if (j.contains("params") && j["params"].is_object()) {
            const auto& params = j["params"];
            for (const auto& param : m_layout.Params) {
                if (!params.contains(param.Name)) continue;
                if (param.ByteOffset + param.Cols * 4 > mat.ParamData.size()) continue;
                float* ptr = reinterpret_cast<float*>(mat.ParamData.data() + param.ByteOffset);
                if (param.Cols == 1) {
                    ptr[0] = params[param.Name].get<float>();
                } else {
                    auto arr = params[param.Name].get<std::vector<float>>();
                    for (uint32_t c = 0; c < param.Cols && c < arr.size(); ++c)
                        ptr[c] = arr[c];
                }
            }
        }
    } catch (...) {}
}

std::string MaterialManager::WriteToJson(const Material& mat) const {
    nlohmann::json j;
    j["name"]              = mat.Name;
    j["albedoTexturePath"] = mat.AlbedoTexturePath;
    j["normalTexturePath"] = mat.NormalTexturePath;
    j["ormTexturePath"]    = mat.OrmTexturePath;

    nlohmann::json params;
    for (const auto& param : m_layout.Params) {
        if (param.ByteOffset + param.Cols * 4 > mat.ParamData.size()) continue;
        const float* ptr = reinterpret_cast<const float*>(mat.ParamData.data() + param.ByteOffset);
        if (param.Cols == 1) {
            params[param.Name] = ptr[0];
        } else {
            params[param.Name] = std::vector<float>(ptr, ptr + param.Cols);
        }
    }
    j["params"] = params;
    return j.dump(2);
}

// ─── Public API ───────────────────────────────────────────────────────────────

Material* MaterialManager::LoadOrCreate(const std::string& path) {
    if (path.empty()) return nullptr;

    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second.get();

    auto mat      = std::make_unique<Material>();
    mat->FilePath = path;
    mat->ParamData.assign(m_layout.MaterialSize, 0);
    ApplyDefaults(*mat);

    // Try to load from disk.
    std::ifstream f(path);
    if (f.is_open()) {
        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        ReadFromJson(*mat, text);
        mat->FilePath = path; // preserve after JSON parse
    } else {
        // Derive default name from filename stem.
        mat->Name = std::filesystem::path(path).stem().string();
    }

    Material* ptr = mat.get();
    m_cache[path] = std::move(mat);
    return ptr;
}

Material* MaterialManager::Get(const std::string& path) const {
    auto it = m_cache.find(path);
    return (it != m_cache.end()) ? it->second.get() : nullptr;
}

bool MaterialManager::Save(const Material& mat) const {
    if (mat.FilePath.empty()) return false;
    try {
        std::filesystem::create_directories(
            std::filesystem::path(mat.FilePath).parent_path());
        std::ofstream f(mat.FilePath);
        if (!f.is_open()) return false;
        f << WriteToJson(mat);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Fujin
