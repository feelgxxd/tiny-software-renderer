#pragma once
#include <cmath>
#include "vec4.h"

struct Mat4 {
    float m[4][4];

    static Mat4 identity() {
        Mat4 r = {};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r = identity();
        r.m[3][0] = x;
        r.m[3][1] = y;
        r.m[3][2] = z;
        return r;
    }

    static Vec4 multiply(const Mat4& mat, const Vec4& v) {
        Vec4 r;
        r.x = v.x * mat.m[0][0] + v.y * mat.m[1][0] + v.z * mat.m[2][0] + v.w * mat.m[3][0];
        r.y = v.x * mat.m[0][1] + v.y * mat.m[1][1] + v.z * mat.m[2][1] + v.w * mat.m[3][1];
        r.z = v.x * mat.m[0][2] + v.y * mat.m[1][2] + v.z * mat.m[2][2] + v.w * mat.m[3][2];
        r.w = v.x * mat.m[0][3] + v.y * mat.m[1][3] + v.z * mat.m[2][3] + v.w * mat.m[3][3];
        return r;
    }

    static Mat4 perspective(float fov, float aspect, float znear, float zfar) {
        Mat4 r = {};
        float tanHalfFov = std::tan(fov * 0.5f);

        r.m[0][0] = 1.0f / (aspect * tanHalfFov);
        r.m[1][1] = 1.0f / tanHalfFov;
        r.m[2][2] = zfar / (zfar - znear);
        r.m[2][3] = 1.0f;
        r.m[3][2] = (-znear * zfar) / (zfar - znear);
        return r;
    }
};