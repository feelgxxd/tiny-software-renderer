#pragma once
#include "vec3.h"
#include <cmath>

inline float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

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