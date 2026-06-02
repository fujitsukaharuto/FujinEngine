#pragma once
/*
 * FujinEngine Physics – inspired by Unreal Engine 5 / Chaos Physics
 *
 * Design:
 *   • Fixed-timestep sub-stepping (configurable, default 1/60 s)
 *   • Sequential-impulse velocity solver (no warm-starting)
 *   • Baumgarte position correction (one correction per unique pair)
 *   • Threshold-based sleeping (bodies wake immediately on contact)
 *   • BVH broadphase rebuilt each step
 *   • Collision channels + Block / Overlap / Ignore response
 *   • OnHit / OnBeginOverlap / OnEndOverlap gameplay events
 */
#include "Engine/Math/Math.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Fujin {

class SceneManager;
class Actor;
class TransformComponent;
class RigidbodyComponent;
class ColliderComponent;

// ---------------------------------------------------------------------------
// Broadphase AABB
// ---------------------------------------------------------------------------
struct AABB { Vector3 min, max; };

// ---------------------------------------------------------------------------
// Contact
//   normal  : from B toward A  (separating direction for A)
//   point   : world-space contact point on B's surface
//   penetration: depth of overlap (> 0)
// ---------------------------------------------------------------------------
struct Contact {
    Actor*              actorA      = nullptr;
    Actor*              actorB      = nullptr;
    TransformComponent* tcA         = nullptr;
    TransformComponent* tcB         = nullptr;
    RigidbodyComponent* rbA         = nullptr;
    RigidbodyComponent* rbB         = nullptr;

    Vector3 normal      = {};
    float   penetration = 0.0f;
    Vector3 point       = {};
    bool    isTrigger   = false;

    // --- solver working data (filled by PreSolve) ---
    Vector3 rA            = {};      // point - posA
    Vector3 rB            = {};      // point - posB
    float   normalMass    = 0.0f;
    float   normalLambda  = 0.0f;   // accumulated (clamped >= 0)
    float   restitBias    = 0.0f;
    Vector3 tangent0      = {};
    float   tangentMass0  = 0.0f;
    float   tangentLambda0 = 0.0f;
    Vector3 tangent1      = {};
    float   tangentMass1  = 0.0f;
    float   tangentLambda1 = 0.0f;
};

// ---------------------------------------------------------------------------
// PhysicsWorld
// ---------------------------------------------------------------------------
class PhysicsWorld {
public:
    // ---- tuning knobs (public, editable at runtime) ----
    Vector3 Gravity          = { 0.0f, -9.81f, 0.0f };
    int     SolverIterations = 8;        // velocity iterations per substep
    float   FixedTimestep    = 1.0f / 120.0f; // finer step ⇒ less tunneling of fast / thin bodies
    int     MaxSubSteps      = 16;       // max substeps per game-frame (keeps clamp ≈ 0.13 s)

    // Sleep
    float LinearSleepThreshold  = 0.05f;
    float AngularSleepThreshold = 0.05f;
    float SleepDelay            = 0.5f;  // seconds below threshold before sleep

    void Step(SceneManager& scene, float dt);
    void Reset(SceneManager& scene);

private:
    float m_accumulator = 0.0f;

    // Overlap tracking (stores IDs, not raw pointers)
    struct OverlapEntry { uint64_t idA; uint64_t idB; };
    std::unordered_map<uint64_t, OverlapEntry> m_prevOverlaps;

    // BVH node pool (rebuilt every substep)
    struct BVHNode {
        AABB aabb;
        int  left = -1, right = -1, actorIdx = -1;
    };
    std::vector<BVHNode> m_bvhNodes;

    // ---- per-substep pipeline ----
    void SubStep(SceneManager& scene, float dt);
    void IntegrateVelocities(SceneManager& scene, float dt);
    void DetectCollisions(SceneManager& scene, std::vector<Contact>& out);
    void DispatchEvents(SceneManager& scene, const std::vector<Contact>& contacts);
    void PreSolve(Contact& c, float dt);
    void SolveNormal(Contact& c);
    void SolveFriction(Contact& c);
    void ApplyImpulse(Contact& c, const Vector3& dir, float lambda);
    void CorrectPosition(Contact& c);
    void IntegratePositions(SceneManager& scene, float dt);
    void UpdateSleep(SceneManager& scene, float dt,
                     const std::vector<Contact>& contacts);

    bool TestPair(Actor* a, Actor* b, std::vector<Contact>& out);

    // ---- inertia + broadphase helpers ----
    static void ComputeInertia(RigidbodyComponent* rb, ColliderComponent* col);
    static AABB GetWorldAABB(TransformComponent* tc, ColliderComponent* col);
    static bool AABBOverlap(const AABB& a, const AABB& b);
    static uint64_t PairKey(uint64_t a, uint64_t b);

    // ---- BVH ----
    int  BuildBVH(std::vector<std::pair<AABB,int>>& leaves, int begin, int end);
    void QueryBVHPairs(int na, int nb, std::vector<std::pair<int,int>>& out) const;

    // ---- narrow-phase (normal: B → A) ----
    static bool SphereSphere(
        const Vector3& cA, float rA, const Vector3& cB, float rB, Contact& c);

    static bool SphereAABB(
        const Vector3& sp, float r,
        const Vector3& bc, const Vector3& he, Contact& c);

    static bool SphereCapsule(
        const Vector3& sp, float sr,
        const Vector3& ca, const Vector3& cb, float cr, Contact& c);

    static bool AABBAABB(
        const Vector3& cA, const Vector3& hA,
        const Vector3& cB, const Vector3& hB,
        std::vector<Contact>& out, const Contact& base);

    static bool AABBCapsule(
        const Vector3& bc, const Vector3& he,
        const Vector3& ca, const Vector3& cb, float cr, Contact& c);

    static bool CapsuleCapsule(
        const Vector3& bA, const Vector3& tA, float rA,
        const Vector3& bB, const Vector3& tB, float rB, Contact& c);
};

} // namespace Fujin
