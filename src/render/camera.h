#pragma once

#include "math/math.h"

namespace sr {

// Orbit camera: rotates around a target point (mouse-drag friendly).
struct OrbitCamera {
    Vec3 target{0.0f};
    float yaw = 0.0f;   // radians, around +Y
    float pitch = 0.3f; // radians, positive looks down at the target
    float distance = 5.0f;
    float fovY = radians(45.0f);
    float zNear = 0.05f;
    float zFar = 200.0f;

    Vec3 position() const
    {
        float cp = std::cos(pitch);
        return target + Vec3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp) * distance;
    }

    Mat4 view() const { return Mat4::lookAt(position(), target, {0, 1, 0}); }
    Mat4 projection(float aspect) const { return Mat4::perspective(fovY, aspect, zNear, zFar); }

    void orbit(float dYaw, float dPitch)
    {
        yaw += dYaw;
        pitch = std::clamp(pitch + dPitch, radians(-89.0f), radians(89.0f));
    }

    void zoom(float factor) { distance = std::clamp(distance * factor, 0.2f, 100.0f); }
};

} // namespace sr
