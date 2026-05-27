#pragma once
#include "Engine/Math/Math.h"

namespace Fujin {

class DebugCamera {
public:
    float MoveSpeed    = 5.0f;   // units/sec (scaled by Shift)
    float RotateSpeed  = 0.003f; // rad/pixel

    // Call once per frame. viewportHovered: mouse is over the 3D viewport.
    void Update(float dt, bool viewportHovered,
                float vpX, float vpY, float vpW, float vpH);

    Vector3 GetPosition() const { return m_pos; }
    Vector3 GetTarget()   const;

private:
    Vector3 m_pos   = { 0.0f,  3.0f, -8.0f };
    float   m_yaw   = 0.0f;    // radians, horizontal rotation around Y
    float   m_pitch = -0.15f;  // radians, vertical

    Vector3 Forward() const;
    Vector3 Right()   const;
};

} // namespace Fujin
