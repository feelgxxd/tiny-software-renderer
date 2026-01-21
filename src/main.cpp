#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"

void runWindow(Framebuffer& fb);

int main()
{
    Framebuffer fb(800, 600);
    fb.clear(0xFF000000);

    Vec3 v0(-0.5f, -0.5f, 2.0f);
    Vec3 v1( 0.5f, -0.5f, 2.0f);
    Vec3 v2( 0.0f,  0.5f, 1.0f); // öne yakın

    drawTriangleProjected(fb, Mat4::identity(), v0, v1, v2, 0xFF00FF00);

    runWindow(fb);
}