/*
 * PhysicsWorld.cpp
 * Unreal Engine 5 / Chaos Physics inspired implementation.
 *
 * Pipeline per substep:
 *   IntegrateVelocities      (forces → velocities, positions unchanged)
 *   DetectCollisions         (BVH broadphase + narrow-phase at CURRENT positions)
 *   DispatchEvents           (OnHit / OnBeginOverlap / OnEndOverlap)
 *   PreSolve                 (effective masses, tangent bases, restitution)
 *   Solve × N                (SolveNormal + SolveFriction)
 *   IntegratePositions       (corrected velocities → positions)
 *   CorrectPosition          (Baumgarte, one per unique pair)
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

// ============================================================================
//  Utility
// ============================================================================

uint64_t PhysicsWorld::PairKey(uint64_t a, uint64_t b) {
    if (a > b) std::swap(a, b);
    return a + b * b;   // Szudzik pairing, collision-free for b < 2^32
}

// ============================================================================
//  Geometry helpers
// ============================================================================

static Vector3 ClosestPtOnSegment(const Vector3& p,
                                   const Vector3& a, const Vector3& b)
{
    Vector3 ab = b - a;
    float   l2 = Vector3::Dot(ab, ab);
    if (l2 < 1e-12f) return a;
    float t = std::clamp(Vector3::Dot(p - a, ab) / l2, 0.0f, 1.0f);
    return a + ab * t;
}

static Vector3 ClosestPtOnAABB(const Vector3& p,
                                const Vector3& mn, const Vector3& mx)
{
    return { std::clamp(p.x, mn.x, mx.x),
             std::clamp(p.y, mn.y, mx.y),
             std::clamp(p.z, mn.z, mx.z) };
}

// Returns squared distance between closest points; writes them out.
static float ClosestSegSeg(
    const Vector3& p0, const Vector3& p1,
    const Vector3& q0, const Vector3& q1,
    Vector3& cp, Vector3& cq)
{
    Vector3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    float a = Vector3::Dot(d1,d1), e = Vector3::Dot(d2,d2);
    float f = Vector3::Dot(d2,r);
    float s, t;

    if (a < 1e-12f && e < 1e-12f) { s = t = 0.f; }
    else if (a < 1e-12f)          { s = 0.f; t = std::clamp(f/e,0.f,1.f); }
    else {
        float c = Vector3::Dot(d1,r);
        if (e < 1e-12f) { t = 0.f; s = std::clamp(-c/a,0.f,1.f); }
        else {
            float b = Vector3::Dot(d1,d2), den = a*e - b*b;
            if (den > 1e-12f) {
                s = std::clamp((b*f - c*e)/den, 0.f, 1.f);
            } else {
                // Parallel segments: use midpoint of overlapping region on p
                float t0 = Vector3::Dot(q0-p0,d1)/a;
                float t1 = Vector3::Dot(q1-p0,d1)/a;
                if (t0 > t1) std::swap(t0,t1);
                float lo = std::max(t0,0.f), hi = std::min(t1,1.f);
                s = (lo<=hi) ? std::clamp((lo+hi)*.5f,0.f,1.f) : 0.f;
            }
            t = (b*s+f)/e;
            if      (t < 0.f) { t=0.f; s=std::clamp(-c/a,    0.f,1.f); }
            else if (t > 1.f) { t=1.f; s=std::clamp((b-c)/a, 0.f,1.f); }
        }
    }
    cp = p0+d1*s; cq = q0+d2*t;
    Vector3 d = cp-cq;
    return Vector3::Dot(d,d);
}

static void CapsuleAxes(const TransformComponent* tc, const ColliderComponent* col,
                         Vector3& base, Vector3& tip)
{
    Matrix4x4 rot = tc->Rotation.ToMatrix();
    Vector3 up(rot.m[0][1], rot.m[1][1], rot.m[2][1]);
    Vector3 cen = tc->Position + col->Offset;
    base = cen - up * col->HalfHeight;
    tip  = cen + up * col->HalfHeight;
}

static float  V3Get(const Vector3& v, int i)
    { return i==0?v.x : i==1?v.y : v.z; }
static void   V3Set(Vector3& v, int i, float f)
    { if(i==0)v.x=f; else if(i==1)v.y=f; else v.z=f; }

// ============================================================================
//  Inertia
// ============================================================================

void PhysicsWorld::ComputeInertia(RigidbodyComponent* rb, ColliderComponent* col) {
    if (!rb || rb->IsKinematic || rb->Mass <= 0.f) {
        if (rb) rb->InvInertiaLocal = {};
        return;
    }
    float m = rb->Mass;
    Vector3 inv;
    switch (col->Shape) {
    case ColliderShape::Sphere: {
        float I = 0.4f*m*col->Radius*col->Radius;
        inv = {1.f/I,1.f/I,1.f/I}; break;
    }
    case ColliderShape::AABB: {
        float hx=col->HalfExtents.x, hy=col->HalfExtents.y, hz=col->HalfExtents.z;
        inv = { 3.f/(m*(hy*hy+hz*hz)),
                3.f/(m*(hx*hx+hz*hz)),
                3.f/(m*(hx*hx+hy*hy)) }; break;
    }
    case ColliderShape::Capsule: {
        float r=col->Radius, h=col->HalfHeight*2.f;
        float Iy  = 0.5f*m*r*r;
        float Ixz = m*(3.f*r*r+h*h)/12.f;
        inv = {1.f/Ixz, 1.f/Iy, 1.f/Ixz}; break;
    }
    }
    rb->InvInertiaLocal = inv;
}

// ============================================================================
//  Broadphase
// ============================================================================

AABB PhysicsWorld::GetWorldAABB(TransformComponent* tc, ColliderComponent* col) {
    Vector3 cen = tc->Position + col->Offset;
    switch (col->Shape) {
    case ColliderShape::Sphere: {
        float r = col->Radius;
        return { cen-Vector3(r,r,r), cen+Vector3(r,r,r) };
    }
    case ColliderShape::AABB:
        return { cen-col->HalfExtents, cen+col->HalfExtents };
    case ColliderShape::Capsule: {
        Matrix4x4 rot = tc->Rotation.ToMatrix();
        Vector3 up(rot.m[0][1],rot.m[1][1],rot.m[2][1]);
        Vector3 e0 = cen - up*col->HalfHeight, e1 = cen + up*col->HalfHeight;
        Vector3 r3(col->Radius,col->Radius,col->Radius);
        return { Vector3(std::min(e0.x,e1.x),std::min(e0.y,e1.y),std::min(e0.z,e1.z))-r3,
                 Vector3(std::max(e0.x,e1.x),std::max(e0.y,e1.y),std::max(e0.z,e1.z))+r3 };
    }
    }
    return {cen,cen};
}

bool PhysicsWorld::AABBOverlap(const AABB& a, const AABB& b) {
    return a.min.x<=b.max.x && a.max.x>=b.min.x
        && a.min.y<=b.max.y && a.max.y>=b.min.y
        && a.min.z<=b.max.z && a.max.z>=b.min.z;
}

// ============================================================================
//  BVH
// ============================================================================

static AABB UnionAABB(const AABB& a, const AABB& b) {
    return { Vector3(std::min(a.min.x,b.min.x),std::min(a.min.y,b.min.y),std::min(a.min.z,b.min.z)),
             Vector3(std::max(a.max.x,b.max.x),std::max(a.max.y,b.max.y),std::max(a.max.z,b.max.z)) };
}

int PhysicsWorld::BuildBVH(std::vector<std::pair<AABB,int>>& lv, int begin, int end) {
    if (begin+1 == end) {
        int idx = (int)m_bvhNodes.size();
        m_bvhNodes.push_back({ lv[(size_t)begin].first, -1,-1, lv[(size_t)begin].second });
        return idx;
    }
    AABB box = lv[(size_t)begin].first;
    for (int i=begin+1; i<end; ++i) box = UnionAABB(box, lv[(size_t)i].first);
    Vector3 sz = box.max - box.min;
    int ax = (sz.y>sz.x && sz.y>sz.z)?1 : (sz.z>sz.x)?2 : 0;
    int mid = (begin+end)/2;
    std::nth_element(lv.begin()+begin, lv.begin()+mid, lv.begin()+end,
        [ax](const std::pair<AABB,int>& a, const std::pair<AABB,int>& b){
            return (V3Get(a.first.min,ax)+V3Get(a.first.max,ax)) <
                   (V3Get(b.first.min,ax)+V3Get(b.first.max,ax));
        });
    int ni = (int)m_bvhNodes.size();
    m_bvhNodes.push_back({});
    int l = BuildBVH(lv, begin, mid);
    int r = BuildBVH(lv, mid,   end);
    m_bvhNodes[(size_t)ni] = { box, l, r, -1 };
    return ni;
}

void PhysicsWorld::QueryBVHPairs(int na, int nb,
                                  std::vector<std::pair<int,int>>& out) const
{
    if (na<0||nb<0) return;
    const BVHNode& a = m_bvhNodes[(size_t)na];
    const BVHNode& b = m_bvhNodes[(size_t)nb];
    if (!AABBOverlap(a.aabb,b.aabb)) return;

    bool al=(a.left<0), bl=(b.left<0);
    if (al&&bl) {
        if (a.actorIdx < b.actorIdx) out.push_back({a.actorIdx,b.actorIdx});
        return;
    }
    if (na==nb) {
        QueryBVHPairs(a.left, a.left,  out);
        QueryBVHPairs(a.right,a.right, out);
        QueryBVHPairs(a.left, a.right, out);
        return;
    }
    if (al){ QueryBVHPairs(na,b.left,out); QueryBVHPairs(na,b.right,out); }
    else if(bl){ QueryBVHPairs(a.left,nb,out); QueryBVHPairs(a.right,nb,out); }
    else {
        QueryBVHPairs(a.left, b.left, out); QueryBVHPairs(a.left, b.right, out);
        QueryBVHPairs(a.right,b.left, out); QueryBVHPairs(a.right,b.right, out);
    }
}

// ============================================================================
//  Narrow-phase tests
//  Convention: normal points FROM B TOWARD A (separating direction for A).
//  penetration > 0 means the shapes overlap.
// ============================================================================

bool PhysicsWorld::SphereSphere(
    const Vector3& cA, float rA,
    const Vector3& cB, float rB,
    Contact& c)
{
    Vector3 d = cA - cB;
    float d2  = Vector3::Dot(d,d), rs = rA+rB;
    if (d2 > rs*rs) return false;
    float dist    = std::sqrt(d2);
    c.normal      = (dist>1e-6f) ? d/dist : Vector3(0,1,0);
    c.penetration = rs - dist;
    c.point       = cB + c.normal*rB;          // on B's surface
    return true;
}

// sphere = A, box = B  →  normal from B toward A
bool PhysicsWorld::SphereAABB(
    const Vector3& sp, float r,
    const Vector3& bc, const Vector3& he,
    Contact& c)
{
    Vector3 mn=bc-he, mx=bc+he;
    Vector3 cl = ClosestPtOnAABB(sp,mn,mx);
    Vector3 d  = sp - cl;
    float   d2 = Vector3::Dot(d,d);
    if (d2 >= r*r) return false;

    if (d2 < 1e-12f) {
        // sphere centre is inside the box – push out through nearest face
        Vector3 dp = sp - bc;
        float ox=he.x-std::fabsf(dp.x), oy=he.y-std::fabsf(dp.y), oz=he.z-std::fabsf(dp.z);
        if      (ox<=oy && ox<=oz) c.normal = Vector3(dp.x>0?1.f:-1.f, 0,0);
        else if (oy<=oz)           c.normal = Vector3(0, dp.y>0?1.f:-1.f, 0);
        else                       c.normal = Vector3(0, 0, dp.z>0?1.f:-1.f);
        c.penetration = r + std::min({ox,oy,oz});
    } else {
        float dist    = std::sqrt(d2);
        c.normal      = d/dist;
        c.penetration = r - dist;
    }
    c.point = cl;   // on B's surface (box surface)
    return true;
}

// sphere = A, capsule = B  →  normal from B toward A
bool PhysicsWorld::SphereCapsule(
    const Vector3& sp, float sr,
    const Vector3& ca, const Vector3& cb, float cr,
    Contact& c)
{
    Vector3 cl  = ClosestPtOnSegment(sp, ca, cb);
    Vector3 d   = sp - cl;
    float   d2  = Vector3::Dot(d,d), rs = sr+cr;
    if (d2 > rs*rs) return false;
    float dist    = std::sqrt(d2);
    c.normal      = (dist>1e-6f) ? d/dist : Vector3(0,1,0);
    c.penetration = rs - dist;
    // Contact point is on the capsule surface (B), in the direction toward the sphere (A)
    c.point       = cl + c.normal*cr;
    return true;
}

// box A vs box B – single contact at centre of overlap face.
// Using the geometric centre gives rA = (0, dy, 0) for vertical contacts,
// which makes Cross(rA, normal) = 0 → no angular contribution → normalMass = 1/invM.
// This maximises the effective impulse per iteration and prevents pass-through.
bool PhysicsWorld::AABBAABB(
    const Vector3& cA, const Vector3& hA,
    const Vector3& cB, const Vector3& hB,
    std::vector<Contact>& out, const Contact& base)
{
    Vector3 d  = cA - cB;
    float ox = (hA.x+hB.x) - std::fabsf(d.x);
    float oy = (hA.y+hB.y) - std::fabsf(d.y);
    float oz = (hA.z+hB.z) - std::fabsf(d.z);
    if (ox<0.f||oy<0.f||oz<0.f) return false;

    Contact c = base;
    int ax;
    if (ox<=oy && ox<=oz) {
        ax=0; c.normal={d.x>0?1.f:-1.f,0,0}; c.penetration=ox;
    } else if (oy<=oz) {
        ax=1; c.normal={0,d.y>0?1.f:-1.f,0}; c.penetration=oy;
    } else {
        ax=2; c.normal={0,0,d.z>0?1.f:-1.f}; c.penetration=oz;
    }
    int a1=(ax+1)%3, a2=(ax+2)%3;

    // B's contact face along the contact axis
    float faceB = V3Get(cB,ax) + V3Get(c.normal,ax) * V3Get(hB,ax);

    // Single contact point: A's centre projected onto B's contact face, clamped to face bounds.
    // This ensures rA is parallel to the contact normal (zero cross product → no angular leakage).
    c.point = {};
    V3Set(c.point, ax, faceB);
    V3Set(c.point, a1, std::clamp(V3Get(cA,a1),
        V3Get(cB,a1)-V3Get(hB,a1), V3Get(cB,a1)+V3Get(hB,a1)));
    V3Set(c.point, a2, std::clamp(V3Get(cA,a2),
        V3Get(cB,a2)-V3Get(hB,a2), V3Get(cB,a2)+V3Get(hB,a2)));

    out.push_back(c);
    return true;
}

// box = A, capsule = B  (caller must flip normal for Capsule-A / AABB-B)
bool PhysicsWorld::AABBCapsule(
    const Vector3& bc, const Vector3& he,
    const Vector3& ca, const Vector3& cb, float cr,
    Contact& c)
{
    Vector3 mn=bc-he, mx=bc+he;

    // Find the closest point on the capsule axis to the AABB.
    // Bug with iterating from box CENTER: when the box sinks deep into the capsule
    // the centre projection lands at the axis midpoint, not the correct endpoint.
    // Fix: seed with BOTH capsule endpoints AND the box-centre projection, then
    // run a short iteration from each seed and keep the result with minimum distance.
    auto iterate = [&](Vector3 seed) -> Vector3 {
        for (int i=0; i<4; ++i) {
            Vector3 bp  = ClosestPtOnAABB(seed, mn, mx);
            Vector3 nxt = ClosestPtOnSegment(bp, ca, cb);
            if ((nxt-seed).LengthSquared() < 1e-10f) break;
            seed = nxt;
        }
        return seed;
    };

    // Seeds: the two capsule endpoints + the projection of box centre onto axis
    Vector3 seeds[3] = {
        ca,                                    // capsule base
        cb,                                    // capsule tip
        ClosestPtOnSegment(bc, ca, cb)         // projection of box centre
    };

    Vector3 bestCp = seeds[0];
    float   bestD2 = 1e30f;
    for (const auto& s : seeds) {
        Vector3 candidate = iterate(s);
        Vector3 box_pt    = ClosestPtOnAABB(candidate, mn, mx);
        float   d2        = (candidate - box_pt).LengthSquared();
        if (d2 < bestD2) { bestD2 = d2; bestCp = candidate; }
    }

    Contact tmp;
    if (!SphereAABB(bestCp, cr, bc, he, tmp)) return false;

    // SphereAABB(sphere=capsule_axis_pt, box=AABB) → normal from box(A) toward capsule(B).
    // We need normal from capsule(B) toward box(A), so flip once.
    c.normal      = -tmp.normal;
    c.penetration = tmp.penetration;
    // Contact point on B's (capsule) surface, toward A (box).
    c.point       = bestCp + c.normal * cr;
    return true;
}

// capsule A vs capsule B
bool PhysicsWorld::CapsuleCapsule(
    const Vector3& bA, const Vector3& tA, float rA,
    const Vector3& bB, const Vector3& tB, float rB,
    Contact& c)
{
    Vector3 clA, clB;
    float d2 = ClosestSegSeg(bA,tA, bB,tB, clA,clB);
    float rs = rA+rB;
    if (d2 > rs*rs) return false;
    float dist    = std::sqrt(d2);
    Vector3 dv    = clA - clB;
    c.normal      = (dist>1e-6f) ? dv/dist : Vector3(0,1,0);
    c.penetration = rs - dist;
    c.point       = clB + c.normal*rB;   // on B's surface
    return true;
}

// ============================================================================
//  Channel filter
// ============================================================================

static CollisionResponse CombinedResponse(const ColliderComponent* a,
                                           const ColliderComponent* b)
{
    return std::min(a->GetResponseTo(b->Channel),
                    b->GetResponseTo(a->Channel));
}

// ============================================================================
//  Pair dispatch
// ============================================================================

bool PhysicsWorld::TestPair(Actor* a, Actor* b, std::vector<Contact>& out) {
    auto* tcA  = a->GetComponent<TransformComponent>();
    auto* tcB  = b->GetComponent<TransformComponent>();
    auto* colA = a->GetComponent<ColliderComponent>();
    auto* colB = b->GetComponent<ColliderComponent>();
    if (!tcA||!tcB||!colA||!colB) return false;

    CollisionResponse resp = CombinedResponse(colA, colB);
    if (resp == CollisionResponse::Ignore) return false;

    Contact base;
    base.actorA = a;  base.actorB = b;
    base.tcA    = tcA; base.tcB   = tcB;
    base.rbA    = a->GetComponent<RigidbodyComponent>();
    base.rbB    = b->GetComponent<RigidbodyComponent>();
    base.isTrigger = (colA->IsTrigger || colB->IsTrigger ||
                      resp==CollisionResponse::Overlap);

    auto shA = colA->Shape, shB = colB->Shape;
    Vector3 pA = tcA->Position+colA->Offset, pB = tcB->Position+colB->Offset;
    size_t before = out.size();

    if (shA==ColliderShape::Sphere && shB==ColliderShape::Sphere) {
        Contact c=base;
        if (SphereSphere(pA,colA->Radius, pB,colB->Radius, c)) out.push_back(c);
    }
    else if (shA==ColliderShape::AABB && shB==ColliderShape::AABB) {
        AABBAABB(pA,colA->HalfExtents, pB,colB->HalfExtents, out, base);
    }
    else if (shA==ColliderShape::Sphere && shB==ColliderShape::AABB) {
        Contact c=base;
        // sphere=A, box=B → normal B→A ✓
        if (SphereAABB(pA,colA->Radius, pB,colB->HalfExtents, c)) out.push_back(c);
    }
    else if (shA==ColliderShape::AABB && shB==ColliderShape::Sphere) {
        Contact c=base;
        // Call with sphere=B, box=A → normal A→B; flip to get B→A
        if (SphereAABB(pB,colB->Radius, pA,colA->HalfExtents, c)) {
            c.normal=-c.normal; out.push_back(c);
        }
    }
    else if (shA==ColliderShape::Capsule && shB==ColliderShape::Capsule) {
        Vector3 bA,tA,bB,tB;
        CapsuleAxes(tcA,colA,bA,tA); CapsuleAxes(tcB,colB,bB,tB);
        Contact c=base;
        if (CapsuleCapsule(bA,tA,colA->Radius, bB,tB,colB->Radius, c)) out.push_back(c);
    }
    else if (shA==ColliderShape::Sphere && shB==ColliderShape::Capsule) {
        Vector3 bB,tB; CapsuleAxes(tcB,colB,bB,tB);
        Contact c=base;
        // sphere=A, capsule=B → normal B→A ✓
        if (SphereCapsule(pA,colA->Radius, bB,tB,colB->Radius, c)) out.push_back(c);
    }
    else if (shA==ColliderShape::Capsule && shB==ColliderShape::Sphere) {
        Vector3 bA,tA; CapsuleAxes(tcA,colA,bA,tA);
        Contact c=base;
        // sphere=B, capsule=A → normal A→B; flip
        if (SphereCapsule(pB,colB->Radius, bA,tA,colA->Radius, c)) {
            c.normal=-c.normal; out.push_back(c);
        }
    }
    else if (shA==ColliderShape::AABB && shB==ColliderShape::Capsule) {
        Vector3 bB,tB; CapsuleAxes(tcB,colB,bB,tB);
        Contact c=base;
        // box=A, capsule=B → AABBCapsule returns normal B→A ✓
        if (AABBCapsule(pA,colA->HalfExtents, bB,tB,colB->Radius, c)) out.push_back(c);
    }
    else if (shA==ColliderShape::Capsule && shB==ColliderShape::AABB) {
        Vector3 bA,tA; CapsuleAxes(tcA,colA,bA,tA);
        Contact c=base;
        // box=B, capsule=A → normal A→B; flip to B→A
        if (AABBCapsule(pB,colB->HalfExtents, bA,tA,colA->Radius, c)) {
            c.normal=-c.normal; out.push_back(c);
        }
    }

    return out.size() > before;
}

// ============================================================================
//  Event dispatch
// ============================================================================

void PhysicsWorld::DispatchEvents(SceneManager& scene,
                                   const std::vector<Contact>& contacts)
{
    std::unordered_map<uint64_t,OverlapEntry> cur;

    for (const auto& c : contacts) {
        auto* colA = c.actorA->GetComponent<ColliderComponent>();
        auto* colB = c.actorB->GetComponent<ColliderComponent>();

        if (!c.isTrigger) {
            if (colA && colA->OnHit) { HitResult hr{c.actorB, c.normal,  c.point,c.penetration}; colA->OnHit(hr); }
            if (colB && colB->OnHit) { HitResult hr{c.actorA,-c.normal,  c.point,c.penetration}; colB->OnHit(hr); }
        } else {
            uint64_t key = PairKey(c.actorA->GetId(), c.actorB->GetId());
            if (cur.find(key)==cur.end()) {
                cur[key] = {c.actorA->GetId(), c.actorB->GetId()};
                if (m_prevOverlaps.find(key)==m_prevOverlaps.end()) {
                    if (colA&&colA->OnBeginOverlap) colA->OnBeginOverlap(c.actorB);
                    if (colB&&colB->OnBeginOverlap) colB->OnBeginOverlap(c.actorA);
                }
            }
        }
    }
    for (const auto& [key,en] : m_prevOverlaps) {
        if (cur.find(key)==cur.end()) {
            Actor* aA=scene.FindActorById(en.idA), *aB=scene.FindActorById(en.idB);
            if (!aA||!aB) continue;
            auto* cA=aA->GetComponent<ColliderComponent>();
            auto* cB=aB->GetComponent<ColliderComponent>();
            if (cA&&cA->OnEndOverlap) cA->OnEndOverlap(aB);
            if (cB&&cB->OnEndOverlap) cB->OnEndOverlap(aA);
        }
    }
    m_prevOverlaps = std::move(cur);
}

// ============================================================================
//  Sequential-impulse solver
// ============================================================================

void PhysicsWorld::PreSolve(Contact& c, float dt) {
    c.rA = c.point - c.tcA->Position;
    c.rB = c.point - c.tcB->Position;

    // Effective inverse mass (0 if static / kinematic)
    auto invM = [](const RigidbodyComponent* rb) {
        return (rb && !rb->IsKinematic && rb->Mass>0.f) ? 1.f/rb->Mass : 0.f;
    };
    // Angular contribution along an axis
    auto angIM = [&](const RigidbodyComponent* rb, const TransformComponent* tc,
                     const Vector3& r, const Vector3& n) {
        if (!rb || rb->IsKinematic || rb->Mass<=0.f) return 0.f;
        Vector3 rn = Vector3::Cross(r,n);
        return Vector3::Dot(rn, rb->ApplyInvInertiaWorld(tc->Rotation, rn));
    };

    float iMA=invM(c.rbA), iMB=invM(c.rbB);

    // Normal effective mass
    float invN = iMA+iMB + angIM(c.rbA,c.tcA,c.rA,c.normal)
                         + angIM(c.rbB,c.tcB,c.rB,c.normal);
    c.normalMass = (invN>1e-10f) ? 1.f/invN : 0.f;

    // Relative velocity along normal
    Vector3 vA=c.rbA?c.rbA->Velocity:Vector3{}, vB=c.rbB?c.rbB->Velocity:Vector3{};
    Vector3 wA=c.rbA?c.rbA->AngularVelocity:Vector3{}, wB=c.rbB?c.rbB->AngularVelocity:Vector3{};
    float vn = Vector3::Dot((vA+Vector3::Cross(wA,c.rA))-(vB+Vector3::Cross(wB,c.rB)), c.normal);

    (void)dt;
    // IMPORTANT: penetration is resolved positionally in CorrectPosition (NGS-style projection).
    // We deliberately do NOT add a Baumgarte bias velocity (beta/dt * penetration) here.
    // Doing so feeds REAL kinetic energy into the body proportional to penetration depth, so any
    // deep contact (fast fall, round point-contact, or one-substep tunnel-in) launches the body
    // ("明らかにおかしく吹き飛ぶ"). The velocity solver below only enforces non-penetration of
    // the relative velocity plus restitution; depenetration is handled by CorrectPosition.

    // Restitution: only for fast impacts to suppress micro-bouncing at rest
    float e = 0.3f;
    if (c.rbA) e=std::min(e,c.rbA->Restitution);
    if (c.rbB) e=std::min(e,c.rbB->Restitution);
    c.restitBias = (vn < -1.f) ? -e*vn : 0.f;

    // Tangent basis (2 axes perpendicular to normal)
    const Vector3& n = c.normal;
    if (std::fabsf(n.x)<=std::fabsf(n.y) && std::fabsf(n.x)<=std::fabsf(n.z))
        c.tangent0 = Vector3::Cross(n,{1,0,0}).GetSafeNormal();
    else if (std::fabsf(n.y)<=std::fabsf(n.z))
        c.tangent0 = Vector3::Cross(n,{0,1,0}).GetSafeNormal();
    else
        c.tangent0 = Vector3::Cross(n,{0,0,1}).GetSafeNormal();
    c.tangent1 = Vector3::Cross(n,c.tangent0).GetSafeNormal();

    auto tMass = [&](const Vector3& t) {
        float inv = iMA+iMB + angIM(c.rbA,c.tcA,c.rA,t)
                            + angIM(c.rbB,c.tcB,c.rB,t);
        return (inv>1e-10f) ? 1.f/inv : 0.f;
    };
    c.tangentMass0 = tMass(c.tangent0);
    c.tangentMass1 = tMass(c.tangent1);

    c.normalLambda = c.tangentLambda0 = c.tangentLambda1 = 0.f;
}

void PhysicsWorld::ApplyImpulse(Contact& c, const Vector3& dir, float lam) {
    if (lam==0.f) return;
    if (c.rbA && !c.rbA->IsKinematic && c.rbA->Mass>0.f) {
        c.rbA->Velocity        += dir*(lam/c.rbA->Mass);
        c.rbA->AngularVelocity += c.rbA->ApplyInvInertiaWorld(
            c.tcA->Rotation, Vector3::Cross(c.rA, dir*lam));
    }
    if (c.rbB && !c.rbB->IsKinematic && c.rbB->Mass>0.f) {
        c.rbB->Velocity        -= dir*(lam/c.rbB->Mass);
        c.rbB->AngularVelocity -= c.rbB->ApplyInvInertiaWorld(
            c.tcB->Rotation, Vector3::Cross(c.rB, dir*lam));
    }
}

void PhysicsWorld::SolveNormal(Contact& c) {
    if (c.isTrigger || c.normalMass<=0.f) return;
    Vector3 vA=c.rbA?c.rbA->Velocity:Vector3{}, vB=c.rbB?c.rbB->Velocity:Vector3{};
    Vector3 wA=c.rbA?c.rbA->AngularVelocity:Vector3{}, wB=c.rbB?c.rbB->AngularVelocity:Vector3{};
    float vn  = Vector3::Dot((vA+Vector3::Cross(wA,c.rA))-(vB+Vector3::Cross(wB,c.rB)), c.normal);
    float lam = (c.restitBias - vn) * c.normalMass;
    float lNew= std::max(c.normalLambda + lam, 0.f);
    ApplyImpulse(c, c.normal, lNew - c.normalLambda);
    c.normalLambda = lNew;
}

void PhysicsWorld::SolveFriction(Contact& c) {
    if (c.isTrigger || c.normalLambda<=0.f) return;
    float mu = 0.5f;
    if (c.rbA) mu=std::min(mu,c.rbA->Friction);
    if (c.rbB) mu=std::min(mu,c.rbB->Friction);
    float maxF = mu * c.normalLambda;

    // Helper for one friction axis
    auto solve = [&](const Vector3& t, float mass, float& acc){
        Vector3 vA=c.rbA?c.rbA->Velocity:Vector3{}, vB=c.rbB?c.rbB->Velocity:Vector3{};
        Vector3 wA=c.rbA?c.rbA->AngularVelocity:Vector3{}, wB=c.rbB?c.rbB->AngularVelocity:Vector3{};
        float vt  = Vector3::Dot((vA+Vector3::Cross(wA,c.rA))-(vB+Vector3::Cross(wB,c.rB)), t);
        float lam = -vt * mass;
        float lNew= std::clamp(acc+lam, -maxF, maxF);
        ApplyImpulse(c, t, lNew-acc);
        acc = lNew;
    };
    solve(c.tangent0, c.tangentMass0, c.tangentLambda0);
    solve(c.tangent1, c.tangentMass1, c.tangentLambda1);
}

void PhysicsWorld::CorrectPosition(Contact& c) {
    if (c.isTrigger) return;
    auto invM=[](const RigidbodyComponent* rb){
        return (rb&&!rb->IsKinematic&&rb->Mass>0.f)?1.f/rb->Mass:0.f; };
    float iA=invM(c.rbA), iB=invM(c.rbB), tot=iA+iB;
    if (tot<1e-12f) return;
    // Positional depenetration (NGS). Clamp the per-substep correction: a single very deep
    // contact must not teleport a body, otherwise it spawns a fresh deep collision next step
    // and the pair oscillates/explodes. beta<1 leaves a hair of penetration for stability.
    const float slop = 0.001f, beta = 0.8f, maxCorrection = 0.2f;
    float mag = std::min(std::max(c.penetration - slop, 0.f) * beta, maxCorrection) / tot;
    Vector3 corr = c.normal*mag;
    if (iA>0.f) c.tcA->Position += corr*iA;
    if (iB>0.f) c.tcB->Position -= corr*iB;
}

// ============================================================================
//  Integration
// ============================================================================

void PhysicsWorld::IntegrateVelocities(SceneManager& scene, float dt) {
    for (auto& ap : scene.GetActors()) {
        auto* rb=ap->GetComponent<RigidbodyComponent>();
        auto* tc=ap->GetComponent<TransformComponent>();
        if (!rb||!tc||rb->IsKinematic||rb->Mass<=0.f||rb->IsSleeping) continue;

        if (rb->UseGravity) rb->AddForce(Gravity*rb->Mass);
        rb->Velocity        += rb->m_forceAccum*(dt/rb->Mass);
        rb->Velocity        *= std::max(0.f, 1.f-rb->LinearDamping*dt);
        rb->AngularVelocity += rb->ApplyInvInertiaWorld(tc->Rotation,rb->m_torqueAccum)*dt;
        rb->AngularVelocity *= std::max(0.f, 1.f-rb->AngularDamping*dt);
        rb->ClearAccumulators();
    }
}

void PhysicsWorld::IntegratePositions(SceneManager& scene, float dt) {
    for (auto& ap : scene.GetActors()) {
        auto* rb=ap->GetComponent<RigidbodyComponent>();
        auto* tc=ap->GetComponent<TransformComponent>();
        if (!rb||!tc||rb->IsKinematic||rb->Mass<=0.f||rb->IsSleeping) continue;

        tc->Position += rb->Velocity*dt;
        float wl = rb->AngularVelocity.Length();
        if (wl>1e-6f) {
            Quaternion dq = Quaternion::FromAxisAngle(rb->AngularVelocity/wl, wl*dt);
            tc->Rotation  = dq * tc->Rotation;
            tc->Rotation.Normalize();
        }
    }
}

// ============================================================================
//  Collision detection
// ============================================================================

void PhysicsWorld::DetectCollisions(SceneManager& scene, std::vector<Contact>& out) {
    auto& actors = scene.GetActors();
    size_t n = actors.size();
    if (n<2) return;

    std::vector<std::pair<AABB,int>> lv;
    lv.reserve(n);
    for (size_t i=0; i<n; ++i) {
        auto* col=actors[i]->GetComponent<ColliderComponent>();
        auto* tc =actors[i]->GetComponent<TransformComponent>();
        if (!col||!tc) continue;
        lv.push_back({GetWorldAABB(tc,col),(int)i});
    }
    if (lv.size()<2) return;

    m_bvhNodes.clear();
    m_bvhNodes.reserve(lv.size()*2);
    int root = BuildBVH(lv, 0, (int)lv.size());

    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(lv.size()*2);
    QueryBVHPairs(root, root, pairs);

    for (auto& [ai,bi] : pairs)
        TestPair(actors[(size_t)ai].get(), actors[(size_t)bi].get(), out);
}

// ============================================================================
//  Sleep
// ============================================================================

void PhysicsWorld::UpdateSleep(SceneManager& scene, float dt,
                                const std::vector<Contact>& contacts)
{
    // Bodies in contact with non-sleeping bodies stay awake.
    // (actual wake-before-solve is handled in SubStep; this just
    //  prevents going back to sleep within a contact frame)
    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        if (!rb || rb->IsKinematic || rb->Mass<=0.f) continue;
        if (rb->IsSleeping) continue; // already asleep

        float lv = rb->Velocity.Length();
        float av = rb->AngularVelocity.Length();
        if (lv < LinearSleepThreshold && av < AngularSleepThreshold) {
            rb->SleepTimer += dt;
            if (rb->SleepTimer >= SleepDelay) {
                rb->IsSleeping      = true;
                rb->Velocity        = {};
                rb->AngularVelocity = {};
            }
        } else {
            rb->SleepTimer = 0.f;
        }
    }
    // Contacts: keep actors awake if they have a non-trigger contact
    for (const auto& c : contacts) {
        if (c.isTrigger) continue;
        if (c.rbA && c.rbA->IsSleeping) c.rbA->WakeUp();
        if (c.rbB && c.rbB->IsSleeping) c.rbB->WakeUp();
    }
}

// ============================================================================
//  Sub-step
// ============================================================================

void PhysicsWorld::SubStep(SceneManager& scene, float dt) {
    // Recompute inertia (mass / shape may have changed)
    for (auto& ap : scene.GetActors()) {
        auto* rb=ap->GetComponent<RigidbodyComponent>();
        auto* col=ap->GetComponent<ColliderComponent>();
        if (rb&&col) ComputeInertia(rb,col);
    }

    // ── Correct pipeline: Detect (current pos) → Solve → Integrate ──────────

    // 1. Apply forces and integrate velocities (positions unchanged)
    IntegrateVelocities(scene, dt);

    // 2. Detect at current positions
    std::vector<Contact> contacts;
    DetectCollisions(scene, contacts);

    // 3. Wake sleeping bodies that have a blocking contact this frame
    //    (must happen BEFORE PreSolve so their mass enters the calculation
    //     and they integrate correctly this frame)
    for (auto& c : contacts) {
        if (c.isTrigger) continue;
        if (c.rbA && c.rbA->IsSleeping) c.rbA->WakeUp();
        if (c.rbB && c.rbB->IsSleeping) c.rbB->WakeUp();
    }

    // 4. Dispatch gameplay events
    DispatchEvents(scene, contacts);

    // 5. Pre-solve (effective masses, tangent basis, restitution)
    for (auto& c : contacts) PreSolve(c, dt);

    // 6. Sequential-impulse iterations
    for (int i=0; i<SolverIterations; ++i) {
        for (auto& c : contacts) SolveNormal(c);
        for (auto& c : contacts) SolveFriction(c);
    }

    // 7. Integrate positions with corrected velocities
    IntegratePositions(scene, dt);

    // 8. Baumgarte position correction – one per unique pair (deepest contact)
    {
        std::unordered_map<uint64_t,Contact*> deep;
        for (auto& c : contacts) {
            if (c.isTrigger) continue;
            uint64_t k = PairKey(c.actorA->GetId(), c.actorB->GetId());
            auto it = deep.find(k);
            if (it==deep.end() || c.penetration > it->second->penetration)
                deep[k] = &c;
        }
        for (auto& [k,cp] : deep) { (void)k; CorrectPosition(*cp); }
    }

    // 9. Sleep update
    UpdateSleep(scene, dt, contacts);
}

// ============================================================================
//  Public entry point
// ============================================================================

void PhysicsWorld::Step(SceneManager& scene, float dt) {
    if (dt <= 0.f) return;
    // Clamp to avoid spiral-of-death on very slow frames
    dt = std::min(dt, FixedTimestep * (float)MaxSubSteps);

    m_accumulator += dt;
    int steps = 0;
    while (m_accumulator >= FixedTimestep && steps < MaxSubSteps) {
        SubStep(scene, FixedTimestep);
        m_accumulator -= FixedTimestep;
        ++steps;
    }
    if (steps == MaxSubSteps) m_accumulator = 0.f; // drain leftover
}

// ============================================================================
//  Reset
// ============================================================================

void PhysicsWorld::Reset(SceneManager& scene) {
    // Fire end-overlap events before clearing state
    for (const auto& [key,en] : m_prevOverlaps) {
        Actor* aA=scene.FindActorById(en.idA), *aB=scene.FindActorById(en.idB);
        if (!aA||!aB) continue;
        auto* cA=aA->GetComponent<ColliderComponent>();
        auto* cB=aB->GetComponent<ColliderComponent>();
        if (cA&&cA->OnEndOverlap) cA->OnEndOverlap(aB);
        if (cB&&cB->OnEndOverlap) cB->OnEndOverlap(aA);
    }
    m_prevOverlaps.clear();
    m_accumulator = 0.f;

    for (auto& ap : scene.GetActors()) {
        auto* rb = ap->GetComponent<RigidbodyComponent>();
        if (!rb) continue;
        rb->Velocity        = {};
        rb->AngularVelocity = {};
        rb->IsSleeping      = false;
        rb->SleepTimer      = 0.f;
        rb->ClearAccumulators();
    }
}

} // namespace Fujin
