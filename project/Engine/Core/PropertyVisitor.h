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

    // Enum stored as an int index, edited via a dropdown of `names` (count entries). Because component
    // enums vary in underlying type (plain enum / enum class : uint8_t), Reflect() bridges through a
    // local int: read the enum into it, call Enum(&tmp,...), then write tmp back to the field.
    virtual void Enum(const char* key, const char* label, int* v, const char* const* names, int count) = 0;

    // Fixed-size array of enum indices (e.g. a per-channel collision response matrix). `base` points at
    // elemCount contiguous ints; `elemLabels` names each row, `optionNames`/optionCount the dropdown.
    // Reflect() bridges a uint8_t/enum array through a local int[] the same way Enum does.
    virtual void EnumArray(const char* key, const char* label, int* base, int elemCount,
                           const char* const* elemLabels,
                           const char* const* optionNames, int optionCount) = 0;
};

} // namespace Fujin
