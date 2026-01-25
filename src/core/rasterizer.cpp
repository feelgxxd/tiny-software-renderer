#include "rasterizer.h"
#include <cstdlib>
#include <cstdio>
#include "math/vec3.h"
#include "../math/math_utils.h"

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return (b.x - a.x) * (c.y - a.y)
         - (b.y - a.y) * (c.x - a.x);
}

struct ProjectedVertex {
    Vec3 screen; 
    float invW;
    float ndcZ;

    Vec3 worldPos;
    Vec3 normal;
};

bool isBackface(const Vec3& v0, const Vec3& v1, const Vec3& v2)
{
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;

    Vec3 normal = cross(e1, e2);

    return normal.z <= 0;
}

static uint32_t applyLight(
    const Vec3& normal,
    const Vec3& lightDir,
    uint32_t baseColor
)
{
    float NdotL = dot(normal, lightDir);
    NdotL = std::max(0.0f, NdotL);

    uint8_t r = (baseColor >> 16) & 0xFF;
    uint8_t g = (baseColor >> 8)  & 0xFF;
    uint8_t b = (baseColor)       & 0xFF;

    r = (uint8_t)(r * NdotL);
    g = (uint8_t)(g * NdotL);
    b = (uint8_t)(b * NdotL);

    return (r << 16) | (g << 8) | b;
}

static ProjectedVertex projectVertex(
    const Vec3& v,
    const Mat4& model,
    const Mat4& viewProj,
    int width,
    int height
) {
    Vec4 world = Mat4::multiply(
        model,
        Vec4(v.x, v.y, v.z, 1.0f)
    );

    Vec4 clip = Mat4::multiply(viewProj, world);

    ProjectedVertex out;
    out.invW = 1.0f / clip.w;

    float ndcX = clip.x * out.invW;
    float ndcY = clip.y * out.invW;
    float ndcZ = clip.z * out.invW;

    static int debugCount2 = 0;
    if (debugCount2++ < 2) {
        printf("Vertex (%.2f, %.2f, %.2f) -> clipZ: %.3f, invW: %.3f, ndcZ: %.3f\n", 
            v.x, v.y, v.z, clip.z, out.invW, ndcZ);
    }

    out.screen.x = (ndcX + 1.0f) * 0.5f * width;
    out.screen.y = (1.0f - (ndcY + 1.0f) * 0.5f) * height;
    out.ndcZ = (ndcZ + 1.0f) * 0.5f;
    out.screen.z = 0.0f;

    out.worldPos = Vec3(world.x, world.y, world.z);
    out.normal = normalize(Vec3(world.x, world.y, world.z));

    return out;
}

void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx - dy;

    while (true) {
        fb.setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void drawTriangleFilled(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
)
{
    float area = edgeFunction(v0, v1, v2);
    if (area == 0.0f) return;

    bool flip = false;
    if (area < 0.0f) {
        area = -area;
        flip = true;
    }

    float z = v0.z;

    int minX = (int)std::floor(std::min({ v0.x, v1.x, v2.x }));
    int maxX = (int)std::ceil (std::max({ v0.x, v1.x, v2.x }));
    int minY = (int)std::floor(std::min({ v0.y, v1.y, v2.y }));
    int maxY = (int)std::ceil (std::max({ v0.y, v1.y, v2.y }));

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {

            Vec3 p(x + 0.5f, y + 0.5f, 0.0f);

            float w0 = edgeFunction(v1, v2, p);
            float w1 = edgeFunction(v2, v0, p);
            float w2 = edgeFunction(v0, v1, p);

            if (flip) {
                w0 = -w0;
                w1 = -w1;
                w2 = -w2;
            }

            if (w0 >= 0 && w1 >= 0 && w2 >= 0)
            {
                w0 /= area;
                w1 /= area;
                w2 /= area;

                float depth =
                    w0 * v0.z +
                    w1 * v1.z +
                    w2 * v2.z;
         
                if (fb.testAndSetDepth(x, y, depth))
                {
                    fb.setPixel(x, y, color);
                }
            }
        }
    }
}

void drawTriangleFilledProjected(
    Framebuffer& fb,
    const ProjectedVertex& v0,
    const ProjectedVertex& v1,
    const ProjectedVertex& v2,
    uint32_t baseColor
)
{
    // World space normal
    Vec3 e1 = v1.worldPos - v0.worldPos;
    Vec3 e2 = v2.worldPos - v0.worldPos;
    //Vec3 normal = normalize(cross(e1, e2));

    // Light (world space)
    static float t = 0.0f;
    t += 0.02f;

    Vec3 lightDir = normalize(Vec3(
        sinf(t),
        0.7f,
        -cosf(t)
    ));

    const Vec3& p0 = v0.screen;
    const Vec3& p1 = v1.screen;
    const Vec3& p2 = v2.screen;

    float area = edgeFunction(p0, p1, p2);
    if (area == 0.0f) return;

    bool flip = false;
    if (area < 0.0f) {
        area = -area;
        flip = true;
    }

    int minX = (int)std::floor(std::min({ p0.x, p1.x, p2.x }));
    int maxX = (int)std::ceil (std::max({ p0.x, p1.x, p2.x }));
    int minY = (int)std::floor(std::min({ p0.y, p1.y, p2.y }));
    int maxY = (int)std::ceil (std::max({ p0.y, p1.y, p2.y }));

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {

            Vec3 p(x + 0.5f, y + 0.5f, 0.0f);

            float w0 = edgeFunction(p1, p2, p);
            float w1 = edgeFunction(p2, p0, p);
            float w2 = edgeFunction(p0, p1, p);

            if (flip) {
                w0 = -w0;
                w1 = -w1;
                w2 = -w2;
            }

            if (w0 >= 0 && w1 >= 0 && w2 >= 0)
            {
                w0 /= area;
                w1 /= area;
                w2 /= area;

                float invW =
                    w0 * v0.invW +
                    w1 * v1.invW +
                    w2 * v2.invW;

                float depth =
                (
                    w0 * v0.ndcZ * v0.invW +
                    w1 * v1.ndcZ * v1.invW +
                    w2 * v2.ndcZ * v2.invW
                ) / invW;

                Vec3 interpNormal =
                    normalize(
                        v0.normal * w0 +
                        v1.normal * w1 +
                        v2.normal * w2
                    );

                uint32_t litColor =
                    applyLight(interpNormal, lightDir, baseColor);

                if (fb.testAndSetDepth(x, y, depth))
                {
                    fb.setPixel(x, y, litColor);
                }
            }
        }
    }
}

void drawTriangleProjected(
    Framebuffer& fb,
    const Mat4& model,
    const Mat4& viewProj,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
)
{
    ProjectedVertex p0 = projectVertex(v0, model, viewProj, fb.width(), fb.height());
    ProjectedVertex p1 = projectVertex(v1, model, viewProj, fb.width(), fb.height());
    ProjectedVertex p2 = projectVertex(v2, model, viewProj, fb.width(), fb.height());

    Vec3 faceNormal = normalize(
        cross(
            p1.worldPos - p0.worldPos,
            p2.worldPos - p0.worldPos
        )
    );

    p0.normal = faceNormal;
    p1.normal = faceNormal;
    p2.normal = faceNormal;

    drawTriangleFilledProjected(fb, p0, p1, p2, color);
}

void drawTriangleWireframe(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
)
{
    drawLine(fb, (int)v0.x, (int)v0.y, (int)v1.x, (int)v1.y, color);
    drawLine(fb, (int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, color);
    drawLine(fb, (int)v2.x, (int)v2.y, (int)v0.x, (int)v0.y, color);
}

void drawTriangleWireframeProjected(
    Framebuffer& fb,
    const Mat4& model,
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
)
{
    ProjectedVertex p0 = projectVertex(v0, model, proj, fb.width(), fb.height());
    ProjectedVertex p1 = projectVertex(v1, model, proj, fb.width(), fb.height());
    ProjectedVertex p2 = projectVertex(v2, model, proj, fb.width(), fb.height());

    drawLine(fb, (int)p0.screen.x, (int)p0.screen.y, (int)p1.screen.x, (int)p1.screen.y, color);
    drawLine(fb, (int)p1.screen.x, (int)p1.screen.y, (int)p2.screen.x, (int)p2.screen.y, color);
    drawLine(fb, (int)p2.screen.x, (int)p2.screen.y, (int)p0.screen.x, (int)p0.screen.y, color);
}

void drawMesh(
    Framebuffer& fb,
    const Mesh& mesh,
    const Mat4& model,
    const Mat4& viewProj,
    uint32_t color
)
{
    uint32_t triCount = mesh.getTriangleCount();
    for (uint32_t i = 0; i < triCount; i++) {
        Vertex v0, v1, v2;
        mesh.getTriangle(i, v0, v1, v2);

        drawTriangleProjected(
            fb,
            model,
            viewProj,
            v0.position,
            v1.position,
            v2.position,
            color
        );
    }
}