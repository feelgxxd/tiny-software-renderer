#include "test.h"

#include "core/image_io.h"
#include "core/renderer.h"
#include "render/scene.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace sr;

extern bool g_updateGolden;

#ifndef SR_GOLDEN_DIR
#define SR_GOLDEN_DIR "tests/golden"
#endif

namespace {

// Counts how many times each pixel is shaded. Vertices are already in NDC.
class CoverageShader final : public Shader {
public:
    CoverageShader(int w, int h) : width(w), counts(static_cast<size_t>(w) * h) {}
    int width;
    mutable std::vector<std::atomic<int>> counts;

    Vec4 vertex(const Vertex& in, float*) const override { return Vec4(in.position, 1.0f); }
    bool fragment(const FragmentInput& in, Vec3& color) const override
    {
        counts[static_cast<size_t>(in.y) * width + in.x].fetch_add(1, std::memory_order_relaxed);
        color = Vec3(1.0f);
        return true;
    }
};

// N x N grid of quads over [-0.8, 0.8]^2 with sub-pixel-jittered interior vertices.
Mesh jitteredGrid(int n, uint32_t seed)
{
    Mesh m;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
    };
    float cell = 1.6f / n;
    for (int y = 0; y <= n; ++y)
        for (int x = 0; x <= n; ++x) {
            Vertex v;
            v.position = {-0.8f + x * cell, -0.8f + y * cell, 0.0f};
            if (x > 0 && x < n && y > 0 && y < n) {
                v.position.x += rnd() * cell * 0.6f;
                v.position.y += rnd() * cell * 0.6f;
            }
            m.vertices.push_back(v);
        }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            uint32_t a = y * (n + 1) + x, b = a + 1, c = a + n + 2, d = a + n + 1;
            if ((x + y) & 1) m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
            else m.indices.insert(m.indices.end(), {a, b, d, b, c, d});
        }
    return m;
}

void checkWatertight(int w, int h, int threads, uint32_t seed)
{
    Mesh grid = jitteredGrid(23, seed);
    CoverageShader shader(w, h);
    Framebuffer fb(w, h);
    Renderer r(threads);
    DrawState state;
    state.depthTest = false;
    state.cull = CullMode::None;
    r.begin(fb);
    r.draw(grid, shader, state);
    r.end();

    // Grid covers window x in [0.1w, 0.9w]; check pixel centers safely inside.
    int overdraw = 0, holes = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int c = shader.counts[static_cast<size_t>(y) * w + x].load();
            if (c > 1) ++overdraw;
            float px = x + 0.5f, py = y + 0.5f;
            bool inside = px > 0.1f * w + 1 && px < 0.9f * w - 1 && py > 0.1f * h + 1 && py < 0.9f * h - 1;
            if (inside && c == 0) ++holes;
        }
    if (overdraw || holes)
        std::printf("    %dx%d threads=%d seed=%u: %d double-shaded, %d holes\n", w, h, threads, seed, overdraw, holes);
    CHECK(overdraw == 0);
    CHECK(holes == 0);
}

} // namespace

TEST_CASE("raster: top-left fill rule shades shared edges exactly once")
{
    checkWatertight(257, 199, 1, 1u);
    checkWatertight(640, 480, 4, 7u);
    checkWatertight(333, 333, 8, 12345u);
}

TEST_CASE("raster: triangles crossing the near plane and behind the camera are clipped")
{
    // A floor plane that extends behind the camera.
    Mesh floor = Mesh::plane(100.0f);
    struct FloorShader final : Shader {
        Mat4 mvp;
        Vec4 vertex(const Vertex& in, float*) const override { return mvp * Vec4(in.position, 1.0f); }
        bool fragment(const FragmentInput& in, Vec3& c) const override
        {
            c = Vec3(in.depth);
            return true;
        }
    } shader;
    Mat4 view = Mat4::lookAt({0, 1, 0}, {0, 0.8f, -5}, {0, 1, 0});
    shader.mvp = Mat4::perspective(radians(70.0f), 4.0f / 3.0f, 0.1f, 100.0f) * view;

    Framebuffer fb(160, 120);
    fb.clear(Vec3(-1.0f));
    Renderer r(2);
    r.begin(fb);
    r.draw(floor, shader);
    r.end();

    CHECK(r.stats().trianglesRasterized > 2); // clipping split the quad
    int covered = 0;
    bool finite = true;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            float d = fb.color(x, y).x;
            if (d >= 0.0f) ++covered;
            if (!std::isfinite(d) || d > 1.0f) finite = false;
        }
    CHECK(finite);
    // Floor below the horizon covers the lower part of the screen.
    CHECK(fb.color(80, 110).x >= 0.0f);
    CHECK(fb.color(80, 5).x < 0.0f);
    CHECK(covered > fb.width() * fb.height() / 3);
}

TEST_CASE("raster: back-face culling follows counter-clockwise front faces")
{
    Mesh tri;
    tri.vertices.resize(3);
    tri.vertices[0].position = {-0.5f, -0.5f, 0};
    tri.vertices[1].position = {0.5f, -0.5f, 0};
    tri.vertices[2].position = {0.0f, 0.5f, 0};
    tri.indices = {0, 1, 2};
    CoverageShader shader(64, 64);
    Framebuffer fb(64, 64);
    Renderer r(1);

    r.begin(fb);
    r.draw(tri, shader);
    r.end();
    CHECK(r.stats().fragmentsShaded > 0);

    tri.indices = {0, 2, 1};
    r.begin(fb);
    r.draw(tri, shader);
    r.end();
    CHECK(r.stats().fragmentsShaded == 0);
    CHECK(r.stats().trianglesCulled == 1);
}

// ---------------------------------------------------------------------------
// Whole-scene tests

namespace {

Framebuffer renderScene(const std::string& name, int threads)
{
    Scene scene;
    std::string error;
    if (!buildScene(name, SR_ASSET_DIR, scene, &error)) return Framebuffer();
    Renderer renderer(threads);
    SceneRenderer sceneRenderer;
    RenderSettings settings;
    settings.shadowMapSize = 1024;
    Framebuffer fb(320, 180);
    sceneRenderer.render(renderer, scene, scene.camera, 0.5f, fb, settings);
    return fb;
}

} // namespace

TEST_CASE("scenes: output is bit-identical for 1 and 8 threads")
{
    for (const char* name : {"materials", "shadows", "textures"}) {
        Framebuffer a = renderScene(name, 1);
        Framebuffer b = renderScene(name, 8);
        CHECK(a.width() == 320);
        bool same = std::memcmp(a.colorData(), b.colorData(), sizeof(Vec3) * 320 * 180) == 0 &&
                    std::memcmp(a.depthData(), b.depthData(), sizeof(float) * 320 * 180) == 0;
        if (!same) std::printf("    scene %s differs between thread counts\n", name);
        CHECK(same);
    }
}

TEST_CASE("scenes: golden image regression")
{
    // Compares 8-bit output with stored references. Small tolerances absorb
    // floating-point differences between compilers / CPUs.
    for (const char* name : {"materials", "shadows", "textures"}) {
        Framebuffer fb = renderScene(name, 0);
        std::vector<uint8_t> actual(static_cast<size_t>(fb.width()) * fb.height() * 4);
        fb.resolveRGBA(actual.data(), ToneMap::AcesFilmic);

        std::string path = std::string(SR_GOLDEN_DIR) + "/" + name + ".png";
        if (g_updateGolden) {
            CHECK(savePngRGBA(path, fb.width(), fb.height(), actual.data()));
            std::printf("    wrote %s\n", path.c_str());
            continue;
        }

        int w = 0, h = 0;
        std::vector<uint8_t> expected;
        if (!loadRGBA(path, w, h, expected)) {
            std::printf("    missing %s (run sr_tests --update-golden)\n", path.c_str());
            CHECK(false);
            continue;
        }
        CHECK(w == fb.width() && h == fb.height());
        if (w != fb.width() || h != fb.height()) continue;

        double sumDiff = 0.0;
        size_t bigDiffs = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            int d = std::abs(int(actual[i]) - int(expected[i]));
            sumDiff += d;
            if (d > 24) ++bigDiffs;
        }
        double meanDiff = sumDiff / actual.size();
        double bigFraction = static_cast<double>(bigDiffs) / actual.size();
        if (meanDiff > 0.5 || bigFraction > 0.002) {
            std::printf("    %s: mean diff %.3f, %.3f%% channels off by >24\n", name, meanDiff, bigFraction * 100);
            savePngRGBA(std::string("renders/golden_fail_") + name + ".png", w, h, actual.data());
        }
        CHECK(meanDiff <= 0.5);
        CHECK(bigFraction <= 0.002);
    }
}
