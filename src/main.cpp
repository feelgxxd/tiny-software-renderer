#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"
#include <limits>

void runWindow(Framebuffer& fb);

static float t = 0.0f;

void render(Framebuffer& fb)
{
    fb.clear(0x000000);
    fb.clearDepth(1.0f);

    t += 0.001f;

    Mat4 rotation = Mat4::rotationY(t);

    Mat4 translation1 = Mat4::translation(0.0f, 0.0f, -2.0f);
    Mat4 model1 = Mat4::multiply(rotation, translation1);

    Mat4 translation2 = Mat4::translation(0.8f, 0.0f, -3.0f);
    Mat4 model2 = Mat4::multiply(rotation, translation2);

    Mat4 proj = Mat4::perspective(
        70.0f * 3.14159f / 180.0f,
        800.0f / 600.0f,
        0.1f,
        100.0f
    );

    drawTriangleProjected(
        fb,
        model1,
        proj,
        Vec3(-0.5f, -0.5f, 0.0f),
        Vec3( 0.5f, -0.5f, 0.0f),
        Vec3( 0.0f,  0.5f, 0.0f),
        0x00FF00
    );

    drawTriangleProjected(
        fb,
        model2,
        proj,
        Vec3(-0.4f, -0.4f, 0.0f),
        Vec3( 0.4f, -0.4f, 0.0f),
        Vec3( 0.0f,  0.6f, 0.0f),
        0xFF0000
    );
}

int main()
{
    Framebuffer fb(800, 600);
    runWindow(fb);
}