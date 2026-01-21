#include "rasterizer.h"
#include <cstdlib>
#include "math/vec3.h"
#include "../math/math_utils.h"

float edgeFunction(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return (b.x - a.x) * (c.y - a.y)
         - (b.y - a.y) * (c.x - a.x);
}

void drawTriangleFilled(
    Framebuffer& fb,
    const Vec3& v0,
    const Vec3& v1,
    const Vec3& v2,
    uint32_t color
)
{
    fb.setPixel(20, 20, 0xFFFFFF00); // sarı

    // bounding box
    int minX = (int)std::min({ v0.x, v1.x, v2.x });
    int maxX = (int)std::max({ v0.x, v1.x, v2.x });
    int minY = (int)std::min({ v0.y, v1.y, v2.y });
    int maxY = (int)std::max({ v0.y, v1.y, v2.y });

    float area = edgeFunction(v0, v1, v2);
    if (area < 0.0f) {
        return drawTriangleFilled(fb, v0, v2, v1, color);
    }

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            Vec3 p(x + 0.5f, y + 0.5f, 0.0f);

            float w0 = edgeFunction(v1, v2, p);
            float w1 = edgeFunction(v2, v0, p);
            float w2 = edgeFunction(v0, v1, p);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                fb.setPixel(x, y, color);
            }
        }
    }
}

static Vec3 projectVertex(
    const Vec3& v,
    const Mat4&,
    int width,
    int height
) {
    float invZ = 1.0f / v.z;

    float x = v.x * invZ;
    float y = v.y * invZ;

    Vec3 out;
    out.x = (x * 0.5f + 0.5f) * width;
    out.y = (1.0f - (y * 0.5f + 0.5f)) * height;
    out.z = v.z;

    return out;
}

void drawTriangleProjected(
    Framebuffer& fb,
    const Mat4& proj,
    Vec3 v0,
    Vec3 v1,
    Vec3 v2,
    uint32_t color
) {
    // 🔴 TEST: ekrana direkt bir piksel bas
    fb.setPixel(10, 10, 0xFFFF00FF); // pembe

    Vec3 p0 = projectVertex(v0, proj, fb.width(), fb.height());
    Vec3 p1 = projectVertex(v1, proj, fb.width(), fb.height());
    Vec3 p2 = projectVertex(v2, proj, fb.width(), fb.height());

    drawTriangleFilled(fb, p0, p1, p2, color);
}