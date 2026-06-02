#pragma once
#include <string>

namespace Fujin {

struct Vector3;

// A component exposes its editable fields by overriding Component::Reflect(IPropertyVisitor&) and
// calling one method per field. The editor supplies an ImGui-backed visitor that renders each as
// the matching widget, so a new component needs no per-type code in InspectorPanel — the UE5
// "declare properties, the Details panel draws them" model. min>=max means "unbounded".
class IPropertyVisitor {
public:
    virtual ~IPropertyVisitor() = default;
    virtual void Float(const char* name, float* v, float speed = 0.01f, float min = 0.0f, float max = 0.0f) = 0;
    virtual void Int  (const char* name, int* v,   int min = 0, int max = 0) = 0;
    virtual void Bool (const char* name, bool* v) = 0;
    virtual void Vec3 (const char* name, Vector3* v, float speed = 0.01f) = 0;
    virtual void Color(const char* name, Vector3* rgb) = 0;            // RGB color picker
    virtual void Text (const char* name, std::string* v) = 0;
};

} // namespace Fujin
