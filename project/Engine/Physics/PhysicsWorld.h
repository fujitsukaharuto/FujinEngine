#pragma once
/*
 * FujinEngine — rigid-body physics (clean rewrite, Unreal/Chaos-inspired)
 *
 * The two ideas that make this robust:
 *
 *   1. SPECULATIVE CONTACTS (Chaos / Box2D "speculative" margins).
 *      A contact is created not only when shapes already overlap, but also when they are
 *      within a small margin OR are about to touch this substep (margin grows with closing
 *      speed). The velocity solver then allows a body to approach only fast enough to *just*
 *      reach the surface — never to pass through it and never to bury itself deep.
 *        → no tunneling (for any pair the broadphase catches)
 *        → no deep penetration ⇒ no violent "fly off" on impact
 *        → bodies come to rest exactly on the surface, not at the centre.
 *
 *   2. POSITIONAL DEPENETRATION (NGS projection), never a Baumgarte bias velocity.
 *      Any residual penetration is removed by moving the transforms, which injects no
 *      kinetic energy. The velocity solver only ever removes approaching velocity + bounce.
 *
 * Plus: fixed-timestep substepping, velocity-swept broadphase, sequential-impulse solver
 * (accumulated normal impulse + Coulomb friction), box/box 4-point manifolds for stable
 * stacking, collision channels (Block/Overlap/Ignore), OnHit/OnBeginOverlap/OnEndOverlap,
 * and threshold sleeping.
 *
 * Conventions:
 *   • Contact normal points FROM B TOWARD A (the separating direction for A).
 *   • Contact::penetration > 0  ⇒ shapes overlap by that much.
 *     Contact::penetration < 0  ⇒ a speculative gap of |penetration| still remains.
 *   • A collider with no RigidbodyComponent (or kinematic / mass<=0) is immovable.
 */
#include "Engine/Math/Math.h"
#include "Engine/Spatial/Bvh.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Fujin {

class SceneManager;
class Actor;
class TransformComponent;
class RigidbodyComponent;
class ColliderComponent;

// World-space axis-aligned bounding box (broadphase).
struct AABB { Vector3 min, max; };

// A single contact constraint between two colliders.
struct Contact {
    Actor*              actorA = nullptr;
    Actor*              actorB = nullptr;
    TransformComponent* tcA    = nullptr;
    TransformComponent* tcB    = nullptr;
    RigidbodyComponent* rbA    = nullptr;
    RigidbodyComponent* rbB    = nullptr;

    Vector3 normal      = {};    // from B toward A
    float   penetration = 0.0f;  // >0 overlap, <0 speculative gap
    Vector3 point       = {};    // world-space contact point
    bool    isTrigger   = false;

    Vector3 comA = {}, comB = {}; // world-space centre (collider centre) of each body

    // --- solver scratch (filled by PreSolve) ---
    Vector3 rA = {}, rB = {};
    float   normalMass    = 0.0f;
    float   normalImpulse = 0.0f;   // accumulated, clamped >= 0
    float   velocityBias  = 0.0f;   // target relative normal velocity (restitution / speculative)
    Vector3 tangent0 = {}, tangent1 = {};
    float   tangent0Mass = 0.0f, tangent1Mass = 0.0f;
    float   tangent0Impulse = 0.0f, tangent1Impulse = 0.0f;
};

// Result of a ray / sweep query.
struct RayHit {
    bool    Hit      = false;
    Actor*  HitActor = nullptr;
    Vector3 Point    = {};     // world contact point on the hit surface
    Vector3 Normal   = {};     // surface normal at the hit (points back toward the ray)
    float   Distance = 0.0f;   // distance along the (normalised) ray direction
};

class PhysicsWorld {
public:
    // ---- public tuning knobs ----
    Vector3 Gravity          = { 0.0f, -9.81f, 0.0f };
    int     SolverIterations = 8;
    float   FixedTimestep    = 1.0f / 120.0f;
    int     MaxSubSteps      = 16;

    float   ContactOffset    = 0.01f;   // speculative margin (m): create contacts within this gap

    // Positional depenetration
    float   Baumgarte     = 0.8f;       // fraction of penetration removed per substep
    float   Slop          = 0.001f;     // penetration left untouched (1 mm) to avoid jitter
    float   MaxCorrection = 0.2f;       // max metres moved per substep by depenetration

    // Restitution / sleeping
    float   RestitutionThreshold  = 0.5f;  // min approach speed (m/s) before a bounce is applied
    float   LinearSleepThreshold  = 0.05f;
    float   AngularSleepThreshold = 0.05f;
    float   SleepDelay            = 0.5f;

    // ---- public API (kept stable for main.cpp / editor) ----
    void Step(SceneManager& scene, float dt);
    void Reset(SceneManager& scene);

    // ---- spatial queries ----
    // Run against the broadphase as of the last Step(); for edit-mode or after manual moves,
    // call SyncQueries(scene) first to refresh it from current collider positions.
    void SyncQueries(SceneManager& scene);

    // Closest hit of a ray (origin + dir*t, t in [0, maxDistance]); dir need not be normalised.
    bool Raycast(const Vector3& origin, const Vector3& dir, float maxDistance,
                 RayHit& out, Actor* ignore = nullptr);
    // Sweep a sphere of `radius` along the ray; reports the first time of impact.
    bool SphereCast(const Vector3& origin, float radius, const Vector3& dir, float maxDistance,
                    RayHit& out, Actor* ignore = nullptr);
    // All colliders overlapping a sphere.
    void OverlapSphere(const Vector3& center, float radius,
                       std::vector<Actor*>& results, Actor* ignore = nullptr);

private:
    float m_accumulator = 0.0f;

    // Overlap (trigger) tracking across frames — stores actor ids, never raw pointers.
    struct OverlapPair { uint64_t a, b; };
    std::unordered_map<uint64_t, OverlapPair> m_prevOverlaps;

    // Pairs that already fired OnHit this frame, so a frame that runs several substeps reports
    // OnHit once per pair (not once per substep). Cleared at the start of each Step().
    std::unordered_set<uint64_t> m_hitThisFrame;

    // Persistent broadphase: a BVH of collider proxies (actor id → proxy id).
    // Proxy userData holds the actor *id* (not a raw Actor*), resolved against the live scene at
    // query time so a destroyed actor is skipped instead of dereferenced (no use-after-free).
    Bvh                               m_broadphase;
    std::unordered_map<uint64_t, int> m_proxies;
    SceneManager*                     m_queryScene = nullptr;  // scene backing the broadphase proxies

    // Sleep bookkeeping: the transform physics last produced (to detect external/editor edits),
    // and the set of bodies disturbed by such an edit this frame.
    std::unordered_map<uint64_t, Vector3>    m_lastPos;
    std::unordered_map<uint64_t, Quaternion> m_lastRot;
    std::unordered_set<uint64_t>             m_disturbed;

    // Wake any body whose transform was changed from outside physics (editor gizmo, gameplay).
    void SyncExternalEdits(SceneManager& scene);

    // ---- per-substep pipeline ----
    void SubStep(SceneManager& scene, float dt);
    void IntegrateVelocities(SceneManager& scene, float dt);
    void IntegratePositions(SceneManager& scene, float dt);
    void DetectCollisions(SceneManager& scene, float dt, std::vector<Contact>& out);
    void DispatchEvents(SceneManager& scene, const std::vector<Contact>& contacts);
    void PreSolve(Contact& c, float dt);
    void SolveVelocity(Contact& c);
    void ApplyImpulse(Contact& c, const Vector3& dir, float lambda);
    void CorrectPositions(std::vector<Contact>& contacts);
    void UpdateSleep(SceneManager& scene, float dt, const std::vector<Contact>& contacts);

    bool TestPair(Actor* a, Actor* b, float dt, std::vector<Contact>& out);

    // ---- helpers ----
    static void     ComputeInertia(RigidbodyComponent* rb, ColliderComponent* col, const TransformComponent* tc);
    static AABB     WorldAABB(TransformComponent* tc, ColliderComponent* col);
    static bool     Overlap(const AABB& a, const AABB& b);
    static uint64_t PairKey(uint64_t a, uint64_t b);

    // ---- narrow-phase primitives ----
    //   Each writes contact(s) (normal from B toward A) into `out`, derived from `base`
    //   (actor / transform / rigidbody / trigger flag already filled). A contact is emitted
    //   when separation < margin, so callers get speculative (not-yet-touching) contacts too.
    static void SphereSphere (const Vector3& ca, float ra,
                              const Vector3& cb, float rb,
                              float margin, std::vector<Contact>& out, const Contact& base);
    static void SphereBox    (const Vector3& sp, float r,
                              const Vector3& bc, const Vector3& he, const Quaternion& brot,
                              float margin, std::vector<Contact>& out, const Contact& base);
    static void SphereCapsule(const Vector3& sp, float sr,
                              const Vector3& a, const Vector3& b, float cr,
                              float margin, std::vector<Contact>& out, const Contact& base);
    static void BoxBox       (const Vector3& ca, const Vector3& ha, const Quaternion& qa,
                              const Vector3& cb, const Vector3& hb, const Quaternion& qb,
                              float margin, std::vector<Contact>& out, const Contact& base);
    static void BoxCapsule   (const Vector3& bc, const Vector3& he, const Quaternion& brot,
                              const Vector3& a, const Vector3& b, float cr,
                              float margin, std::vector<Contact>& out, const Contact& base,
                              bool flipNormal);
    static void CapsuleCapsule(const Vector3& a0, const Vector3& a1, float ra,
                               const Vector3& b0, const Vector3& b1, float rb,
                               float margin, std::vector<Contact>& out, const Contact& base);
};

} // namespace Fujin
