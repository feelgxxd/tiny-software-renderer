#include "core/framebuffer.h"
#include "core/rasterizer.h"
#include "math/mat4.h"
#include <limits>

void runWindow(Framebuffer& fb);

static float t = 0.0f;

static Vec3 cubeVerts[8] = {
    Vec3(-0.5f, -0.5f, -0.5f), // 0
    Vec3( 0.5f, -0.5f, -0.5f), // 1
    Vec3( 0.5f,  0.5f, -0.5f), // 2
    Vec3(-0.5f,  0.5f, -0.5f), // 3
    Vec3(-0.5f, -0.5f,  0.5f), // 4
    Vec3( 0.5f, -0.5f,  0.5f), // 5
    Vec3( 0.5f,  0.5f,  0.5f), // 6
    Vec3(-0.5f,  0.5f,  0.5f)  // 7
};

static int cubeTris[12][3] = {
    // Front (+Z)
    {4, 5, 6}, {4, 6, 7},
    // Back (-Z)
    {1, 0, 3}, {1, 3, 2},
    // Left (-X)
    {0, 4, 7}, {0, 7, 3},
    // Right (+X)
    {5, 1, 2}, {5, 2, 6},
    // Top (+Y)
    {3, 7, 6}, {3, 6, 2},
    // Bottom (-Y)
    {0, 1, 5}, {0, 5, 4}
};

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

    Vec3 camPos    = Vec3(0.0f, 0.0f, 3.0f);
    Vec3 camTarget = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 camUp     = Vec3(0.0f, 1.0f, 0.0f);

    Mat4 view = Mat4::lookAt(camPos, camTarget, camUp);

    Mat4 proj = Mat4::perspective(
        70.0f * 3.14159f / 180.0f,
        800.0f / 600.0f,
        0.1f,
        100.0f
    );

    Mat4 viewProj = Mat4::multiply(proj, view);

    for (int i = 0; i < 12; i++)
    {
        ProjectedVertex pv0 = projectVertex(
            cubeVerts[cubeTris[i][0]],
            model1,
            viewProj,
            fb.width(),
            fb.height()
        );

        ProjectedVertex pv1 = projectVertex(
            cubeVerts[cubeTris[i][1]],
            model1,
            viewProj,
            fb.width(),
            fb.height()
        );

        ProjectedVertex pv2 = projectVertex(
            cubeVerts[cubeTris[i][2]],
            model1,
            viewProj,
            fb.width(),
            fb.height()
        );

        drawTriangleFilledProjected(
            fb,
            pv0,
            pv1,
            pv2,
            0x00FF00
        );
    }
}

int main()
{
    Framebuffer fb(800, 600);
    runWindow(fb);
}