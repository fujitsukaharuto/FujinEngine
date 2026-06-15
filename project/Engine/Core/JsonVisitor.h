#pragma once
#include "PropertyVisitor.h"
#include "Engine/Math/Math.h"
#include <json.hpp>
#include <string>

namespace Fujin {

// IPropertyVisitor implementations that serialize a component's reflected properties to/from JSON,
// keyed by each property's stable `key`. Together with Component's default ToJson/FromJson (which
// route through these), Reflect() becomes the single source of truth: a component that declares its
// fields in Reflect() needs no bespoke ToJson/FromJson. Vector3 is stored as a 3-element array
// [x,y,z], matching the existing convention used by the hand-written serializers (e.g. Collider).

// Writes each reflected property into `j` under its key.
struct JsonWriteVisitor : IPropertyVisitor {
    nlohmann::json& j;
    explicit JsonWriteVisitor(nlohmann::json& json) : j(json) {}

    void Float(const char* key, const char*, float* v, float, float, float) override { j[key] = *v; }
    void Int  (const char* key, const char*, int* v, int, int)              override { j[key] = *v; }
    void Bool (const char* key, const char*, bool* v)                       override { j[key] = *v; }
    void Vec3 (const char* key, const char*, Vector3* v, float)             override { j[key] = { v->x, v->y, v->z }; }
    void Color(const char* key, const char*, Vector3* v)                    override { j[key] = { v->x, v->y, v->z }; }
    void Text (const char* key, const char*, std::string* v)                override { j[key] = *v; }
    void Enum (const char* key, const char*, int* v, const char* const*, int) override { j[key] = *v; }
    void EnumArray(const char* key, const char*, int* base, int n,
                   const char* const*, const char* const*, int) override {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < n; ++i) arr.push_back(base[i]);
        j[key] = std::move(arr);
    }
};

// Reads each reflected property from `j` by its key. Missing keys leave the field at its current
// value (the constructor default), matching the j.value(key, current) semantics of the old code.
struct JsonReadVisitor : IPropertyVisitor {
    const nlohmann::json& j;
    explicit JsonReadVisitor(const nlohmann::json& json) : j(json) {}

    void Float(const char* key, const char*, float* v, float, float, float) override { *v = j.value(key, *v); }
    void Int  (const char* key, const char*, int* v, int, int)              override { *v = j.value(key, *v); }
    void Bool (const char* key, const char*, bool* v)                       override { *v = j.value(key, *v); }
    void Text (const char* key, const char*, std::string* v)                override { *v = j.value(key, *v); }
    void Vec3 (const char* key, const char*, Vector3* v, float)             override { ReadVec(key, v); }
    void Color(const char* key, const char*, Vector3* v)                    override { ReadVec(key, v); }
    void Enum (const char* key, const char*, int* v, const char* const*, int) override { *v = j.value(key, *v); }
    void EnumArray(const char* key, const char*, int* base, int n,
                   const char* const*, const char* const*, int) override {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array()) return;
        for (int i = 0; i < n && i < static_cast<int>(it->size()); ++i)
            base[i] = (*it)[i].get<int>();
    }

private:
    void ReadVec(const char* key, Vector3* v) {
        auto it = j.find(key);
        if (it != j.end() && it->is_array() && it->size() == 3) {
            v->x = (*it)[0].get<float>();
            v->y = (*it)[1].get<float>();
            v->z = (*it)[2].get<float>();
        }
    }
};

} // namespace Fujin
