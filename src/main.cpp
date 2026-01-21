#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"
#include <limits>

void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);
    fb.clear(0x000000);
    fb.clearDepth(std::numeric_limits<float>::max());

    Vec3 eye(0.0f, 0.0f, 3.0f);
    Vec3 center(0.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);

    Mat4 view = Mat4::lookAt(eye, center, up);
    Mat4 proj = Mat4::perspective(
        70.0f * 3.14159f / 180.0f,
        800.0f / 600.0f,
        0.1f,
        100.0f
    );

    Mat4 viewProj = Mat4::multiply(proj, view);

    drawTriangleProjected(
        fb,
        viewProj,
        Vec3(-0.1f,  0.1f, -0.5f),
        Vec3( 0.1f,  0.1f, -0.5f),
        Vec3( 0.0f, -0.1f, -0.5f),
        0xFF0000
    );

    runWindow(fb);
}