#include "core/framebuffer.h"
#include "core/rasterizer.h"

void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);

    fb.clear(0xFF000000);

    Vec3 a0(100, 100, 0.5f);
    Vec3 a1(300, 100, 0.5f);
    Vec3 a2(200, 300, 0.5f);

    Vec3 b0(150, 150, 0.2f);
    Vec3 b1(350, 150, 0.2f);
    Vec3 b2(250, 350, 0.2f);

    drawTriangleFilled(fb, a0, a1, a2, 0xFFFFFFFF);
    drawTriangleFilled(fb, b0, b1, b2, 0xFFFF0000);

    runWindow(fb);
    return 0;
}