#pragma once
#include "math/vec3.h"          
#include "core/framebuffer.h"
#include <cstdint>

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c);

void drawTriangleFilled(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
);