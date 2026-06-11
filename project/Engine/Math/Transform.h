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

    // Decompose an affine TRS matrix (T*R*S, no shear — e.g. a glTF node transform) back into a
    // Transform. Column-vector convention: translation is the last column; each 3x3 column holds
    // R·S so its length is that axis' scale and, once divided out, the columns are the rotation.
    static Transform FromMatrix(const Matrix4x4& m) {
        Transform t;
        t.Position = m.GetTranslation();
        float sx = Vector3(m.m[0][0], m.m[1][0], m.m[2][0]).Length();
        float sy = Vector3(m.m[0][1], m.m[1][1], m.m[2][1]).Length();
        float sz = Vector3(m.m[0][2], m.m[1][2], m.m[2][2]).Length();
        t.Scale = Vector3(sx, sy, sz);
        float ix = sx > 1e-8f ? 1.0f / sx : 0.0f;
        float iy = sy > 1e-8f ? 1.0f / sy : 0.0f;
        float iz = sz > 1e-8f ? 1.0f / sz : 0.0f;
        Matrix4x4 rot;   // identity; fill the orthonormalised 3x3
        rot.m[0][0] = m.m[0][0]*ix; rot.m[1][0] = m.m[1][0]*ix; rot.m[2][0] = m.m[2][0]*ix;
        rot.m[0][1] = m.m[0][1]*iy; rot.m[1][1] = m.m[1][1]*iy; rot.m[2][1] = m.m[2][1]*iy;
        rot.m[0][2] = m.m[0][2]*iz; rot.m[1][2] = m.m[1][2]*iz; rot.m[2][2] = m.m[2][2]*iz;
        t.Rotation = Quaternion::FromMatrix(rot);
        return t;
    }

    // Per-component blend: lerp position/scale, slerp rotation. w=0 → a, w=1 → b. Used to blend
    // sampled animation poses (the correct space for interpolation — never lerp raw matrices).
    static Transform Blend(const Transform& a, const Transform& b, float w) {
        Transform t;
        t.Position = a.Position + (b.Position - a.Position) * w;
        t.Scale    = a.Scale    + (b.Scale    - a.Scale)    * w;
        t.Rotation = Quaternion::Slerp(a.Rotation, b.Rotation, w);
        return t;
    }
};

} // namespace Fujin
