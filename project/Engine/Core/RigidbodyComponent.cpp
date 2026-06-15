#include "RigidbodyComponent.h"
#include "Component.h"

namespace Fujin {

static const bool s_rigidbodyRegistered = []() {
    ComponentRegistry::Get().Register("RigidbodyComponent", []() {
        return std::make_unique<RigidbodyComponent>();
    });
    return true;
}();

// I^{-1}_{world}(v) = R * (d ⊙ (R^T * v))
// where d = InvInertiaLocal (diagonal).
// Row-major convention: R[i][j] = m[i][j], so:
//   R  * v : result[i] = sum_j m[i][j] * v[j]
//   R^T* v : result[j] = sum_i m[i][j] * v[i]
Vector3 RigidbodyComponent::ApplyInvInertiaWorld(const Quaternion& rot, const Vector3& v) const {
    if (IsKinematic) return {};
    Matrix4x4 r = rot.ToMatrix();

    // local = R^T * v
    Vector3 local(
        r.m[0][0]*v.x + r.m[1][0]*v.y + r.m[2][0]*v.z,
        r.m[0][1]*v.x + r.m[1][1]*v.y + r.m[2][1]*v.z,
        r.m[0][2]*v.x + r.m[1][2]*v.y + r.m[2][2]*v.z);

    // scale by diagonal inv inertia
    local.x *= InvInertiaLocal.x;
    local.y *= InvInertiaLocal.y;
    local.z *= InvInertiaLocal.z;

    // world = R * local
    return Vector3(
        r.m[0][0]*local.x + r.m[0][1]*local.y + r.m[0][2]*local.z,
        r.m[1][0]*local.x + r.m[1][1]*local.y + r.m[1][2]*local.z,
        r.m[2][0]*local.x + r.m[2][1]*local.y + r.m[2][2]*local.z);
}

// ToJson/FromJson now come from Component's Reflect-driven defaults (see RigidbodyComponent::Reflect).

} // namespace Fujin
