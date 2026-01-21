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
    uint32_t color)
{
    float area = edgeFunction(v0, v1, v2);
    if (area == 0) return;

    int minX = (int)std::min({ v0.x, v1.x, v2.x });
    int minY = (int)std::min({ v0.y, v1.y, v2.y });
    int maxX = (int)std::max({ v0.x, v1.x, v2.x });
    int maxY = (int)std::max({ v0.y, v1.y, v2.y });

    for (int y = minY; y <= maxY; y++)
    {
        for (int x = minX; x <= maxX; x++)
        {
            Vec3 p((float)x + 0.5f, (float)y + 0.5f, 0);

            float w0 = edgeFunction(v1, v2, p);
            float w1 = edgeFunction(v2, v0, p);
            float w2 = edgeFunction(v0, v1, p);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (w0 <= 0 && w1 <= 0 && w2 <= 0))
            {
                w0 /= area;
                w1 /= area;
                w2 /= area;

                float z = v0.z * w0 + v1.z * w1 + v2.z * w2;

                if (z < fb.depth(x, y))
                {
                    fb.depth(x, y) = z;
                    fb.setPixel(x, y, color);
                }
            }
        }
    }
}