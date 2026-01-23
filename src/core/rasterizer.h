#pragma once
#include "math/vec3.h"
#include "math/vec4.h"
#include "math/mat4.h"
#include "core/framebuffer.h"
#include <cstdint>

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c);

void drawLine(
    Framebuffer& fb,
    int x0, int y0,
    int x1, int y1,
    uint32_t color
);

void drawTriangleFilled(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
);

void drawTriangleProjected(
    Framebuffer& fb,
    const Mat4& viewProj,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
);

void drawTriangleWireframe(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
);

void drawTriangleWireframeProjected(
    Framebuffer& fb,
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
);