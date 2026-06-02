#pragma once
#include "Component.h"
#include "Engine/Math/Math.h"
#include <functional>
#include <array>

namespace Fujin {

class Actor;

enum class ColliderShape { Sphere, AABB, Capsule };

// Which "type" this object belongs to
enum class CollisionChannel : uint8_t {
    WorldStatic  = 0,
    WorldDynamic = 1,
    Pawn         = 2,
    Projectile   = 3,
    Custom1      = 4,
    Custom2      = 5,
    Count        = 6,
};
static constexpr int kChannelCount = static_cast<int>(CollisionChannel::Count);

// How this object reacts to each channel
enum class CollisionResponse : uint8_t {
    Ignore  = 0,   // no interaction
    Overlap = 1,   // fire overlap events only
    Block   = 2,   // full physics collision
};

// Passed to OnHit callbacks
struct HitResult {
    Actor*  otherActor  = nullptr;
    Vector3 normal      = {};   // points away from other actor
    Vector3 point       = {};   // world-space contact point
    float   penetration = 0.0f;
};

class ColliderComponent : public Component {
public:
    ColliderShape Shape       = ColliderShape::AABB;
    Vector3       Offset      = {};
    bool          IsTrigger   = false;

    // Sphere / Capsule radius
    float   Radius      = 0.5f;
    // AABB half-extents
    Vector3 HalfExtents = { 0.5f, 0.5f, 0.5f };
    // Capsule half-height of the cylinder (not including caps), axis = local Y
    float   HalfHeight  = 1.0f;

    // This object's collision channel
    CollisionChannel Channel = CollisionChannel::WorldDynamic;

    // Response to each incoming channel (defaults to Block all)
    std::array<CollisionResponse, kChannelCount> Responses;

    ColliderComponent() { Responses.fill(CollisionResponse::Block); }

    CollisionResponse GetResponseTo(CollisionChannel ch) const {
        return Responses[static_cast<size_t>(ch)];
    }
    void SetResponseToChannel(CollisionChannel ch, CollisionResponse r) {
        Responses[static_cast<size_t>(ch)] = r;
    }
    void SetResponseToAll(CollisionResponse r) { Responses.fill(r); }

    // Gameplay callbacks — set from actor/game code, not serialized
    std::function<void(const HitResult&)> OnHit;          // fires each frame while blocking
    std::function<void(Actor*)>           OnBeginOverlap;  // fires once on overlap start
    std::function<void(Actor*)>           OnEndOverlap;    // fires once on overlap end

    const char* GetTypeName() const override { return "ColliderComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
