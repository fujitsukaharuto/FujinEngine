#include "PhysicsWorld.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/RigidbodyComponent.h"
#include "Engine/Core/ColliderComponent.h"
#include <algorithm>
#include <cmath>

namespace Fujin {

// ── Geometry helpers ──────────────────────────────────────────────────────────

static Vector3 ClosestPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b) {
    Vector3 ab = b - a;
    float lenSq = Vector3::Dot(ab, ab);
    if (lenSq < 1e-12f) return a;
    float t = std::clamp(Vector3::Dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return a + ab * t;
}

static Vector3 ClosestPointOnAABB(const Vector3& p, const Vector3& mn, const Vector3& mx) {
    return Vector3(
        std::clamp(p.x, mn.x, mx.x),
        std::clamp(p.y, mn.y, mx.y),
        std::clamp(p.z, mn.z, mx.z));
}

// Closest points between two segments; returns squared distance.
static float ClosestSegmentSegment(
    const Vector3& p0, const Vector3& p1,
    const Vector3& q0, const Vector3& q1,
    Vector3& closestP, Vector3& closestQ)
{
    Vector3 d1 = p1 - p0;
    Vector3 d2 = q1 - q0;
    Vector3 r  = p0 - q0;
    float a  = Vector3::Dot(d1, d1);
    float e  = Vector3::Dot(d2, d2);
    float f  = Vector3::Dot(d2, r);
    float s, t;

    if (a < 1e-12f && e < 1e-12f) { s = t = 0.0f; }
    else if (a < 1e-12f)          { s = 0.0f; t = std::clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vector3::Dot(d1, r);
        if (e < 1e-12f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b     = Vector3::Dot(d1, d2);
            float denom = a * e - b * b;
            s = (denom > 1e-12f) ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    closestP = p0 + d1 * s;
    closestQ = q0 + d2 * t;
    Vector3 diff = closestP - closestQ;
    return Vector3::Dot(diff, diff);
}

// ── World-space capsule endpoints ─────────────────────────────────────────────

static void CapsuleEndpoints(const TransformComponent* tc, const ColliderComponent* col,
                              Vector3& base, Vector3& tip)
{
    // Local Y axis in world space (second column of rotation matrix)
    Matrix4x4 rot = tc->Rotation.ToMatrix();
    Vector3 up(rot.m[0][1], rot.m[1][1], rot.m[2][1]);
    Vector3 center = tc->Position + col->Offset;
    base = center - up * col->HalfHeight;
    tip  = center + up * col->HalfHeight;
}

// ── Primitive collision tests ─────────────────────────────────────────────────
// Convention: normal points from B toward A (separating direction for A).

bool PhysicsWorld::SphereSphere(
    const Vector3& posA, float rA,
    const Vector3& posB, float rB,
    Contact& c)
{
    Vector3 d = posA - posB;
    float dist2 = Vector3::Dot(d, d);
    float rSum  = rA + rB;
    if (dist2 >= rSum * rSum) return false;

    float dist = std::sqrt(dist2);
    c.normal      = (dist > 1e-6f) ? d / dist : Vector3(0.0f, 1.0f, 0.0f);
    c.penetration = rSum - dist;
    c.point       = posB + c.normal * rB;
    return true;
}

bool PhysicsWorld::SphereAABB(
    const Vector3& spherePos, float r,
    const Vector3& boxCenter, const Vector3& halfExt,
    Contact& c)
{
    Vector3 mn = boxCenter - halfExt;
    Vector3 mx = boxCenter + halfExt;
    Vector3 closest = ClosestPointOnAABB(spherePos, mn, mx);
    Vector3 d = spherePos - closest;
    float dist2 = Vector3::Dot(d, d);
    if (dist2 >= r * r) return false;

    if (dist2 < 1e-12f) {
        // Sphere center inside AABB — push out along minimum axis
        Vector3 dp = spherePos - boxCenter;
        float ox = halfExt.x - std::fabsf(dp.x);
        float oy = halfExt.y - std::fabsf(dp.y);
        float oz = halfExt.z - std::fabsf(dp.z);
        if (ox < oy && ox < oz)      c.normal = Vector3(dp.x > 0 ? 1.0f : -1.0f, 0, 0);
        else if (oy < oz)            c.normal = Vector3(0, dp.y > 0 ? 1.0f : -1.0f, 0);
        else                         c.normal = Vector3(0, 0, dp.z > 0 ? 1.0f : -1.0f);
        c.penetration = r + std::min({ ox, oy, oz });
    } else {
        float dist = std::sqrt(dist2);
        c.normal      = d / dist;
        c.penetration = r - dist;
    }
    c.point = closest;
    return true;
}

bool PhysicsWorld::SphereCapsule(
    const Vector3& spherePos, float sphereR,
    const Vector3& capA, const Vector3& capB, float capR,
    Contact& c)
{
    Vector3 closest = ClosestPointOnSegment(spherePos, capA, capB);
    Vector3 d = spherePos - closest;
    float dist2 = Vector3::Dot(d, d);
    float rSum  = sphereR + capR;
    if (dist2 >= rSum * rSum) return false;

    float dist    = std::sqrt(dist2);
    c.normal      = (dist > 1e-6f) ? d / dist : Vector3(0.0f, 1.0f, 0.0f);
    c.penetration = rSum - dist;
    c.point       = closest + c.normal * capR;
    return true;
}

bool PhysicsWorld::AABBAABB(
    const Vector3& centerA, const Vector3& halfA,
    const Vector3& centerB, const Vector3& halfB,
    Contact& c)
{
    Vector3 d = centerA - centerB;
    float ox = (halfA.x + halfB.x) - std::fabsf(d.x);
    float oy = (halfA.y + halfB.y) - std::fabsf(d.y);
    float oz = (halfA.z + halfB.z) - std::fabsf(d.z);
    if (ox < 0 || oy < 0 || oz < 0) return false;

    if (ox < oy && ox < oz) {
        c.normal      = Vector3(d.x > 0 ? 1.0f : -1.0f, 0, 0);
        c.penetration = ox;
    } else if (oy < oz) {
        c.normal      = Vector3(0, d.y > 0 ? 1.0f : -1.0f, 0);
        c.penetration = oy;
    } else {
        c.normal      = Vector3(0, 0, d.z > 0 ? 1.0f : -1.0f);
        c.penetration = oz;
    }
    c.point = centerB + c.normal * (-1.0f) * (halfB - Vector3(ox * 0.5f, oy * 0.5f, oz * 0.5f));
    return true;
}

bool PhysicsWorld::AABBCapsule(
    const Vector3& boxCenter, const Vector3& halfExt,
    const Vector3& capA, const Vector3& capB, float capR,
    Contact& c)
{
    // Find closest point on capsule segment to AABB center, clamp to segment.
    Vector3 capClosest = ClosestPointOnSegment(boxCenter, capA, capB);
    // Then find closest point on AABB to that segment point → sphere-AABB test.
    // A = AABB (boxCenter), B = Capsule (represented as sphere at capClosest)
    // But normal convention: A = AABB, B = capsule ↔ normal from capsule toward AABB
    // so we call SphereAABB with sphere=capClosest, r=capR, then negate normal.
    Contact tmp;
    if (!SphereAABB(capClosest, capR, boxCenter, halfExt, tmp)) return false;
    c.normal      = -tmp.normal; // flip: original was sphere→AABB, we want AABB→capsule
    c.penetration = tmp.penetration;
    c.point       = tmp.point;
    return true;
}

bool PhysicsWorld::CapsuleCapsule(
    const Vector3& baseA, const Vector3& tipA, float rA,
    const Vector3& baseB, const Vector3& tipB, float rB,
    Contact& c)
{
    Vector3 closestA, closestB;
    float dist2 = ClosestSegmentSegment(baseA, tipA, baseB, tipB, closestA, closestB);
    float rSum  = rA + rB;
    if (dist2 >= rSum * rSum) return false;

    float dist    = std::sqrt(dist2);
    Vector3 d     = closestA - closestB;
    c.normal      = (dist > 1e-6f) ? d / dist : Vector3(0.0f, 1.0f, 0.0f);
    c.penetration = rSum - dist;
    c.point       = closestB + c.normal * rB;
    return true;
}

// ── Pair dispatch ─────────────────────────────────────────────────────────────

bool PhysicsWorld::TestPair(Actor* a, Actor* b, Contact& out) {
    auto* tcA  = a->GetComponent<TransformComponent>();
    auto* tcB  = b->GetComponent<TransformComponent>();
    auto* colA = a->GetComponent<ColliderComponent>();
    auto* colB = b->GetComponent<ColliderComponent>();
    if (!tcA || !tcB || !colA || !colB) return false;

    out.actorA = a;
    out.actorB = b;
    out.tcA    = tcA;
    out.tcB    = tcB;
    out.rbA    = a->GetComponent<RigidbodyComponent>();
    out.rbB    = b->GetComponent<RigidbodyComponent>();
    out.isTrigger = colA->IsTrigger || colB->IsTrigger;

    auto shA = colA->Shape;
    auto shB = colB->Shape;

    // World positions
    Vector3 posA = tcA->Position + colA->Offset;
    Vector3 posB = tcB->Position + colB->Offset;

    if (shA == ColliderShape::Sphere && shB == ColliderShape::Sphere) {
        return SphereSphere(posA, colA->Radius, posB, colB->Radius, out);
    }
    if (shA == ColliderShape::AABB && shB == ColliderShape::AABB) {
        return AABBAABB(posA, colA->HalfExtents, posB, colB->HalfExtents, out);
    }
    // Sphere vs AABB
    if (shA == ColliderShape::Sphere && shB == ColliderShape::AABB) {
        return SphereAABB(posA, colA->Radius, posB, colB->HalfExtents, out);
    }
    if (shA == ColliderShape::AABB && shB == ColliderShape::Sphere) {
        Contact tmp;
        if (!SphereAABB(posB, colB->Radius, posA, colA->HalfExtents, tmp)) return false;
        out.normal      = -tmp.normal;
        out.penetration = tmp.penetration;
        out.point       = tmp.point;
        return true;
    }
    // Capsule vs Capsule
    if (shA == ColliderShape::Capsule && shB == ColliderShape::Capsule) {
        Vector3 baseA, tipA, baseB, tipB;
        CapsuleEndpoints(tcA, colA, baseA, tipA);
        CapsuleEndpoints(tcB, colB, baseB, tipB);
        return CapsuleCapsule(baseA, tipA, colA->Radius, baseB, tipB, colB->Radius, out);
    }
    // Sphere vs Capsule
    if (shA == ColliderShape::Sphere && shB == ColliderShape::Capsule) {
        Vector3 baseB, tipB;
        CapsuleEndpoints(tcB, colB, baseB, tipB);
        return SphereCapsule(posA, colA->Radius, baseB, tipB, colB->Radius, out);
    }
    if (shA == ColliderShape::Capsule && shB == ColliderShape::Sphere) {
        Vector3 baseA, tipA;
        CapsuleEndpoints(tcA, colA, baseA, tipA);
        Contact tmp;
        if (!SphereCapsule(posB, colB->Radius, baseA, tipA, colA->Radius, tmp)) return false;
        out.normal      = -tmp.normal;
        out.penetration = tmp.penetration;
        out.point       = tmp.point;
        return true;
    }
    // AABB vs Capsule
    if (shA == ColliderShape::AABB && shB == ColliderShape::Capsule) {
        Vector3 baseB, tipB;
        CapsuleEndpoints(tcB, colB, baseB, tipB);
        return AABBCapsule(posA, colA->HalfExtents, baseB, tipB, colB->Radius, out);
    }
    if (shA == ColliderShape::Capsule && shB == ColliderShape::AABB) {
        Vector3 baseA, tipA;
        CapsuleEndpoints(tcA, colA, baseA, tipA);
        Contact tmp;
        if (!AABBCapsule(posB, colB->HalfExtents, baseA, tipA, colA->Radius, tmp)) return false;
        out.normal      = -tmp.normal;
        out.penetration = tmp.penetration;
        out.point       = tmp.point;
        return true;
    }
    return false;
}

// ── Collision response ────────────────────────────────────────────────────────

void PhysicsWorld::ResolveContact(Contact& c) {
    if (c.isTrigger) return;

    float invMassA = (c.rbA && !c.rbA->IsKinematic && c.rbA->Mass > 0) ? 1.0f / c.rbA->Mass : 0.0f;
    float invMassB = (c.rbB && !c.rbB->IsKinematic && c.rbB->Mass > 0) ? 1.0f / c.rbB->Mass : 0.0f;
    float invMassSum = invMassA + invMassB;
    if (invMassSum < 1e-12f) return;

    Vector3 vA = c.rbA ? c.rbA->Velocity : Vector3{};
    Vector3 vB = c.rbB ? c.rbB->Velocity : Vector3{};
    float vRelN = Vector3::Dot(vA - vB, c.normal);
    if (vRelN >= 0) return; // already separating

    float e = 0.3f;
    if (c.rbA) e = (std::min)(e, c.rbA->Restitution);
    if (c.rbB) e = (std::min)(e, c.rbB->Restitution);

    float j = -(1.0f + e) * vRelN / invMassSum;

    if (c.rbA && !c.rbA->IsKinematic) c.rbA->Velocity += c.normal * (j * invMassA);
    if (c.rbB && !c.rbB->IsKinematic) c.rbB->Velocity -= c.normal * (j * invMassB);
}

void PhysicsWorld::CorrectPosition(Contact& c) {
    if (c.isTrigger) return;

    float invMassA = (c.rbA && !c.rbA->IsKinematic && c.rbA->Mass > 0) ? 1.0f / c.rbA->Mass : 0.0f;
    float invMassB = (c.rbB && !c.rbB->IsKinematic && c.rbB->Mass > 0) ? 1.0f / c.rbB->Mass : 0.0f;
    float invMassSum = invMassA + invMassB;
    if (invMassSum < 1e-12f) return;

    const float slop    = 0.005f;  // penetration tolerance
    const float percent = 0.4f;   // correction strength
    float mag = (std::max)(c.penetration - slop, 0.0f) / invMassSum * percent;
    Vector3 corr = c.normal * mag;

    if (c.tcA && c.rbA && !c.rbA->IsKinematic) c.tcA->Position += corr * invMassA;
    if (c.tcB && c.rbB && !c.rbB->IsKinematic) c.tcB->Position -= corr * invMassB;
}

// ── Main step ─────────────────────────────────────────────────────────────────

void PhysicsWorld::IntegrateVelocities(SceneManager& scene, float dt) {
    for (auto& actorPtr : scene.GetActors()) {
        auto* rb = actorPtr->GetComponent<RigidbodyComponent>();
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        if (!rb || !tc || rb->IsKinematic) continue;

        if (rb->UseGravity)
            rb->AddForce(Gravity * rb->Mass);

        float invMass = (rb->Mass > 0) ? 1.0f / rb->Mass : 0.0f;
        rb->Velocity += rb->m_forceAccum * (invMass * dt);
        rb->Velocity *= (1.0f - std::clamp(rb->LinearDamping * dt, 0.0f, 1.0f));

        tc->Position += rb->Velocity * dt;
        rb->ClearAccumulators();
    }
}

void PhysicsWorld::DetectCollisions(SceneManager& scene, std::vector<Contact>& out) {
    auto& actors = scene.GetActors();
    for (size_t i = 0; i < actors.size(); ++i) {
        if (!actors[i]->GetComponent<ColliderComponent>()) continue;
        for (size_t j = i + 1; j < actors.size(); ++j) {
            if (!actors[j]->GetComponent<ColliderComponent>()) continue;
            Contact c;
            if (TestPair(actors[i].get(), actors[j].get(), c))
                out.push_back(c);
        }
    }
}

void PhysicsWorld::Step(SceneManager& scene, float dt) {
    if (dt <= 0.0f) return;

    IntegrateVelocities(scene, dt);

    std::vector<Contact> contacts;
    DetectCollisions(scene, contacts);

    for (auto& c : contacts) ResolveContact(c);
    for (auto& c : contacts) CorrectPosition(c);
}

} // namespace Fujin
