/*
 * PhysicsWorld.cpp — clean rewrite (Unreal/Chaos-inspired).
 *
 * Per substep:
 *   ComputeInertia
 *   IntegrateVelocities      forces/gravity → velocities (positions unchanged)
 *   DetectCollisions         velocity-swept broadphase + speculative narrow-phase
 *   wake bodies w/ contact
 *   DispatchEvents           OnHit / OnBeginOverlap / OnEndOverlap
 *   PreSolve                 effective masses, tangent basis, restitution + speculative bias
 *   SolveVelocity × N        accumulated normal impulse + Coulomb friction
 *   IntegratePositions       velocities → positions
 *   CorrectPositions         positional depenetration (NGS), no energy injection
 *   UpdateSleep
 */
#include "PhysicsWorld.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/RigidbodyComponent.h"
#include "Engine/Core/ColliderComponent.h"
#include <algorithm>
#include <cmath>

namespace Fujin {

// Broadphase proxies carry the actor *id* (not a raw pointer) so a query that runs after an actor
// was destroyed resolves to nullptr and skips it, instead of dereferencing freed memory.
static void*    IdToUD(uint64_t id) { return reinterpret_cast<void*>(static_cast<uintptr_t>(id)); }
static uint64_t UDToId(void* ud)    { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ud)); }

// ============================================================================
//  Small geometry helpers
// ============================================================================

static Vector3 ClosestPtOnSegment(const Vector3& p, const Vector3& a, const Vector3& b) {
    Vector3 ab = b - a;
    float   l2 = Vector3::Dot(ab, ab);
    if (l2 < 1e-12f) return a;
    float t = std::clamp(Vector3::Dot(p - a, ab) / l2, 0.0f, 1.0f);
    return a + ab * t;
}

static Vector3 ClosestPtOnAABB(const Vector3& p, const Vector3& mn, const Vector3& mx) {
    return { std::clamp(p.x, mn.x, mx.x),
             std::clamp(p.y, mn.y, mx.y),
             std::clamp(p.z, mn.z, mx.z) };
}

// Sphere vs an AABB centred at the origin with half-extents `he` (i.e. a box's LOCAL frame).
// Returns false when separated by >= margin. On contact, nL = box→sphere direction (local),
// pen = penetration depth, clL = closest point on the box surface (local). Callers rotate the
// result back to world, which is how oriented boxes are supported without a bespoke OBB routine.
static bool SphereAabbLocal(const Vector3& spL, float r, const Vector3& he,
                            float margin, Vector3& nL, float& pen, Vector3& clL) {
    Vector3 mn(-he.x, -he.y, -he.z), mx(he.x, he.y, he.z);
    clL = ClosestPtOnAABB(spL, mn, mx);
    Vector3 d  = spL - clL;
    float   d2 = Vector3::Dot(d, d);
    if (d2 > 1e-12f) {
        float dist = std::sqrt(d2);
        float sep  = dist - r;
        if (sep >= margin) return false;
        nL  = d / dist;                     // box → sphere
        pen = -sep;
        return true;
    }
    // Sphere centre inside the box → push out through the nearest face (always overlapping).
    float ox = he.x - std::fabsf(spL.x), oy = he.y - std::fabsf(spL.y), oz = he.z - std::fabsf(spL.z);
    if      (ox <= oy && ox <= oz) { nL = Vector3(spL.x > 0 ? 1.f : -1.f, 0, 0); pen = r + ox; }
    else if (oy <= oz)             { nL = Vector3(0, spL.y > 0 ? 1.f : -1.f, 0); pen = r + oy; }
    else                           { nL = Vector3(0, 0, spL.z > 0 ? 1.f : -1.f); pen = r + oz; }
    return true;
}

// Closest points between two segments; returns squared distance and writes cp/cq.
static float ClosestSegSeg(const Vector3& p0, const Vector3& p1,
                           const Vector3& q0, const Vector3& q1,
                           Vector3& cp, Vector3& cq) {
    Vector3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    float a = Vector3::Dot(d1, d1), e = Vector3::Dot(d2, d2), f = Vector3::Dot(d2, r);
    float s, t;
    if (a < 1e-12f && e < 1e-12f) { s = t = 0.f; }
    else if (a < 1e-12f) { s = 0.f; t = std::clamp(f / e, 0.f, 1.f); }
    else {
        float c = Vector3::Dot(d1, r);
        if (e < 1e-12f) { t = 0.f; s = std::clamp(-c / a, 0.f, 1.f); }
        else {
            float b = Vector3::Dot(d1, d2), den = a * e - b * b;
            if (den > 1e-12f) s = std::clamp((b * f - c * e) / den, 0.f, 1.f);
            else {
                // parallel: use midpoint of the overlapping interval projected on segment p
                float t0 = Vector3::Dot(q0 - p0, d1) / a;
                float t1 = Vector3::Dot(q1 - p0, d1) / a;
                if (t0 > t1) std::swap(t0, t1);
                float lo = std::max(t0, 0.f), hi = std::min(t1, 1.f);
                s = (lo <= hi) ? std::clamp((lo + hi) * 0.5f, 0.f, 1.f) : 0.f;
            }
            t = (b * s + f) / e;
            if      (t < 0.f) { t = 0.f; s = std::clamp(-c / a,       0.f, 1.f); }
            else if (t > 1.f) { t = 1.f; s = std::clamp((Vector3::Dot(d1,d2) - c) / a, 0.f, 1.f); }
        }
    }
    cp = p0 + d1 * s; cq = q0 + d2 * t;
    Vector3 d = cp - cq;
    return Vector3::Dot(d, d);
}

static float V3Get(const Vector3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
static void  V3Set(Vector3& v, int i, float f) { if (i == 0) v.x = f; else if (i == 1) v.y = f; else v.z = f; }

// Build a contact from a geometric result and push it (used by every narrow-phase).
static void Emit(std::vector<Contact>& out, const Contact& base,
                 const Vector3& normal, float penetration, const Vector3& point) {
    Contact c   = base;
    c.normal      = normal;
    c.penetration = penetration;
    c.point       = point;
    out.push_back(c);
}

// ============================================================================
//  World-space shape resolution
//    Resolves a collider into world space using the actor's cached world transform,
//    baking the world SCALE into the dimensions (so a scaled mesh and its collider agree)
//    and the world ROTATION into the capsule axis.
// ============================================================================

namespace {

struct ShapeWS {
    ColliderShape shape       = ColliderShape::AABB;
    Vector3       center      = {};   // world collider centre (also used as centre of mass)
    Quaternion    rot         = {};   // world rotation
    float         radius      = 0.f;  // scaled (sphere / capsule)
    Vector3       halfExtents = {};   // scaled (box)
    float         halfHeight  = 0.f;  // scaled (capsule)
    Vector3       capBase = {}, capTip = {};
};

ShapeWS MakeShape(const TransformComponent* tc, const ColliderComponent* col) {
    const Transform& w = tc->CachedWorld;
    Vector3 as(std::fabsf(w.Scale.x), std::fabsf(w.Scale.y), std::fabsf(w.Scale.z));
    ShapeWS o;
    o.shape  = col->Shape;
    o.center = w.TransformPoint(col->Offset);
    o.rot    = w.Rotation;
    switch (col->Shape) {
    case ColliderShape::Sphere:
        o.radius = col->Radius * std::max({ as.x, as.y, as.z });          // keep it a sphere
        break;
    case ColliderShape::AABB:
        o.halfExtents = Vector3(col->HalfExtents.x * as.x,
                                col->HalfExtents.y * as.y,
                                col->HalfExtents.z * as.z);
        break;
    case ColliderShape::Capsule: {
        o.radius     = col->Radius * std::max(as.x, as.z);                // radius from X/Z
        o.halfHeight = col->HalfHeight * as.y;                            // length from Y
        Vector3 up   = o.rot * Vector3(0, 1, 0);
        o.capBase = o.center - up * o.halfHeight;
        o.capTip  = o.center + up * o.halfHeight;
        break;
    }
    }
    return o;
}

// Parent's world transform (identity for a root actor) — used to convert world→local on write.
Transform ParentWorldOf(const TransformComponent* tc) {
    Actor* owner = tc->GetOwner();
    if (owner && owner->GetParent())
        if (auto* p = owner->GetParent()->GetComponent<TransformComponent>())
            return p->CachedWorld;
    return Transform{};
}

// ---- ray vs shape (dir assumed normalised; returns nearest t >= 0) ----

bool RaySphere(const Vector3& o, const Vector3& dir, const Vector3& c, float r,
               float& t, Vector3& normal) {
    Vector3 m = o - c;
    float b = Vector3::Dot(m, dir);
    float cc = Vector3::Dot(m, m) - r * r;
    if (cc > 0.f && b > 0.f) return false;          // outside and pointing away
    float disc = b * b - cc;
    if (disc < 0.f) return false;
    float th = -b - std::sqrt(disc);
    if (th < 0.f) th = 0.f;                          // origin inside
    t = th;
    normal = ((o + dir * th) - c).GetSafeNormal();
    return true;
}

bool RayAabb(const Vector3& o, const Vector3& dir, const Vector3& center, const Vector3& he,
             float maxDist, float& t, Vector3& normal) {
    Vector3 mn = center - he, mx = center + he;
    float tmin = 0.f, tmax = maxDist;
    int axis = -1; float axisSign = 0.f;
    for (int a = 0; a < 3; ++a) {
        float od = V3Get(o, a), dd = V3Get(dir, a), lo = V3Get(mn, a), hi = V3Get(mx, a);
        if (dd > -1e-8f && dd < 1e-8f) { if (od < lo || od > hi) return false; continue; }
        float inv = 1.f / dd;
        float t1 = (lo - od) * inv, t2 = (hi - od) * inv;
        float entrySign = -1.f;                      // entering the low face
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; entrySign = 1.f; }
        if (t1 > tmin) { tmin = t1; axis = a; axisSign = entrySign; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (axis < 0) { t = 0.f; normal = Vector3(0, 1, 0); return true; }   // origin inside
    t = tmin;
    normal = {}; V3Set(normal, axis, axisSign);
    return true;
}

// Inigo Quilez capsule intersection + normal. dir normalised.
bool RayCapsule(const Vector3& o, const Vector3& dir, const Vector3& pa, const Vector3& pb, float r,
                float& t, Vector3& normal) {
    Vector3 ba = pb - pa, oa = o - pa;
    float baba = Vector3::Dot(ba, ba);
    float bard = Vector3::Dot(ba, dir);
    float baoa = Vector3::Dot(ba, oa);
    float rdoa = Vector3::Dot(dir, oa);
    float oaoa = Vector3::Dot(oa, oa);
    float A = baba - bard * bard;
    float B = baba * rdoa - baoa * bard;
    float C = baba * oaoa - baoa * baoa - r * r * baba;
    float h = B * B - A * C;
    float th = -1.f;
    if (h >= 0.f && A > 1e-12f) {
        th = (-B - std::sqrt(h)) / A;
        float y = baoa + th * bard;
        if (y < 0.f || y > baba) {                   // outside the cylinder body → test a cap
            Vector3 oc = (y <= 0.f) ? oa : (o - pb);
            float bb = Vector3::Dot(dir, oc);
            float cc = Vector3::Dot(oc, oc) - r * r;
            float hh = bb * bb - cc;
            th = (hh > 0.f) ? (-bb - std::sqrt(hh)) : -1.f;
        }
    } else {
        // ray ~parallel to the axis: test both end caps
        float best = -1.f;
        for (const Vector3& cap : { pa, pb }) {
            float tc; Vector3 n;
            if (RaySphere(o, dir, cap, r, tc, n) && (best < 0.f || tc < best)) best = tc;
        }
        th = best;
    }
    if (th < 0.f) return false;
    t = th;
    Vector3 p = o + dir * th;
    float yy = std::clamp(Vector3::Dot(p - pa, ba) / baba, 0.f, 1.f);
    normal = (p - (pa + ba * yy)).GetSafeNormal();
    return true;
}

// ---- overlap of a query sphere against a world shape (boolean) ----
bool SphereOverlapsShape(const Vector3& qc, float qr, const ShapeWS& s) {
    switch (s.shape) {
    case ColliderShape::Sphere: {
        float rs = qr + s.radius; Vector3 d = qc - s.center;
        return Vector3::Dot(d, d) <= rs * rs;
    }
    case ColliderShape::AABB: {
        // Box may be oriented: test in its local frame (rotation preserves distances).
        Vector3 qcL = s.rot.Inverse() * (qc - s.center);
        Vector3 mn(-s.halfExtents.x, -s.halfExtents.y, -s.halfExtents.z);
        Vector3 mx(s.halfExtents.x, s.halfExtents.y, s.halfExtents.z);
        Vector3 cl = ClosestPtOnAABB(qcL, mn, mx);
        Vector3 d = qcL - cl; return Vector3::Dot(d, d) <= qr * qr;
    }
    case ColliderShape::Capsule: {
        Vector3 cl = ClosestPtOnSegment(qc, s.capBase, s.capTip);
        float rs = qr + s.radius; Vector3 d = qc - cl;
        return Vector3::Dot(d, d) <= rs * rs;
    }
    }
    return false;
}

} // namespace

// ============================================================================
//  Inertia
// ============================================================================

void PhysicsWorld::ComputeInertia(RigidbodyComponent* rb, ColliderComponent* col, const TransformComponent* tc) {
    if (!rb || rb->IsKinematic || rb->Mass <= 0.f) { if (rb) rb->InvInertiaLocal = {}; return; }
    ShapeWS s = MakeShape(tc, col);          // scaled, world-space dimensions
    float m = rb->Mass;
    // A degenerate collider (radius/extent 0, e.g. user-zeroed in the inspector) makes a moment
    // of inertia 0; 1/I would be inf and propagate NaN through the whole solver. Treat a ~0 moment
    // as infinitely resistant to rotation (inv = 0) so the body simply doesn't spin about that axis.
    auto safeInv = [](float I) { return (I > 1e-9f) ? 1.f / I : 0.f; };
    Vector3 inv{};
    switch (col->Shape) {
    case ColliderShape::Sphere: {
        float I = 0.4f * m * s.radius * s.radius;                // 2/5 m r^2
        inv = { safeInv(I), safeInv(I), safeInv(I) }; break;
    }
    case ColliderShape::AABB: {
        float hx = s.halfExtents.x, hy = s.halfExtents.y, hz = s.halfExtents.z;
        inv = { safeInv(m * (hy*hy + hz*hz) / 3.f),              // 1/3 m (h^2 + h^2)
                safeInv(m * (hx*hx + hz*hz) / 3.f),
                safeInv(m * (hx*hx + hy*hy) / 3.f) }; break;
    }
    case ColliderShape::Capsule: {
        float r = s.radius, h = s.halfHeight * 2.f;              // cylinder approximation
        float Iy  = 0.5f * m * r * r;
        float Ixz = m * (3.f * r * r + h * h) / 12.f;
        inv = { safeInv(Ixz), safeInv(Iy), safeInv(Ixz) }; break;
    }
    }
    rb->InvInertiaLocal = inv;
}

// ============================================================================
//  Broadphase
// ============================================================================

AABB PhysicsWorld::WorldAABB(TransformComponent* tc, ColliderComponent* col) {
    ShapeWS s = MakeShape(tc, col);
    switch (s.shape) {
    case ColliderShape::Sphere: {
        Vector3 r(s.radius, s.radius, s.radius);
        return { s.center - r, s.center + r };
    }
    case ColliderShape::AABB: {
        // Enclose the (possibly rotated) box by transforming its 8 corners — convention-proof.
        const Vector3& he = s.halfExtents;
        Vector3 c0 = s.center + s.rot * Vector3(-he.x, -he.y, -he.z);
        Vector3 mn = c0, mx = c0;
        for (int i = 1; i < 8; ++i) {
            Vector3 corner((i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z);
            Vector3 w = s.center + s.rot * corner;
            mn = Vector3(std::min(mn.x, w.x), std::min(mn.y, w.y), std::min(mn.z, w.z));
            mx = Vector3(std::max(mx.x, w.x), std::max(mx.y, w.y), std::max(mx.z, w.z));
        }
        return { mn, mx };
    }
    case ColliderShape::Capsule: {
        Vector3 r(s.radius, s.radius, s.radius);
        Vector3 mn(std::min(s.capBase.x, s.capTip.x), std::min(s.capBase.y, s.capTip.y), std::min(s.capBase.z, s.capTip.z));
        Vector3 mx(std::max(s.capBase.x, s.capTip.x), std::max(s.capBase.y, s.capTip.y), std::max(s.capBase.z, s.capTip.z));
        return { mn - r, mx + r };
    }
    }
    return { s.center, s.center };
}

bool PhysicsWorld::Overlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

uint64_t PhysicsWorld::PairKey(uint64_t a, uint64_t b) {
    if (a > b) std::swap(a, b);
    // Bit-pack the ordered ids. Injective (no overflow) for ids < 2^32, unlike Szudzik's
    // a + b*b whose b*b overflows uint64 as b approaches 2^32.
    return (a << 32) | (b & 0xffffffffULL);
}

// ============================================================================
//  Narrow-phase
//    Convention: normal points from B toward A. penetration = -separation.
//    A contact is emitted whenever separation < margin (speculative).
// ============================================================================

void PhysicsWorld::SphereSphere(const Vector3& ca, float ra,
                                const Vector3& cb, float rb,
                                float margin, std::vector<Contact>& out, const Contact& base) {
    Vector3 d = ca - cb;
    float dist = d.Length();
    float sep  = dist - (ra + rb);
    if (sep >= margin) return;
    Vector3 n = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);   // B → A
    Emit(out, base, n, -sep, cb + n * rb);
}

void PhysicsWorld::SphereBox(const Vector3& sp, float r,
                             const Vector3& bc, const Vector3& he, const Quaternion& brot,
                             float margin, std::vector<Contact>& out, const Contact& base) {
    // Work in the box's local frame so it becomes an axis-aligned box at the origin.
    Quaternion inv = brot.Inverse();
    Vector3    spL = inv * (sp - bc);
    Vector3 nL, clL; float pen;
    if (!SphereAabbLocal(spL, r, he, margin, nL, pen, clL)) return;
    Vector3 n  = brot * nL;                  // box(B) → sphere(A), back in world
    Vector3 pt = bc + brot * clL;
    Emit(out, base, n, pen, pt);
}

void PhysicsWorld::SphereCapsule(const Vector3& sp, float sr,
                                 const Vector3& a, const Vector3& b, float cr,
                                 float margin, std::vector<Contact>& out, const Contact& base) {
    Vector3 cl = ClosestPtOnSegment(sp, a, b);
    Vector3 d  = sp - cl;
    float dist = d.Length();
    float sep  = dist - (sr + cr);
    if (sep >= margin) return;
    Vector3 n = (dist > 1e-6f) ? d / dist : Vector3(0, 1, 0);   // capsule(B) → sphere(A)
    Emit(out, base, n, -sep, cl + n * cr);
}

// ---- Oriented box (OBB) vs oriented box ----
namespace {

struct ObbBox { Vector3 c; Vector3 h; Vector3 u[3]; };   // centre, half-extents, world unit axes

// Half-projection of a box onto a (unit) world axis L.
static float ObbProj(const ObbBox& b, const Vector3& L) {
    return std::fabsf(Vector3::Dot(b.u[0], L)) * b.h.x
         + std::fabsf(Vector3::Dot(b.u[1], L)) * b.h.y
         + std::fabsf(Vector3::Dot(b.u[2], L)) * b.h.z;
}

// The face of `b` (4 world verts, CCW) whose outward normal is most anti-parallel to n.
static void ObbIncidentFace(const ObbBox& b, const Vector3& n, Vector3 out[4]) {
    int a = 0; float best = std::fabsf(Vector3::Dot(b.u[0], n));
    for (int i = 1; i < 3; ++i) { float dd = std::fabsf(Vector3::Dot(b.u[i], n)); if (dd > best) { best = dd; a = i; } }
    float s = (Vector3::Dot(b.u[a], n) > 0.f) ? -1.f : 1.f;       // face opposing n
    int t1 = (a + 1) % 3, t2 = (a + 2) % 3;
    Vector3 fc = b.c + b.u[a] * (s * V3Get(b.h, a));
    Vector3 e1 = b.u[t1] * V3Get(b.h, t1), e2 = b.u[t2] * V3Get(b.h, t2);
    out[0] = fc - e1 - e2; out[1] = fc + e1 - e2; out[2] = fc + e1 + e2; out[3] = fc - e1 + e2;
}

// Sutherland–Hodgman clip of polygon `in` (n verts) against the half-space dot(pn,p) <= off.
static int ObbClip(const Vector3* in, int n, const Vector3& pn, float off, Vector3* out) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Vector3& cur = in[i];
        const Vector3& prv = in[(i + n - 1) % n];
        float dc = Vector3::Dot(pn, cur) - off;
        float dp = Vector3::Dot(pn, prv) - off;
        bool inC = dc <= 0.f, inP = dp <= 0.f;
        if (inP != inC) { float t = dp / (dp - dc); out[m++] = prv + (cur - prv) * t; }
        if (inC && m < 8) out[m++] = cur;
        if (m >= 8) break;
    }
    return m;
}

} // namespace

// Oriented box vs oriented box (SAT over 15 axes + reference-face clipping for a stable manifold).
// Face contacts emit up to a few points (keeps stacks from rocking); an edge-edge separating axis
// emits a single closest-point contact. Normal is reported B → A, penetration > 0 when overlapping.
void PhysicsWorld::BoxBox(const Vector3& ca, const Vector3& ha, const Quaternion& qa,
                          const Vector3& cb, const Vector3& hb, const Quaternion& qb,
                          float margin, std::vector<Contact>& out, const Contact& base) {
    ObbBox A{ ca, ha, { qa * Vector3(1,0,0), qa * Vector3(0,1,0), qa * Vector3(0,0,1) } };
    ObbBox B{ cb, hb, { qb * Vector3(1,0,0), qb * Vector3(0,1,0), qb * Vector3(0,0,1) } };
    Vector3 d = ca - cb;   // points B → A

    // Track the best (largest separation = least overlap) face axis and edge axis separately,
    // then bias toward faces to avoid edge-axis jitter on near-aligned boxes.
    float   faceSep = -1e30f; Vector3 faceL{}; int faceOwner = -1;
    float   edgeSep = -1e30f; Vector3 edgeL{}; int edgeI = -1, edgeJ = -1;

    // Returns false if this axis separates the boxes beyond the margin (⇒ no contact at all).
    auto axisSep = [&](Vector3 L, float& sepOut) -> bool {
        float len2 = Vector3::Dot(L, L);
        if (len2 < 1e-8f) { sepOut = -1e30f; return true; }       // degenerate (parallel edges)
        L = L / std::sqrt(len2);
        if (Vector3::Dot(L, d) < 0.f) L = -L;                     // orient B → A
        float sep = std::fabsf(Vector3::Dot(d, L)) - ObbProj(A, L) - ObbProj(B, L);
        sepOut = sep;
        return sep < margin;
    };

    float sep;
    for (int i = 0; i < 3; ++i) {
        if (!axisSep(A.u[i], sep)) return;
        if (sep > faceSep) { faceSep = sep; faceL = (Vector3::Dot(A.u[i], d) < 0.f) ? -A.u[i] : A.u[i]; faceOwner = 0; }
    }
    for (int i = 0; i < 3; ++i) {
        if (!axisSep(B.u[i], sep)) return;
        if (sep > faceSep) { faceSep = sep; faceL = (Vector3::Dot(B.u[i], d) < 0.f) ? -B.u[i] : B.u[i]; faceOwner = 1; }
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            Vector3 L = Vector3::Cross(A.u[i], B.u[j]);
            if (!axisSep(L, sep)) return;
            float len2 = Vector3::Dot(L, L);
            if (len2 >= 1e-8f && sep > edgeSep) {
                L = L / std::sqrt(len2);
                edgeSep = sep; edgeL = (Vector3::Dot(L, d) < 0.f) ? -L : L; edgeI = i; edgeJ = j;
            }
        }

    if (faceOwner < 0) return;

    const float kEdgeBias = 0.001f;   // prefer face axes unless an edge axis is clearly deeper
    if (edgeJ >= 0 && edgeSep > faceSep + kEdgeBias) {
        // ── Edge-edge: single contact at the closest points of the two world edges ──
        Vector3 nBA = edgeL;                                   // B → A
        // A's edge (parallel to A.u[edgeI]) sits on the corner facing B (i.e. along -nBA).
        Vector3 pA = A.c;
        for (int k = 0; k < 3; ++k) if (k != edgeI)
            pA = pA + A.u[k] * (Vector3::Dot(A.u[k], nBA) < 0.f ? V3Get(A.h, k) : -V3Get(A.h, k));
        Vector3 pB = B.c;
        for (int k = 0; k < 3; ++k) if (k != edgeJ)
            pB = pB + B.u[k] * (Vector3::Dot(B.u[k], nBA) > 0.f ? V3Get(B.h, k) : -V3Get(B.h, k));
        Vector3 a0 = pA - A.u[edgeI] * V3Get(A.h, edgeI), a1 = pA + A.u[edgeI] * V3Get(A.h, edgeI);
        Vector3 b0 = pB - B.u[edgeJ] * V3Get(B.h, edgeJ), b1 = pB + B.u[edgeJ] * V3Get(B.h, edgeJ);
        Vector3 cpA, cpB; ClosestSegSeg(a0, a1, b0, b1, cpA, cpB);
        Emit(out, base, nBA, -edgeSep, (cpA + cpB) * 0.5f);
        return;
    }

    // ── Face contact: clip the incident face against the reference face's side planes ──
    ObbBox& ref = (faceOwner == 0) ? A : B;
    ObbBox& inc = (faceOwner == 0) ? B : A;
    Vector3 nBA = faceL;                                       // B → A (the reported normal)
    // Reference outward normal points from ref centre toward the incident box.
    Vector3 nRef = (Vector3::Dot(faceL, inc.c - ref.c) > 0.f) ? faceL : -faceL;

    int aRef = 0; float bestd = std::fabsf(Vector3::Dot(ref.u[0], nRef));
    for (int i = 1; i < 3; ++i) { float dd = std::fabsf(Vector3::Dot(ref.u[i], nRef)); if (dd > bestd) { bestd = dd; aRef = i; } }
    float sRef = (Vector3::Dot(ref.u[aRef], nRef) > 0.f) ? 1.f : -1.f;
    Vector3 nRefU = ref.u[aRef] * sRef;                       // exact reference face normal
    Vector3 fcR   = ref.c + nRefU * V3Get(ref.h, aRef);       // reference face centre

    Vector3 poly[8]; ObbIncidentFace(inc, nRefU, poly);
    int np = 4;
    int t1 = (aRef + 1) % 3, t2 = (aRef + 2) % 3;
    Vector3 clipped[8];
    for (int side = 0; side < 2; ++side) {
        int   ax  = side == 0 ? t1 : t2;
        float hh  = V3Get(ref.h, ax);
        // + face of the slab
        np = ObbClip(poly, np, ref.u[ax], Vector3::Dot(ref.u[ax], ref.c) + hh, clipped);
        for (int i = 0; i < np; ++i) poly[i] = clipped[i];
        // - face of the slab
        np = ObbClip(poly, np, ref.u[ax] * -1.0f, -Vector3::Dot(ref.u[ax], ref.c) + hh, clipped);
        for (int i = 0; i < np; ++i) poly[i] = clipped[i];
    }

    bool any = false;
    for (int i = 0; i < np; ++i) {
        float s = Vector3::Dot(nRefU, poly[i] - fcR);          // >0 above the face (gap), <0 below (penetrating)
        if (s >= margin) continue;
        Emit(out, base, nBA, -s, poly[i]);
        any = true;
    }
    // Degenerate fallback (e.g. clipping wiped every point): one contact along the axis.
    if (!any) Emit(out, base, nBA, -faceSep, fcR);
}

// Box(A) vs capsule(B). If flipNormal, the caller's A/B are swapped (capsule is the real A),
// so the emitted normal is negated to stay "from B toward A" in the caller's terms.
void PhysicsWorld::BoxCapsule(const Vector3& bc, const Vector3& he, const Quaternion& brot,
                              const Vector3& a, const Vector3& b, float cr,
                              float margin, std::vector<Contact>& out, const Contact& base,
                              bool flipNormal) {
    // Transform the capsule axis into the box's local frame; the box is then an AABB at origin.
    Quaternion inv = brot.Inverse();
    Vector3    aL  = inv * (a - bc), bL = inv * (b - bc);
    Vector3 mn(-he.x, -he.y, -he.z), mx(he.x, he.y, he.z);

    // Closest point on the capsule axis to the box (local). Seed from both endpoints and from the
    // box-centre projection, then a few alternating projections; keep the nearest result.
    auto iterate = [&](Vector3 seed) -> Vector3 {
        for (int i = 0; i < 4; ++i) {
            Vector3 bp  = ClosestPtOnAABB(seed, mn, mx);
            Vector3 nxt = ClosestPtOnSegment(bp, aL, bL);
            if ((nxt - seed).LengthSquared() < 1e-10f) break;
            seed = nxt;
        }
        return seed;
    };
    Vector3 seeds[3] = { aL, bL, ClosestPtOnSegment(Vector3{}, aL, bL) };
    Vector3 bestCp = seeds[0];
    float   bestD2 = 1e30f;
    for (const auto& s : seeds) {
        Vector3 cand = iterate(s);
        Vector3 bp   = ClosestPtOnAABB(cand, mn, mx);
        float   dd   = (cand - bp).LengthSquared();
        if (dd < bestD2) { bestD2 = dd; bestCp = cand; }
    }

    // Treat the nearest axis point as a sphere of radius cr against the local box.
    Vector3 nL, clL; float pen;
    if (!SphereAabbLocal(bestCp, cr, he, margin, nL, pen, clL)) return;
    Vector3 nWorld = brot * nL;             // box(A) → capsule point, i.e. A → B
    Vector3 n      = -nWorld;               // we need B → A
    Vector3 pt     = (bc + brot * bestCp) + n * cr;   // on capsule(B) surface toward box(A)
    if (flipNormal) n = -n;                 // caller swapped A/B
    Emit(out, base, n, pen, pt);
}

void PhysicsWorld::CapsuleCapsule(const Vector3& a0, const Vector3& a1, float ra,
                                  const Vector3& b0, const Vector3& b1, float rb,
                                  float margin, std::vector<Contact>& out, const Contact& base) {
    Vector3 clA, clB;
    float d2   = ClosestSegSeg(a0, a1, b0, b1, clA, clB);
    float dist = std::sqrt(d2);
    float sep  = dist - (ra + rb);
    if (sep >= margin) return;
    Vector3 dv = clA - clB;
    Vector3 n  = (dist > 1e-6f) ? dv / dist : Vector3(0, 1, 0);  // B → A
    Emit(out, base, n, -sep, clB + n * rb);
}

// ============================================================================
//  Channel filter
// ============================================================================

static CollisionResponse CombinedResponse(const ColliderComponent* a, const ColliderComponent* b) {
    return std::min(a->GetResponseTo(b->Channel), b->GetResponseTo(a->Channel));
}

// ============================================================================
//  Pair dispatch
// ============================================================================

bool PhysicsWorld::TestPair(Actor* a, Actor* b, float dt, std::vector<Contact>& out) {
    auto* tcA  = a->GetComponent<TransformComponent>();
    auto* tcB  = b->GetComponent<TransformComponent>();
    auto* colA = a->GetComponent<ColliderComponent>();
    auto* colB = b->GetComponent<ColliderComponent>();
    if (!tcA || !tcB || !colA || !colB) return false;

    CollisionResponse resp = CombinedResponse(colA, colB);
    if (resp == CollisionResponse::Ignore) return false;

    // World-space shapes (scale baked into dimensions, rotation into the capsule axis).
    ShapeWS A = MakeShape(tcA, colA);
    ShapeWS B = MakeShape(tcB, colB);

    Contact base;
    base.actorA = a; base.actorB = b;
    base.tcA = tcA;  base.tcB = tcB;
    base.rbA = a->GetComponent<RigidbodyComponent>();
    base.rbB = b->GetComponent<RigidbodyComponent>();
    base.isTrigger = (colA->IsTrigger || colB->IsTrigger || resp == CollisionResponse::Overlap);
    base.comA = A.center;   // world centres of mass, used for the solver lever arms
    base.comB = B.center;

    // Speculative margin = static offset + how far the pair can close this substep.
    Vector3 vA = base.rbA ? base.rbA->Velocity : Vector3{};
    Vector3 vB = base.rbB ? base.rbB->Velocity : Vector3{};
    float   relSpeed = (vA - vB).Length();
    float   margin   = ContactOffset + relSpeed * dt;

    auto shA = A.shape, shB = B.shape;
    size_t before = out.size();

    if (shA == ColliderShape::Sphere && shB == ColliderShape::Sphere) {
        SphereSphere(A.center, A.radius, B.center, B.radius, margin, out, base);
    }
    else if (shA == ColliderShape::AABB && shB == ColliderShape::AABB) {
        BoxBox(A.center, A.halfExtents, A.rot, B.center, B.halfExtents, B.rot, margin, out, base);
    }
    else if (shA == ColliderShape::Sphere && shB == ColliderShape::AABB) {
        SphereBox(A.center, A.radius, B.center, B.halfExtents, B.rot, margin, out, base);   // sphere=A box=B ⇒ B→A
    }
    else if (shA == ColliderShape::AABB && shB == ColliderShape::Sphere) {
        // sphere=B box=A: SphereBox gives box(A)→sphere(B) = A→B; flip to B→A
        std::vector<Contact> tmp; Contact tb = base;
        SphereBox(B.center, B.radius, A.center, A.halfExtents, A.rot, margin, tmp, tb);
        for (auto& c : tmp) { c.normal = -c.normal; out.push_back(c); }
    }
    else if (shA == ColliderShape::Capsule && shB == ColliderShape::Capsule) {
        CapsuleCapsule(A.capBase, A.capTip, A.radius, B.capBase, B.capTip, B.radius, margin, out, base);
    }
    else if (shA == ColliderShape::Sphere && shB == ColliderShape::Capsule) {
        SphereCapsule(A.center, A.radius, B.capBase, B.capTip, B.radius, margin, out, base); // capsule=B ⇒ B→A
    }
    else if (shA == ColliderShape::Capsule && shB == ColliderShape::Sphere) {
        // sphere=B capsule=A: SphereCapsule gives capsule(A)→sphere(B) = A→B; flip
        std::vector<Contact> tmp; Contact tb = base;
        SphereCapsule(B.center, B.radius, A.capBase, A.capTip, A.radius, margin, tmp, tb);
        for (auto& c : tmp) { c.normal = -c.normal; out.push_back(c); }
    }
    else if (shA == ColliderShape::AABB && shB == ColliderShape::Capsule) {
        // box=A capsule=B ⇒ want B→A, no flip
        BoxCapsule(A.center, A.halfExtents, A.rot, B.capBase, B.capTip, B.radius, margin, out, base, /*flip*/false);
    }
    else if (shA == ColliderShape::Capsule && shB == ColliderShape::AABB) {
        // box=B capsule=A ⇒ BoxCapsule gives capsule→box = A→B; flip to B→A
        BoxCapsule(B.center, B.halfExtents, B.rot, A.capBase, A.capTip, A.radius, margin, out, base, /*flip*/true);
    }

    return out.size() > before;
}

// ============================================================================
//  Events
// ============================================================================

void PhysicsWorld::DispatchEvents(SceneManager& scene, const std::vector<Contact>& contacts) {
    std::unordered_map<uint64_t, OverlapPair> cur;

    for (const auto& c : contacts) {
        if (c.penetration <= 0.f) continue;       // speculative (not actually touching) → no event
        auto* colA = c.actorA->GetComponent<ColliderComponent>();
        auto* colB = c.actorB->GetComponent<ColliderComponent>();

        if (!c.isTrigger) {
            // Fire OnHit at most once per pair per frame, even across multiple substeps.
            uint64_t hitKey = PairKey(c.actorA->GetId(), c.actorB->GetId());
            if (m_hitThisFrame.insert(hitKey).second) {
                if (colA && colA->OnHit) { HitResult hr{ c.actorB,  c.normal, c.point, c.penetration }; colA->OnHit(hr); }
                if (colB && colB->OnHit) { HitResult hr{ c.actorA, -c.normal, c.point, c.penetration }; colB->OnHit(hr); }
            }
        } else {
            uint64_t key = PairKey(c.actorA->GetId(), c.actorB->GetId());
            if (cur.find(key) == cur.end()) {
                cur[key] = { c.actorA->GetId(), c.actorB->GetId() };
                if (m_prevOverlaps.find(key) == m_prevOverlaps.end()) {
                    if (colA && colA->OnBeginOverlap) colA->OnBeginOverlap(c.actorB);
                    if (colB && colB->OnBeginOverlap) colB->OnBeginOverlap(c.actorA);
                }
            }
        }
    }
    for (const auto& [key, en] : m_prevOverlaps) {
        if (cur.find(key) == cur.end()) {
            Actor* aA = scene.FindActorById(en.a), *aB = scene.FindActorById(en.b);
            if (!aA || !aB) continue;
            auto* cA = aA->GetComponent<ColliderComponent>();
            auto* cB = aB->GetComponent<ColliderComponent>();
            if (cA && cA->OnEndOverlap) cA->OnEndOverlap(aB);
            if (cB && cB->OnEndOverlap) cB->OnEndOverlap(aA);
        }
    }
    m_prevOverlaps = std::move(cur);
}

// ============================================================================
//  Sequential-impulse solver
// ============================================================================

void PhysicsWorld::PreSolve(Contact& c, float dt) {
    c.rA = c.point - c.comA;   // lever arms about the world centre of mass
    c.rB = c.point - c.comB;

    auto invM = [](const RigidbodyComponent* rb) {
        return (rb && !rb->IsKinematic && rb->Mass > 0.f) ? 1.f / rb->Mass : 0.f;
    };
    auto angIM = [&](const RigidbodyComponent* rb, const TransformComponent* tc,
                     const Vector3& r, const Vector3& n) {
        if (!rb || rb->IsKinematic || rb->Mass <= 0.f) return 0.f;
        Vector3 rn = Vector3::Cross(r, n);
        return Vector3::Dot(rn, rb->ApplyInvInertiaWorld(tc->CachedWorld.Rotation, rn));
    };

    float iMA = invM(c.rbA), iMB = invM(c.rbB);

    float invN = iMA + iMB + angIM(c.rbA, c.tcA, c.rA, c.normal)
                          + angIM(c.rbB, c.tcB, c.rB, c.normal);
    c.normalMass = (invN > 1e-10f) ? 1.f / invN : 0.f;

    // Relative normal velocity (A relative to B). Negative ⇒ approaching.
    Vector3 vA = c.rbA ? c.rbA->Velocity : Vector3{}, vB = c.rbB ? c.rbB->Velocity : Vector3{};
    Vector3 wA = c.rbA ? c.rbA->AngularVelocity : Vector3{}, wB = c.rbB ? c.rbB->AngularVelocity : Vector3{};
    float vn = Vector3::Dot((vA + Vector3::Cross(wA, c.rA)) - (vB + Vector3::Cross(wB, c.rB)), c.normal);

    // Does the pair actually meet THIS substep? Either already overlapping, or the approach
    // speed (-vn) covers the remaining gap within dt  ⇔  vn <= penetration/dt (both negative).
    bool willTouch = (c.penetration >= 0.f) || (vn <= c.penetration / dt);

    if (willTouch && vn < -RestitutionThreshold) {
        // Real impact this substep. Use vn — the PRE-solve approach velocity — for restitution,
        // so the bounce reflects the true impact speed. The body is never braked before this
        // moment (the speculative branch below stays slack until it arrives), so no energy is
        // bled off the approach: this is what makes dropped bodies actually bounce.
        // Combine the participating bodies' restitution (average of those present). A static
        // anchor has no rigidbody, so a bouncy body dropped on the ground keeps its own value
        // instead of being clamped to a hard-coded constant.
        float e = 0.0f; int ne = 0;
        if (c.rbA) { e += c.rbA->Restitution; ++ne; }
        if (c.rbB) { e += c.rbB->Restitution; ++ne; }
        e = (ne > 0) ? e / (float)ne : 0.0f;
        c.velocityBias = -e * vn;                       // > 0 : rebound
    } else {
        // No bounce: stop exactly on the surface. For a gap this lets the body close it this
        // substep and no more (speculative, prevents tunnelling); when already touching it is 0.
        c.velocityBias = (c.penetration < 0.f) ? c.penetration / dt : 0.f;
    }

    // Tangent basis perpendicular to the normal.
    const Vector3& n = c.normal;
    if (std::fabsf(n.x) <= std::fabsf(n.y) && std::fabsf(n.x) <= std::fabsf(n.z))
        c.tangent0 = Vector3::Cross(n, { 1,0,0 }).GetSafeNormal();
    else if (std::fabsf(n.y) <= std::fabsf(n.z))
        c.tangent0 = Vector3::Cross(n, { 0,1,0 }).GetSafeNormal();
    else
        c.tangent0 = Vector3::Cross(n, { 0,0,1 }).GetSafeNormal();
    c.tangent1 = Vector3::Cross(n, c.tangent0).GetSafeNormal();

    auto tMass = [&](const Vector3& t) {
        float inv = iMA + iMB + angIM(c.rbA, c.tcA, c.rA, t) + angIM(c.rbB, c.tcB, c.rB, t);
        return (inv > 1e-10f) ? 1.f / inv : 0.f;
    };
    c.tangent0Mass = tMass(c.tangent0);
    c.tangent1Mass = tMass(c.tangent1);

    c.normalImpulse = c.tangent0Impulse = c.tangent1Impulse = 0.f;
}

void PhysicsWorld::ApplyImpulse(Contact& c, const Vector3& dir, float lambda) {
    if (lambda == 0.f) return;
    if (c.rbA && !c.rbA->IsKinematic && c.rbA->Mass > 0.f) {
        c.rbA->Velocity        += dir * (lambda / c.rbA->Mass);
        c.rbA->AngularVelocity += c.rbA->ApplyInvInertiaWorld(c.tcA->CachedWorld.Rotation, Vector3::Cross(c.rA, dir * lambda));
    }
    if (c.rbB && !c.rbB->IsKinematic && c.rbB->Mass > 0.f) {
        c.rbB->Velocity        -= dir * (lambda / c.rbB->Mass);
        c.rbB->AngularVelocity -= c.rbB->ApplyInvInertiaWorld(c.tcB->CachedWorld.Rotation, Vector3::Cross(c.rB, dir * lambda));
    }
}

void PhysicsWorld::SolveVelocity(Contact& c) {
    if (c.isTrigger || c.normalMass <= 0.f) return;

    // --- normal ---
    {
        Vector3 vA = c.rbA ? c.rbA->Velocity : Vector3{}, vB = c.rbB ? c.rbB->Velocity : Vector3{};
        Vector3 wA = c.rbA ? c.rbA->AngularVelocity : Vector3{}, wB = c.rbB ? c.rbB->AngularVelocity : Vector3{};
        float vn  = Vector3::Dot((vA + Vector3::Cross(wA, c.rA)) - (vB + Vector3::Cross(wB, c.rB)), c.normal);
        float lam = -(vn - c.velocityBias) * c.normalMass;
        float nNew = std::max(c.normalImpulse + lam, 0.f);
        ApplyImpulse(c, c.normal, nNew - c.normalImpulse);
        c.normalImpulse = nNew;
    }

    // --- friction (only while genuinely pushing) ---
    if (c.normalImpulse <= 0.f) return;
    // Combine the participating bodies' friction (average of those present); a static anchor
    // contributes none, so the dynamic body's own coefficient is used against the ground.
    float mu = 0.0f; int nf = 0;
    if (c.rbA) { mu += c.rbA->Friction; ++nf; }
    if (c.rbB) { mu += c.rbB->Friction; ++nf; }
    mu = (nf > 0) ? mu / (float)nf : 0.5f;
    float maxF = mu * c.normalImpulse;

    auto solveT = [&](const Vector3& t, float mass, float& acc) {
        Vector3 vA = c.rbA ? c.rbA->Velocity : Vector3{}, vB = c.rbB ? c.rbB->Velocity : Vector3{};
        Vector3 wA = c.rbA ? c.rbA->AngularVelocity : Vector3{}, wB = c.rbB ? c.rbB->AngularVelocity : Vector3{};
        float vt  = Vector3::Dot((vA + Vector3::Cross(wA, c.rA)) - (vB + Vector3::Cross(wB, c.rB)), t);
        float lam = -vt * mass;
        float nNew = std::clamp(acc + lam, -maxF, maxF);
        ApplyImpulse(c, t, nNew - acc);
        acc = nNew;
    };
    solveT(c.tangent0, c.tangent0Mass, c.tangent0Impulse);
    solveT(c.tangent1, c.tangent1Mass, c.tangent1Impulse);
}

// Positional depenetration (NGS). One correction per unique pair (deepest contact),
// clamped so a single deep contact can't teleport a body. Injects no kinetic energy.
void PhysicsWorld::CorrectPositions(std::vector<Contact>& contacts) {
    std::unordered_map<uint64_t, Contact*> deepest;
    for (auto& c : contacts) {
        if (c.isTrigger || c.penetration <= Slop) continue;
        uint64_t k = PairKey(c.actorA->GetId(), c.actorB->GetId());
        auto it = deepest.find(k);
        if (it == deepest.end() || c.penetration > it->second->penetration) deepest[k] = &c;
    }
    auto invM = [](const RigidbodyComponent* rb) {
        return (rb && !rb->IsKinematic && rb->Mass > 0.f) ? 1.f / rb->Mass : 0.f;
    };
    for (auto& [k, cp] : deepest) {
        (void)k;
        Contact& c = *cp;
        float iA = invM(c.rbA), iB = invM(c.rbB), tot = iA + iB;
        if (tot < 1e-12f) continue;
        float mag = std::min((c.penetration - Slop) * Baumgarte, MaxCorrection) / tot;
        Vector3 corr = c.normal * mag;
        // Correction is world-space: move the cached world origin, then write back to local.
        if (iA > 0.f) {
            Vector3 wp = c.tcA->CachedWorld.Position + corr * iA;
            c.tcA->Position = ParentWorldOf(c.tcA).InverseTransformPoint(wp);
            c.tcA->CachedWorld.Position = wp;
        }
        if (iB > 0.f) {
            Vector3 wp = c.tcB->CachedWorld.Position - corr * iB;
            c.tcB->Position = ParentWorldOf(c.tcB).InverseTransformPoint(wp);
            c.tcB->CachedWorld.Position = wp;
        }
    }
}

// ============================================================================
//  Integration
// ============================================================================

void PhysicsWorld::IntegrateVelocities(SceneManager& scene, float dt) {
    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        auto* tc = ap->GetComponent<TransformComponent>();
        if (!rb || !tc || rb->IsKinematic || rb->Mass <= 0.f || rb->IsSleeping) continue;

        if (rb->UseGravity) rb->AddForce(Gravity * rb->Mass);
        rb->Velocity        += rb->m_forceAccum * (dt / rb->Mass);
        rb->Velocity        *= std::max(0.f, 1.f - rb->LinearDamping * dt);
        rb->AngularVelocity += rb->ApplyInvInertiaWorld(tc->CachedWorld.Rotation, rb->m_torqueAccum) * dt;
        rb->AngularVelocity *= std::max(0.f, 1.f - rb->AngularDamping * dt);
        rb->ClearAccumulators();
    }
}

void PhysicsWorld::IntegratePositions(SceneManager& scene, float dt) {
    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        auto* tc = ap->GetComponent<TransformComponent>();
        if (!rb || !tc || rb->IsKinematic || rb->Mass <= 0.f || rb->IsSleeping) continue;

        // Integrate in WORLD space (velocities are world-space), then write back to local so
        // parented bodies move correctly. For a root actor parent-world is identity ⇒ local==world.
        Vector3    worldPos = tc->CachedWorld.Position + rb->Velocity * dt;
        Quaternion worldRot = tc->CachedWorld.Rotation;
        float wl = rb->AngularVelocity.Length();
        if (wl > 1e-6f) {
            Quaternion dq = Quaternion::FromAxisAngle(rb->AngularVelocity / wl, wl * dt);
            worldRot = dq * worldRot;
            worldRot.Normalize();
        }
        Transform pw = ParentWorldOf(tc);
        tc->Position = pw.InverseTransformPoint(worldPos);
        tc->Rotation = pw.InverseTransformRotation(worldRot);
        tc->Rotation.Normalize();
        // Keep the cache consistent for CorrectPositions later this same substep.
        tc->CachedWorld.Position = worldPos;
        tc->CachedWorld.Rotation = worldRot;
    }
}

// ============================================================================
//  Collision detection (velocity-swept broadphase + speculative narrow-phase)
// ============================================================================

void PhysicsWorld::DetectCollisions(SceneManager& scene, float dt, std::vector<Contact>& out) {
    auto& actors = scene.GetActors();
    if (actors.size() < 2) return;

    // ── 1. refresh broadphase proxies (persistent: only escaping moves trigger a rebuild) ──
    struct Item { Actor* actor; Aabb box; };
    std::vector<Item> items;
    items.reserve(actors.size());
    std::unordered_set<uint64_t> seen;
    seen.reserve(actors.size());

    for (auto& ap : actors) {
        auto* col = ap->GetComponent<ColliderComponent>();
        auto* tc  = ap->GetComponent<TransformComponent>();
        if (!col || !tc) continue;
        uint64_t id = ap->GetId();
        seen.insert(id);

        // World AABB swept by this substep's motion + the speculative offset, so pairs that are
        // about to touch are still produced for the narrow-phase.
        AABB w = WorldAABB(tc, col);
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        Vector3 disp = (rb && !rb->IsKinematic) ? rb->Velocity * dt : Vector3{};
        Vector3 ext(ContactOffset, ContactOffset, ContactOffset);
        Aabb box(w.min + Vector3(std::min(disp.x,0.f), std::min(disp.y,0.f), std::min(disp.z,0.f)) - ext,
                 w.max + Vector3(std::max(disp.x,0.f), std::max(disp.y,0.f), std::max(disp.z,0.f)) + ext);

        auto it = m_proxies.find(id);
        if (it == m_proxies.end()) m_proxies[id] = m_broadphase.CreateProxy(box, IdToUD(id));
        else                       m_broadphase.MoveProxy(it->second, box);
        items.push_back({ ap.get(), box });
    }

    // drop proxies for actors that disappeared
    for (auto it = m_proxies.begin(); it != m_proxies.end(); ) {
        if (seen.count(it->first)) ++it;
        else { m_broadphase.DestroyProxy(it->second); it = m_proxies.erase(it); }
    }

    // ── 2. emit each overlapping pair once (the lower-id proxy queries) ──
    for (auto& item : items) {
        uint64_t selfId = item.actor->GetId();
        Actor*   self   = item.actor;
        m_broadphase.QueryOverlap(item.box, [&](void* ud) {
            uint64_t otherId = UDToId(ud);
            if (otherId <= selfId) return;          // skip self and the mirror pair
            Actor* other = scene.FindActorById(otherId);
            if (other) TestPair(self, other, dt, out);
        });
    }
}

// ============================================================================
//  Sleeping
// ============================================================================

// Island-based sleeping. Bodies connected by resting contacts form an island; an island may
// sleep only when EVERY member is slow, undisturbed, and the island is grounded (touches a
// static/kinematic anchor). Otherwise the whole island stays awake. This makes a stack settle
// and wake together, and — crucially — a body that loses its support (its supporter is moved or
// removed) is no longer grounded, so it wakes and falls under gravity again.
void PhysicsWorld::UpdateSleep(SceneManager& scene, float dt, const std::vector<Contact>& contacts) {
    auto& actors = scene.GetActors();
    auto isDynamic = [](RigidbodyComponent* rb) {
        return rb && !rb->IsKinematic && rb->Mass > 0.f;
    };

    // Index every dynamic body.
    std::unordered_map<uint64_t, int> idToIdx;
    std::vector<RigidbodyComponent*>  rbs;
    std::vector<uint8_t> lowMotion, hasAnchor, disturbed;
    for (auto& ap : actors) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        if (!isDynamic(rb)) continue;
        idToIdx[ap->GetId()] = (int)rbs.size();
        rbs.push_back(rb);
        lowMotion.push_back(rb->Velocity.Length() < LinearSleepThreshold &&
                            rb->AngularVelocity.Length() < AngularSleepThreshold ? 1 : 0);
        hasAnchor.push_back(0);
        disturbed.push_back(m_disturbed.count(ap->GetId()) ? 1 : 0);
    }
    if (rbs.empty()) return;

    // Union-find over resting contacts between two dynamic bodies; contacts with an anchor
    // (static / kinematic) instead mark the dynamic side as grounded.
    std::vector<int> parent(rbs.size());
    for (int i = 0; i < (int)parent.size(); ++i) parent[i] = i;
    auto find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    auto unite = [&](int a, int b) { a = find(a); b = find(b); if (a != b) parent[a] = b; };
    auto idxOf = [&](Actor* a) -> int {
        if (!a) return -1;
        auto it = idToIdx.find(a->GetId());
        return it == idToIdx.end() ? -1 : it->second;
    };

    for (const auto& c : contacts) {
        if (c.isTrigger || c.penetration <= -ContactOffset) continue;   // only touching contacts support
        int ia = idxOf(c.actorA), ib = idxOf(c.actorB);
        if (ia >= 0 && ib >= 0) unite(ia, ib);
        else { if (ia >= 0) hasAnchor[(size_t)ia] = 1; if (ib >= 0) hasAnchor[(size_t)ib] = 1; }
    }

    // Aggregate island state.
    struct Island { uint8_t allLow = 1, anyDisturbed = 0, grounded = 0; float minTimer = 1e30f; };
    std::unordered_map<int, Island> islands;
    for (int i = 0; i < (int)rbs.size(); ++i) {
        Island& s = islands[find(i)];
        s.allLow       = s.allLow && lowMotion[(size_t)i];
        s.anyDisturbed = s.anyDisturbed || disturbed[(size_t)i];
        s.grounded     = s.grounded || hasAnchor[(size_t)i];
        s.minTimer     = std::min(s.minTimer, rbs[(size_t)i]->SleepTimer);
    }

    // Apply per body using its island's verdict.
    for (int i = 0; i < (int)rbs.size(); ++i) {
        const Island& s = islands[find(i)];
        RigidbodyComponent* rb = rbs[(size_t)i];
        bool canSleep = s.allLow && !s.anyDisturbed && s.grounded;
        if (canSleep) {
            rb->SleepTimer += dt;
            if (s.minTimer + dt >= SleepDelay) {        // slowest member has been quiet long enough
                rb->IsSleeping = true; rb->Velocity = {}; rb->AngularVelocity = {};
            } else {
                rb->IsSleeping = false;                 // still settling — keep the island awake together
            }
        } else {
            rb->IsSleeping = false;
            rb->SleepTimer = 0.f;
        }
    }
}

// ============================================================================
//  Sub-step
// ============================================================================

void PhysicsWorld::SubStep(SceneManager& scene, float dt) {
    // Refresh world transforms so detection, the solver and integration all see the same,
    // parent-resolved, scale-baked world state for this substep.
    scene.UpdateWorldTransforms();

    for (auto& ap : scene.GetActors()) {
        auto* rb  = ap->GetComponent<RigidbodyComponent>();
        auto* col = ap->GetComponent<ColliderComponent>();
        auto* tc  = ap->GetComponent<TransformComponent>();
        if (rb && col && tc) ComputeInertia(rb, col, tc);
    }

    IntegrateVelocities(scene, dt);

    std::vector<Contact> contacts;
    DetectCollisions(scene, dt, contacts);

    // Wake a sleeping body that is struck by an awake, movable body, so the impact is resolved
    // this substep. (Resting on static ground / on a sleeping island must NOT wake it — that is
    // why we only wake when the *other* body is an awake dynamic body.)
    for (auto& c : contacts) {
        if (c.isTrigger || c.penetration <= 0.f) continue;
        auto impact = [](RigidbodyComponent* s, RigidbodyComponent* o) {
            if (s && s->IsSleeping && o && !o->IsKinematic && o->Mass > 0.f && !o->IsSleeping)
                s->WakeUp();
        };
        impact(c.rbA, c.rbB);
        impact(c.rbB, c.rbA);
    }

    DispatchEvents(scene, contacts);

    for (auto& c : contacts) PreSolve(c, dt);
    for (int it = 0; it < SolverIterations; ++it)
        for (auto& c : contacts) SolveVelocity(c);

    IntegratePositions(scene, dt);
    CorrectPositions(contacts);
    UpdateSleep(scene, dt, contacts);
}

// ============================================================================
//  External-edit detection (editor gizmo / gameplay teleports)
// ============================================================================

void PhysicsWorld::SyncExternalEdits(SceneManager& scene) {
    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        auto* tc = ap->GetComponent<TransformComponent>();
        if (!rb || !tc || rb->IsKinematic || rb->Mass <= 0.f) continue;

        uint64_t id = ap->GetId();
        auto itP = m_lastPos.find(id);
        if (itP == m_lastPos.end()) continue;   // first time seen — nothing to compare against yet

        // Compare WORLD transforms (CachedWorld), refreshed by UpdateWorldTransforms before Step:
        // this also detects the case where a parent was moved (the child's local stays put but its
        // world changes), so a parented body wakes and falls when its support is dragged.
        bool moved = (tc->CachedWorld.Position - itP->second).LengthSquared() > 1e-10f;
        if (!moved) {
            auto itR = m_lastRot.find(id);
            if (itR != m_lastRot.end()) {
                const Quaternion& a = itR->second; const Quaternion& b = tc->CachedWorld.Rotation;
                float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
                if (std::fabsf(dot) < 0.9999f) moved = true;
            }
        }
        if (moved) { rb->WakeUp(); m_disturbed.insert(id); }   // editor moved it → fall again
    }
}

// ============================================================================
//  Public entry points
// ============================================================================

void PhysicsWorld::Step(SceneManager& scene, float dt) {
    m_queryScene = &scene;   // so post-Step spatial queries resolve proxy ids against this scene
    if (dt <= 0.f) return;
    dt = std::min(dt, FixedTimestep * (float)MaxSubSteps);    // avoid spiral of death

    m_hitThisFrame.clear();   // OnHit de-dup is per frame, not per substep

    // Wake bodies that were moved from outside physics since last frame, BEFORE stepping.
    SyncExternalEdits(scene);

    m_accumulator += dt;
    int steps = 0;
    while (m_accumulator >= FixedTimestep && steps < MaxSubSteps) {
        SubStep(scene, FixedTimestep);
        m_accumulator -= FixedTimestep;
        ++steps;
    }
    if (steps == MaxSubSteps) m_accumulator = 0.f;

    // Record the transforms physics produced so next frame can tell apart editor edits.
    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        auto* tc = ap->GetComponent<TransformComponent>();
        if (!rb || !tc) continue;
        m_lastPos[ap->GetId()] = tc->CachedWorld.Position;   // world-space, matches SyncExternalEdits
        m_lastRot[ap->GetId()] = tc->CachedWorld.Rotation;
    }
    m_disturbed.clear();
}

void PhysicsWorld::Reset(SceneManager& scene) {
    for (const auto& [key, en] : m_prevOverlaps) {
        (void)key;
        Actor* aA = scene.FindActorById(en.a), *aB = scene.FindActorById(en.b);
        if (!aA || !aB) continue;
        auto* cA = aA->GetComponent<ColliderComponent>();
        auto* cB = aB->GetComponent<ColliderComponent>();
        if (cA && cA->OnEndOverlap) cA->OnEndOverlap(aB);
        if (cB && cB->OnEndOverlap) cB->OnEndOverlap(aA);
    }
    m_prevOverlaps.clear();
    m_hitThisFrame.clear();
    m_accumulator = 0.f;
    m_lastPos.clear();
    m_lastRot.clear();
    m_disturbed.clear();
    m_broadphase.Clear();
    m_proxies.clear();

    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        if (!rb) continue;
        rb->Velocity = {}; rb->AngularVelocity = {};
        rb->IsSleeping = false; rb->SleepTimer = 0.f;
        rb->ClearAccumulators();
    }
}

// ============================================================================
//  Spatial queries
// ============================================================================

void PhysicsWorld::SyncQueries(SceneManager& scene) {
    m_queryScene = &scene;
    scene.UpdateWorldTransforms();
    std::unordered_set<uint64_t> seen;
    for (auto& ap : scene.GetActors()) {
        auto* col = ap->GetComponent<ColliderComponent>();
        auto* tc  = ap->GetComponent<TransformComponent>();
        if (!col || !tc) continue;
        uint64_t id = ap->GetId();
        seen.insert(id);
        AABB w = WorldAABB(tc, col);
        Aabb box(w.min, w.max);
        auto it = m_proxies.find(id);
        if (it == m_proxies.end()) m_proxies[id] = m_broadphase.CreateProxy(box, IdToUD(id));
        else                       m_broadphase.MoveProxy(it->second, box);
    }
    for (auto it = m_proxies.begin(); it != m_proxies.end(); ) {
        if (seen.count(it->first)) ++it;
        else { m_broadphase.DestroyProxy(it->second); it = m_proxies.erase(it); }
    }
}

bool PhysicsWorld::Raycast(const Vector3& origin, const Vector3& dir, float maxDistance,
                           RayHit& out, Actor* ignore) {
    out = RayHit{};
    Vector3 dn = dir.GetSafeNormal();
    if (dn.LengthSquared() < 0.5f) return false;          // degenerate direction

    float closest = maxDistance;
    bool  found   = false;
    m_broadphase.RayCast(origin, dn, maxDistance, [&](void* ud) {
        Actor* a = m_queryScene ? m_queryScene->FindActorById(UDToId(ud)) : nullptr;
        if (!a || a == ignore) return;   // a==nullptr ⇒ actor destroyed since the last Step/SyncQueries
        auto* col = a->GetComponent<ColliderComponent>();
        auto* tc  = a->GetComponent<TransformComponent>();
        if (!col || !tc) return;
        ShapeWS s = MakeShape(tc, col);
        float t = 0.f; Vector3 n;
        bool hit = false;
        switch (s.shape) {
        case ColliderShape::Sphere:  hit = RaySphere(origin, dn, s.center, s.radius, t, n); break;
        case ColliderShape::AABB: {   // oriented: transform the ray into the box's local frame
            Quaternion binv = s.rot.Inverse();
            Vector3 oL = binv * (origin - s.center), dL = binv * dn, nL;
            hit = RayAabb(oL, dL, Vector3{}, s.halfExtents, maxDistance, t, nL);
            if (hit) n = s.rot * nL;
            break;
        }
        case ColliderShape::Capsule: hit = RayCapsule(origin, dn, s.capBase, s.capTip, s.radius, t, n); break;
        }
        if (hit && t <= closest) {
            closest = t; found = true;
            out.Hit = true; out.HitActor = a; out.Distance = t;
            out.Point = origin + dn * t; out.Normal = n;
        }
    });
    return found;
}

bool PhysicsWorld::SphereCast(const Vector3& origin, float radius, const Vector3& dir,
                              float maxDistance, RayHit& out, Actor* ignore) {
    out = RayHit{};
    Vector3 dn = dir.GetSafeNormal();
    if (dn.LengthSquared() < 0.5f) return false;

    Vector3 endp = origin + dn * maxDistance;
    Vector3 rr(radius, radius, radius);
    Aabb sweptBox(Vector3(std::min(origin.x,endp.x), std::min(origin.y,endp.y), std::min(origin.z,endp.z)) - rr,
                  Vector3(std::max(origin.x,endp.x), std::max(origin.y,endp.y), std::max(origin.z,endp.z)) + rr);

    float closest = maxDistance;
    bool  found   = false;
    m_broadphase.QueryOverlap(sweptBox, [&](void* ud) {
        Actor* a = m_queryScene ? m_queryScene->FindActorById(UDToId(ud)) : nullptr;
        if (!a || a == ignore) return;
        auto* col = a->GetComponent<ColliderComponent>();
        auto* tc  = a->GetComponent<TransformComponent>();
        if (!col || !tc) return;
        ShapeWS s = MakeShape(tc, col);
        float t = 0.f; Vector3 n;
        bool hit = false;
        switch (s.shape) {                                 // sweep = ray vs shape grown by radius
        case ColliderShape::Sphere:  hit = RaySphere(origin, dn, s.center, s.radius + radius, t, n); break;
        case ColliderShape::AABB: {   // oriented: transform the ray into the box's local frame
            Quaternion binv = s.rot.Inverse();
            Vector3 oL = binv * (origin - s.center), dL = binv * dn, nL;
            hit = RayAabb(oL, dL, Vector3{}, s.halfExtents + rr, maxDistance, t, nL);
            if (hit) n = s.rot * nL;
            break;
        }
        case ColliderShape::Capsule: hit = RayCapsule(origin, dn, s.capBase, s.capTip, s.radius + radius, t, n); break;
        }
        if (hit && t <= closest) {
            closest = t; found = true;
            out.Hit = true; out.HitActor = a; out.Distance = t;
            out.Normal = n; out.Point = (origin + dn * t) - n * radius;   // contact on the real surface
        }
    });
    return found;
}

void PhysicsWorld::OverlapSphere(const Vector3& center, float radius,
                                 std::vector<Actor*>& results, Actor* ignore) {
    results.clear();
    Vector3 rr(radius, radius, radius);
    Aabb box(center - rr, center + rr);
    m_broadphase.QueryOverlap(box, [&](void* ud) {
        Actor* a = m_queryScene ? m_queryScene->FindActorById(UDToId(ud)) : nullptr;
        if (!a || a == ignore) return;
        auto* col = a->GetComponent<ColliderComponent>();
        auto* tc  = a->GetComponent<TransformComponent>();
        if (!col || !tc) return;
        ShapeWS s = MakeShape(tc, col);
        if (SphereOverlapsShape(center, radius, s)) results.push_back(a);
    });
}

} // namespace Fujin
