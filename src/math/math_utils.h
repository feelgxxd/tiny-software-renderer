#pragma once
#include "vec3.h"
#include <cmath>

inline Vec3 normalize(const Vec3& v)
{
    float len = std::sqrt(dot(v, v));
    if (len == 0.0f) return v;
    return {
        v.x / len,
        v.y / len,
        v.z / len
    };
}