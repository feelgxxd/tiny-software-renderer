#pragma once
#include <cmath>

struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }

    Vec3 operator-() const {
        return Vec3(-x, -y, -z);
    }

    Vec3 operator/(float s) const {
        return Vec3(x / s, y / s, z / s);
    }

    Vec3 operator * (float s) const {
        return Vec3(x * s, y * s, z * s);
    }

    Vec3 operator + (const Vec3& v) const {
        return Vec3(x + v.x, y + v.y, z + v.z);
    }

    Vec3 normalized() const {
        float len = std::sqrt(x * x + y * y + z * z);
        if (len == 0) return Vec3(0, 0, 0);
        return Vec3(x / len, y / len, z / len);
    }

    float lengthSquared() const {
        return x*x + y*y + z*z;
    }
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
};