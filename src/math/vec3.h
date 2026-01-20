#pragma once
#include <cmath>

struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& v) const {
        return { x + v.x, y + v.y, z + v.z };
    }

    Vec3 operator-(const Vec3& v) const {
        return { x - v.x, y - v.y, z - v.z };
    }

    Vec3 operator*(float s) const {
        return { x * s, y * s, z * s };
    }
};