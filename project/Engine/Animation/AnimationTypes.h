#pragma once
#include "Engine/Math/Math.h"
#include "Engine/Math/Transform.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Fujin {

static constexpr uint32_t MAX_BONES = 128;

// Single joint in a skeleton hierarchy.
struct Joint {
    std::string Name;
    int32_t     ParentIndex    = -1;
    Matrix4x4   InverseBindPose;    // mesh-space → joint-space (aiMesh::mBones[i].mOffsetMatrix)
    Matrix4x4   BindPoseLocal;      // rest-pose local transform (aiNode::mTransformation)
    Transform   BindLocal;          // BindPoseLocal decomposed to TRS (rest pose for joints a clip
                                    // doesn't animate; the neutral pose blends/state-machine fall back to)
};

struct Skeleton {
    std::vector<Joint>                       Joints;
    std::unordered_map<std::string, uint32_t> JointMap;  // name → index
};

struct KeyVec3 { float Time; Vector3    Value; };
struct KeyQuat { float Time; Quaternion Value; };

// Per-joint animation track (one channel = one joint).
struct NodeAnim {
    std::string           JointName;
    std::vector<KeyVec3>  PositionKeys;
    std::vector<KeyVec3>  ScaleKeys;
    std::vector<KeyQuat>  RotationKeys;
};

struct AnimationClip {
    std::string           Name;
    float                 DurationSeconds = 0.0f;
    std::vector<NodeAnim> Channels;
};

} // namespace Fujin
