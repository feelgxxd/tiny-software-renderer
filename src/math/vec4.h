#pragma once

struct Vec4
{
    float x, y, z, w;

    Vec4() : x(0), y(0), z(0), w(1) {}
    Vec4(float x, float y, float z, float w = 1.0f)
        : x(x), y(y), z(z), w(w) {}
};