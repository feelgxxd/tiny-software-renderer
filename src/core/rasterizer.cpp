#include "rasterizer.h"
#include <cstdlib>
#include "math/vec3.h"
#include "../math/math_utils.h"

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

bool isBackface(const Vec3& v0, const Vec3& v1, const Vec3& v2)
{
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;

    Vec3 normal = cross(e1, e2);
    return normal.z >= 0;
}