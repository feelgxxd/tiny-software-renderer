#include "core/framebuffer.h"
#include "core/rasterizer.h"

// platform-specific
void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);

    fb.clear(0xFF000000); // siyah

    // wireframe triangle
    drawLine(fb, 100, 100, 400, 100, 0xFFFFFFFF);
    drawLine(fb, 400, 100, 250, 400, 0xFFFFFFFF);
    drawLine(fb, 250, 400, 100, 100, 0xFFFFFFFF);

    runWindow(fb);
    return 0;
}