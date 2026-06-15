#pragma once
#include <string>

namespace Fujin {

struct Vector3;

// A component exposes its editable fields by overriding Component::Reflect(IPropertyVisitor&) and
// calling one method per field. The editor supplies an ImGui-backed visitor that renders each as
// the matching widget, so a new component needs no per-type code in InspectorPanel — the UE5
// "declare properties, the Details panel draws them" model. min>=max means "unbounded".
//
// Each property carries TWO names: a stable `key` (used verbatim as the JSON serialization key, so
// renaming the display text never breaks save files) and a human `label` (shown in the Inspector).
// Because Reflect() now declares both, it is the single source of truth that drives BOTH the editor
// AND save/load: JsonWriteVisitor/JsonReadVisitor (JsonVisitor.h) walk the same declarations, and
// Component's default ToJson/FromJson route through them — no per-component ToJson/FromJson needed.
class IPropertyVisitor {
public:
    virtual ~IPropertyVisitor() = default;
    virtual void Float(const char* key, const char* label, float* v, float speed = 0.01f, float min = 0.0f, float max = 0.0f) = 0;
    virtual void Int  (const char* key, const char* label, int* v,   int min = 0, int max = 0) = 0;
    virtual void Bool (const char* key, const char* label, bool* v) = 0;
    virtual void Vec3 (const char* key, const char* label, Vector3* v, float speed = 0.01f) = 0;
    virtual void Color(const char* key, const char* label, Vector3* rgb) = 0;            // RGB color picker
    virtual void Text (const char* key, const char* label, std::string* v) = 0;
};

} // namespace Fujin
