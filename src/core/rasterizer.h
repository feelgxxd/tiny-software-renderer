#pragma once
#include "math/vec3.h"
#include "math/vec4.h"
#include "math/mat4.h"
#include "core/framebuffer.h"
#include "geometry/mesh.h"
#include <cstdint>

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c);

struct ProjectedVertex {
    Vec3 screen;
    float invW;
    float ndcZ;
    Vec3 worldPos;
    Vec3 normal;
};

ProjectedVertex projectVertex(
    const Vec3& v,
    const Mat4& model,
    const Mat4& viewProj,
    int width,
    int height
);

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

/*void drawTriangleProjected(
    Framebuffer& fb,
    const Mat4& model,
    const Mat4& viewProj,
    const Vec3& camPos,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
);*/

void drawTriangleFilledProjected(
    Framebuffer& fb,
    const ProjectedVertex& v0,
    const ProjectedVertex& v1,
    const ProjectedVertex& v2,
    uint32_t baseColor
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
    const Mat4& model,
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
);

void drawMesh(
   Framebuffer& fb,
    const Mesh& mesh,
    const Mat4& model,
    const Mat4& viewProj,
    uint32_t color
);