#include "rasterizer.h"
#include <cstdlib>
#include "math/vec3.h"
#include "../math/math_utils.h"

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return (b.x - a.x) * (c.y - a.y)
         - (b.y - a.y) * (c.x - a.x);
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
    
    int minX = (int)std::min({ v0.x, v1.x, v2.x });
    int maxX = (int)std::max({ v0.x, v1.x, v2.x });
    int minY = (int)std::min({ v0.y, v1.y, v2.y });
    int maxY = (int)std::max({ v0.y, v1.y, v2.y });

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            Vec3 p(x + 0.5f, y + 0.5f, 0);

            float w0 = edgeFunction(v1, v2, p);
            float w1 = edgeFunction(v2, v0, p);
            float w2 = edgeFunction(v0, v1, p);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0 && area > 0) ||
            (w0 <= 0 && w1 <= 0 && w2 <= 0 && area < 0))
            {
                float alpha = w0 / area;
                float beta  = w1 / area;
                float gamma = w2 / area;

                float depth = alpha * v0.z + beta * v1.z + gamma * v2.z;

                if (fb.testAndSetDepth(x, y, depth))
                {
                    fb.setPixel(x, y, color);
                }
            }

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0 && area > 0) ||
                (w0 <= 0 && w1 <= 0 && w2 <= 0 && area < 0))
            {
                fb.setPixel(x, y, color);
            }
        }
    }
}

static Vec3 projectVertex(
    const Vec3& v,
    const Mat4& proj,
    int width,
    int height
) {
    Vec4 clip = Mat4::multiply(
        proj,
        Vec4(v.x, v.y, v.z, 1.0f)
    );

    if (clip.w == 0.0f)
        return Vec3(0, 0, 0);

    // Perspective divide
    clip.x /= clip.w;
    clip.y /= clip.w;
    clip.z /= clip.w;

    // NDC → Screen
    Vec3 out;
    out.x = (clip.x + 1.0f) * 0.5f * width;
    out.y = (1.0f + (clip.y - 1.0f) * 0.5f) * height;
    out.z = clip.z;

    return out;
}

void drawTriangleProjected(
    Framebuffer& fb,
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
)
{
    Vec3 p0 = projectVertex(v0, proj, fb.width(), fb.height());
    Vec3 p1 = projectVertex(v1, proj, fb.width(), fb.height());
    Vec3 p2 = projectVertex(v2, proj, fb.width(), fb.height());

    float area = edgeFunction(p0, p1, p2);
    if (area <= 0.0f)
        return;

    drawTriangleFilled(fb, p0, p1, p2, color);
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
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
)
{
    Vec3 p0 = projectVertex(v0, proj, fb.width(), fb.height());
    Vec3 p1 = projectVertex(v1, proj, fb.width(), fb.height());
    Vec3 p2 = projectVertex(v2, proj, fb.width(), fb.height());

    drawTriangleWireframe(fb, p0, p1, p2, color);
}