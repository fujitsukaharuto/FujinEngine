#pragma once
#include "Vector.h"
#include "Quaternion.h"
#include "Matrix.h"

namespace Fujin {

// A decomposed Translation/Rotation/Scale transform (UE5 FTransform semantics).
//   • Composition and inverse-point operations never introduce shear, unlike raw matrices.
//   • Like FTransform, a non-uniform scale combined with rotation in the parent chain is an
//     approximation (TRS cannot represent shear); this matches Unreal's behaviour.
struct Transform {
    Vector3    Position = Vector3(0.0f, 0.0f, 0.0f);
    Quaternion Rotation;                              // identity by default
    Vector3    Scale    = Vector3(1.0f, 1.0f, 1.0f);

    Transform() = default;
    Transform(const Vector3& p, const Quaternion& r, const Vector3& s)
        : Position(p), Rotation(r), Scale(s) {}

    // --- forward transforms (local → world) ---
    Vector3 TransformPoint(const Vector3& p) const {
        return Position + Rotation * (Scale * p);     // Scale * p is component-wise
    }
    Vector3 TransformVector(const Vector3& v) const { // rotation + scale, no translation
        return Rotation * (Scale * v);
    }
    Vector3 TransformDirection(const Vector3& v) const { // rotation only (unit-preserving)
        return Rotation * v;
    }

    // --- inverse transforms (world → local) ---
    Vector3 InverseTransformPoint(const Vector3& world) const {
        Vector3 r = Rotation.Inverse() * (world - Position);
        return Vector3(r.x / Scale.x, r.y / Scale.y, r.z / Scale.z);
    }
    Quaternion InverseTransformRotation(const Quaternion& world) const {
        return Rotation.Inverse() * world;
    }

    // Compose so that  world = parentWorld * local.
    Transform operator*(const Transform& child) const {
        Transform out;
        out.Scale    = Scale * child.Scale;                       // component-wise
        out.Rotation = Rotation * child.Rotation;
        out.Position = Position + Rotation * (Scale * child.Position);
        return out;
    }

    Matrix4x4 ToMatrix() const {
        return Matrix4x4::Translation(Position) * Rotation.ToMatrix() * Matrix4x4::Scale(Scale);
    }
};

} // namespace Fujin
