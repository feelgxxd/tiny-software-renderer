#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"

void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);
    fb.clear(0x000000);
    fb.clearDepth(std::numeric_limits<float>::max());

    // Basit üçgen, ekran koordinatlarıyla
    Vec3 v0(300, 300, 0.5f);
    Vec3 v1(500, 300, 0.5f);
    Vec3 v2(400, 500, 0.5f);

    drawTriangleFilled(fb, v0, v1, v2, 0x00FF00);

    runWindow(fb);
}