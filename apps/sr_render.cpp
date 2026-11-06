// sr_render: headless renderer. Writes PNGs and runs benchmarks.
//
//   sr_render --scene materials --width 1280 --height 720 --out renders/materials.png
//   sr_render --scene shadows --frames 120 --out renders/shadows/frame.png
//   sr_render --scene all --bench

#include "core/image_io.h"
#include "core/renderer.h"
#include "render/scene.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef SR_ASSET_DIR
#define SR_ASSET_DIR "assets"
#endif

using namespace sr;

namespace {

struct Options {
    std::string scene = "materials";
    std::string out;
    std::string assets = SR_ASSET_DIR;
    int width = 1280;
    int height = 720;
    int threads = 0;
    int frames = 1;
    float fps = 30.0f;
    float time = 0.0f;
    float turntable = -1.0f; // < 0: use the scene default
    bool bench = false;
    bool benchThreads = false;
    bool simd = true;
    int benchFrames = 20;
    RenderSettings settings;
};

void usage()
{
    std::printf(
        "usage: sr_render [options]\n"
        "  --scene NAME       materials | shadows | textures | spot | all   (default materials)\n"
        "  --width N --height N  output size (default 1280x720)\n"
        "  --out PATH         output PNG (default renders/<scene>[_<view>].png)\n"
        "  --view NAME        shaded | albedo | normals | lighting | mips | depth\n"
        "  --wireframe        overlay triangle edges\n"
        "  --no-shadows       disable the shadow pass\n"
        "  --shadow-size N    shadow map resolution (default 2048)\n"
        "  --threads N        worker threads (default: all hardware threads)\n"
        "  --time T           animation time in seconds (default 0)\n"
        "  --turntable S      camera orbit speed in rad/s (default: per scene)\n"
        "  --frames N         render an N-frame sequence (PATH gets _0000 suffixes)\n"
        "  --fps F            sequence frame rate (default 30)\n"
        "  --bench            scalar vs. SIMD, 1 vs. all threads (markdown table)\n"
        "  --bench-threads    frame time for 1, 2, 4, ... N threads\n"
        "  --no-simd          use the scalar per-pixel shading path\n"
        "  --bench-frames N   frames per benchmark run (default 20)\n"
        "  --assets DIR       asset directory (default %s)\n",
        SR_ASSET_DIR);
}

bool parseView(const std::string& name, DebugView& out)
{
    for (int i = 0; i < static_cast<int>(DebugView::Count); ++i) {
        if (name == debugViewName(static_cast<DebugView>(i))) {
            out = static_cast<DebugView>(i);
            return true;
        }
    }
    return false;
}

bool parseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--scene") o.scene = next();
        else if (a == "--out") o.out = next();
        else if (a == "--assets") o.assets = next();
        else if (a == "--width") o.width = std::atoi(next());
        else if (a == "--height") o.height = std::atoi(next());
        else if (a == "--threads") o.threads = std::atoi(next());
        else if (a == "--frames") o.frames = std::max(1, std::atoi(next()));
        else if (a == "--fps") o.fps = static_cast<float>(std::atof(next()));
        else if (a == "--time") o.time = static_cast<float>(std::atof(next()));
        else if (a == "--turntable") o.turntable = static_cast<float>(std::atof(next()));
        else if (a == "--bench") o.bench = true;
        else if (a == "--bench-threads") o.bench = o.benchThreads = true;
        else if (a == "--no-simd") o.simd = false;
        else if (a == "--bench-frames") o.benchFrames = std::max(1, std::atoi(next()));
        else if (a == "--wireframe") o.settings.wireframe = true;
        else if (a == "--no-shadows") o.settings.shadows = false;
        else if (a == "--shadow-size") o.settings.shadowMapSize = std::max(64, std::atoi(next()));
        else if (a == "--view") {
            std::string v = next();
            if (!parseView(v, o.settings.view)) {
                std::fprintf(stderr, "unknown view '%s'\n", v.c_str());
                return false;
            }
        } else if (a == "--help" || a == "-h") {
            usage();
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", a.c_str());
            usage();
            return false;
        }
    }
    if (o.width < 16 || o.height < 16 || o.width > 8192 || o.height > 8192) {
        std::fprintf(stderr, "resolution must be between 16 and 8192\n");
        return false;
    }
    return true;
}

std::string defaultOut(const Options& o, const std::string& scene)
{
    std::string name = "renders/" + scene;
    if (o.settings.view != DebugView::Shaded) name += std::string("_") + debugViewName(o.settings.view);
    if (o.settings.wireframe) name += "_wire";
    return name + ".png";
}

std::string frameName(const std::string& path, int frame)
{
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%04d", frame);
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return path + suffix + ".png";
    return path.substr(0, dot) + suffix + path.substr(dot);
}

double renderOnce(Renderer& renderer, SceneRenderer& sceneRenderer, Scene& scene, float time, Framebuffer& fb,
                  const RenderSettings& settings)
{
    OrbitCamera cam = scene.camera;
    cam.yaw += time * scene.turntableSpeed;
    auto t0 = std::chrono::steady_clock::now();
    sceneRenderer.render(renderer, scene, cam, time, fb, settings);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Median frame time over o.benchFrames frames (after warm-up).
double measure(const Options& o, Scene& scene, Framebuffer& fb, int threads, bool simd)
{
    Renderer renderer(threads);
    renderer.setSimdShading(simd);
    SceneRenderer sceneRenderer;
    for (int i = 0; i < 3; ++i) renderOnce(renderer, sceneRenderer, scene, 0.0f, fb, o.settings);
    std::vector<double> times;
    for (int i = 0; i < o.benchFrames; ++i)
        times.push_back(renderOnce(renderer, sceneRenderer, scene, i / 30.0f, fb, o.settings));
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

int runBench(const Options& o, const std::vector<std::string>& scenes)
{
    const int maxThreads = ThreadPool::hardwareThreads();
    std::printf("Resolution %dx%d, median of %d frames, %d hardware threads, SIMD backend: %s\n\n", o.width,
                o.height, o.benchFrames, maxThreads, Renderer::simdBackend());
    std::printf("| Scene | Triangles | Scalar, 1 thread | SIMD, 1 thread | SIMD, %d threads | SIMD gain | Total gain |\n",
                maxThreads);
    std::printf("|---|---:|---:|---:|---:|---:|---:|\n");

    Framebuffer fb(o.width, o.height);
    for (const std::string& name : scenes) {
        Scene scene;
        std::string error;
        if (!buildScene(name, o.assets, scene, &error)) {
            std::fprintf(stderr, "skipping %s: %s\n", name.c_str(), error.c_str());
            continue;
        }
        size_t tris = 0;
        for (const auto& obj : scene.objects) tris += obj.mesh->triangleCount();

        double scalar1 = measure(o, scene, fb, 1, false);
        double simd1 = measure(o, scene, fb, 1, true);
        double simdN = measure(o, scene, fb, maxThreads, true);
        std::printf("| %s | %zu | %.1f ms | %.1f ms | **%.1f ms** (%.0f FPS) | %.2fx | %.1fx |\n", name.c_str(), tris,
                    scalar1, simd1, simdN, 1000.0 / simdN, scalar1 / simd1, scalar1 / simdN);
        std::fflush(stdout);
    }
    return 0;
}

int runThreadBench(const Options& o, const std::vector<std::string>& scenes)
{
    std::vector<int> threadCounts;
    int maxThreads = ThreadPool::hardwareThreads();
    for (int t = 1; t < maxThreads; t *= 2) threadCounts.push_back(t);
    threadCounts.push_back(maxThreads);

    std::printf("Resolution %dx%d, %d frames per run, %d hardware threads\n\n", o.width, o.height, o.benchFrames,
                maxThreads);
    std::printf("| Scene | Triangles | Threads | Frame (ms) | FPS | Speedup |\n");
    std::printf("|---|---:|---:|---:|---:|---:|\n");

    Framebuffer fb(o.width, o.height);
    for (const std::string& name : scenes) {
        Scene scene;
        std::string error;
        if (!buildScene(name, o.assets, scene, &error)) {
            std::fprintf(stderr, "skipping %s: %s\n", name.c_str(), error.c_str());
            continue;
        }
        size_t tris = 0;
        for (const auto& obj : scene.objects) tris += obj.mesh->triangleCount();

        double baseline = 0.0;
        for (int threads : threadCounts) {
            double median = measure(o, scene, fb, threads, o.simd);
            if (threads == 1) baseline = median;
            std::printf("| %s | %zu | %d | %.2f | %.1f | %.2fx |\n", name.c_str(), tris, threads, median,
                        1000.0 / median, baseline / median);
            std::fflush(stdout);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    Options o;
    if (!parseArgs(argc, argv, o)) return 2;

    std::vector<std::string> scenes;
    if (o.scene == "all") scenes = sceneNames();
    else scenes.push_back(o.scene);

    if (o.bench) return o.benchThreads ? runThreadBench(o, scenes) : runBench(o, scenes);

    Renderer renderer(o.threads);
    renderer.setSimdShading(o.simd);
    SceneRenderer sceneRenderer;
    Framebuffer fb(o.width, o.height);
    int failures = 0;

    for (const std::string& name : scenes) {
        Scene scene;
        std::string error;
        if (!buildScene(name, o.assets, scene, &error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            ++failures;
            continue;
        }
        if (o.turntable >= 0.0f) scene.turntableSpeed = o.turntable;

        std::string out = (o.out.empty() || scenes.size() > 1) ? defaultOut(o, name) : o.out;
        for (int f = 0; f < o.frames; ++f) {
            float time = o.time + static_cast<float>(f) / o.fps;
            double ms = renderOnce(renderer, sceneRenderer, scene, time, fb, o.settings);
            std::string path = o.frames > 1 ? frameName(out, f) : out;
            if (!savePng(path, fb, SceneRenderer::toneMapFor(o.settings.view), scene.exposure)) {
                std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
                return 1;
            }
            const RenderStats& s = renderer.stats();
            std::printf("%s  %.2f ms [shadow %.1f, main %.1f, sky+other %.1f]  (%llu tris in, %llu rasterized, "
                        "%llu fragments, %d threads, %s)\n",
                        path.c_str(), ms, sceneRenderer.shadowMs(), sceneRenderer.mainMs(),
                        ms - sceneRenderer.shadowMs() - sceneRenderer.mainMs(),
                        static_cast<unsigned long long>(s.trianglesSubmitted),
                        static_cast<unsigned long long>(s.trianglesRasterized),
                        static_cast<unsigned long long>(s.fragmentsShaded), renderer.threadCount(),
                        renderer.simdShading() ? Renderer::simdBackend() : "scalar");
        }
    }
    return failures == 0 ? 0 : 1;
}
