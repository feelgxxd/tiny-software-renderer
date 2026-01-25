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

    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 result = {};

        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[i][k] * b.m[k][j];
                }
                result.m[i][j] = sum;
            }
        }

        return result;
    }

    static Mat4 perspective(float fov, float aspect, float znear, float zfar)
    {
        Mat4 r = {};
        float tanHalfFov = std::tan(fov * 0.5f);

        r.m[0][0] = 1.0f / (aspect * tanHalfFov);
        r.m[1][1] = 1.0f / tanHalfFov;

        r.m[2][2] = -(zfar + znear) / (zfar - znear);
        r.m[2][3] = -1.0f;

        r.m[3][2] = -(2.0f * znear * zfar) / (zfar - znear);
        r.m[3][3] = 0.0f;

        return r;
    }

    static Mat4 simplePerspective(float fov, float aspect)
    {
        Mat4 r = {};
        float f = 1.0f / std::tan(fov * 0.5f);

        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = 1.0f;
        r.m[3][3] = 1.0f;

        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();  
        Vec3 s = cross(f, up).normalized();    
        Vec3 u = cross(s, f);                   

        Mat4 result = identity();

        result.m[0][0] = s.x;
        result.m[1][0] = s.y;
        result.m[2][0] = s.z;

        result.m[0][1] = u.x;
        result.m[1][1] = u.y;
        result.m[2][1] = u.z;

        result.m[0][2] = -f.x;
        result.m[1][2] = -f.y;
        result.m[2][2] = -f.z;

        result.m[3][0] = -dot(s, eye);
        result.m[3][1] = -dot(u, eye);
        result.m[3][2] = dot(f, eye);

        return result;
    }

    static Mat4 rotationY(float angle)
    {
        Mat4 r = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);

        r.m[0][0] =  c;
        r.m[0][2] =  s;

        r.m[2][0] = -s;
        r.m[2][2] =  c;

        return r;
    }
};