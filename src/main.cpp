#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"
#include <limits>

void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);
    fb.clear(0x000000);

    fb.clearDepth(1.0f);

    Mat4 proj = Mat4::perspective(
        70.0f * 3.14159f / 180.0f,
        800.0f / 600.0f,
        0.1f,
        100.0f
    );
    
    drawTriangleProjected(
        fb,
        proj,
        Vec3(-0.5f, -0.5f, -1.0f),
        Vec3( 0.5f, -0.5f, -1.0f),
        Vec3( 0.0f,  0.5f, -1.0f),
        0x00FF00
    );

    drawTriangleProjected(
        fb,
        proj,
        Vec3(-0.3f, -0.3f, -0.8f),
        Vec3( 0.7f, -0.3f, -0.8f),
        Vec3( 0.2f,  0.7f, -0.8f),
        0xFF0000
    );

    runWindow(fb);
}