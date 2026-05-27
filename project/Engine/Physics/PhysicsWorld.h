#pragma once
#include "Engine/Math/Math.h"
#include <vector>

namespace Fujin {

class SceneManager;
class Actor;
class TransformComponent;
class RigidbodyComponent;
class ColliderComponent;

// Contact normal points from B toward A (separating direction for A).
struct Contact {
    Actor*              actorA      = nullptr;
    Actor*              actorB      = nullptr;
    TransformComponent* tcA         = nullptr;
    TransformComponent* tcB         = nullptr;
    RigidbodyComponent* rbA         = nullptr;
    RigidbodyComponent* rbB         = nullptr;
    Vector3             normal      = {};
    float               penetration = 0.0f;
    Vector3             point       = {};
    bool                isTrigger   = false;
};

class PhysicsWorld {
public:
    Vector3 Gravity = { 0.0f, -9.81f, 0.0f };

    void Step(SceneManager& scene, float dt);

private:
    void IntegrateVelocities(SceneManager& scene, float dt);
    void DetectCollisions(SceneManager& scene, std::vector<Contact>& out);
    void ResolveContact(Contact& c);
    void CorrectPosition(Contact& c);

    bool TestPair(Actor* a, Actor* b, Contact& out);

    // --- primitive tests (normal: from B toward A) ---
    static bool SphereSphere(
        const Vector3& posA, float rA,
        const Vector3& posB, float rB,
        Contact& c);

    static bool SphereAABB(
        const Vector3& spherePos, float r,
        const Vector3& boxCenter, const Vector3& halfExt,
        Contact& c);

    static bool SphereCapsule(
        const Vector3& spherePos, float sphereR,
        const Vector3& capA, const Vector3& capB, float capR,
        Contact& c);

    static bool AABBAABB(
        const Vector3& centerA, const Vector3& halfA,
        const Vector3& centerB, const Vector3& halfB,
        Contact& c);

    static bool AABBCapsule(
        const Vector3& boxCenter, const Vector3& halfExt,
        const Vector3& capA, const Vector3& capB, float capR,
        Contact& c);

    static bool CapsuleCapsule(
        const Vector3& baseA, const Vector3& tipA, float rA,
        const Vector3& baseB, const Vector3& tipB, float rB,
        Contact& c);
};

} // namespace Fujin
