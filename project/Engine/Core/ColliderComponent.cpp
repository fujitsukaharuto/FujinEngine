#include "ColliderComponent.h"
#include "Component.h"

namespace Fujin {

static const bool s_colliderRegistered = []() {
    ComponentRegistry::Get().Register("ColliderComponent", []() {
        return std::make_unique<ColliderComponent>();
    });
    return true;
}();

void ColliderComponent::ToJson(nlohmann::json& j) const {
    j["shape"]       = static_cast<int>(Shape);
    j["offset"]      = { Offset.x, Offset.y, Offset.z };
    j["isTrigger"]   = IsTrigger;
    j["radius"]      = Radius;
    j["halfExtents"] = { HalfExtents.x, HalfExtents.y, HalfExtents.z };
    j["halfHeight"]  = HalfHeight;
    j["channel"]     = static_cast<int>(Channel);
    nlohmann::json rArr = nlohmann::json::array();
    for (auto r : Responses) rArr.push_back(static_cast<int>(r));
    j["responses"]   = rArr;
}

void ColliderComponent::FromJson(const nlohmann::json& j) {
    if (j.contains("shape"))     Shape     = static_cast<ColliderShape>(j["shape"].get<int>());
    if (j.contains("isTrigger")) IsTrigger = j["isTrigger"].get<bool>();
    if (j.contains("radius"))    Radius    = j["radius"].get<float>();
    if (j.contains("halfHeight"))HalfHeight= j["halfHeight"].get<float>();
    if (j.contains("offset")) {
        auto& o = j["offset"];
        Offset = { o[0].get<float>(), o[1].get<float>(), o[2].get<float>() };
    }
    if (j.contains("halfExtents")) {
        auto& h = j["halfExtents"];
        HalfExtents = { h[0].get<float>(), h[1].get<float>(), h[2].get<float>() };
    }
    if (j.contains("channel"))
        Channel = static_cast<CollisionChannel>(j["channel"].get<int>());
    if (j.contains("responses")) {
        auto& rArr = j["responses"];
        for (int i = 0; i < kChannelCount && i < static_cast<int>(rArr.size()); ++i)
            Responses[static_cast<size_t>(i)] =
                static_cast<CollisionResponse>(rArr[i].get<int>());
    }
}

} // namespace Fujin
