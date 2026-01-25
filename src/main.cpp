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

    // Create projection matrix
    Mat4 proj = Mat4::perspective(
        70.0f * 3.14159f / 180.0f,
        800.0f / 600.0f,
        0.1f,
        100.0f
    );
    
    // Create identity model matrix (no transformation)
    Mat4 model = Mat4::identity();
    
    drawTriangleProjected(
        fb,
        model,
        proj,
        Vec3(-0.5f, -0.5f, -2.0f),
        Vec3( 0.5f, -0.5f, -2.0f),
        Vec3( 0.0f,  0.5f, -2.0f),
        0x00FF00
    );

    drawTriangleProjected(
        fb,
        model,
        proj,
        Vec3(-0.3f, -0.3f, -3.0f),
        Vec3( 0.7f, -0.3f, -3.0f),
        Vec3( 0.2f,  0.7f, -3.0f),
        0xFF0000
    );

    fb.drawDepthDebug();

    runWindow(fb);
}