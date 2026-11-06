// sr_viewer: interactive window. SDL is only used to show the finished frame;
// every pixel is produced by the software renderer.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "core/image_io.h"
#include "core/renderer.h"
#include "render/scene.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#ifndef SR_ASSET_DIR
#define SR_ASSET_DIR "assets"
#endif

using namespace sr;

namespace {

const char* kHelp =
    "Controls\n"
    "  mouse drag     orbit camera        wheel      zoom\n"
    "  1-4            switch scene        R          reset camera\n"
    "  V              cycle debug view    F          wireframe overlay\n"
    "  S              toggle shadows      T          1 thread <-> all threads\n"
    "  H              tile cost heatmap   [ / ]      render scale\n"
    "  Space          pause animation     P          save screenshot\n"
    "  Esc            quit\n";

// Blends a per-tile cost heatmap over the image (blue = cheap, red = expensive).
void overlayHeatmap(std::vector<uint32_t>& pixels, int w, int h, const Renderer& r)
{
    const auto& times = r.tileTimesMs();
    float maxT = 0.0f;
    for (float t : times) maxT = std::max(maxT, t);
    if (maxT <= 0.0f) return;

    for (int ty = 0; ty < r.tilesY(); ++ty) {
        for (int tx = 0; tx < r.tilesX(); ++tx) {
            float k = times[static_cast<size_t>(ty) * r.tilesX() + tx] / maxT;
            Vec3 heat = k < 0.5f ? lerp(Vec3(0.1f, 0.2f, 1.0f), Vec3(0.1f, 1.0f, 0.2f), k * 2.0f)
                                 : lerp(Vec3(0.1f, 1.0f, 0.2f), Vec3(1.0f, 0.1f, 0.05f), (k - 0.5f) * 2.0f);
            int x0 = tx * Renderer::kTileSize, y0 = ty * Renderer::kTileSize;
            int x1 = std::min(x0 + Renderer::kTileSize, w), y1 = std::min(y0 + Renderer::kTileSize, h);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    uint32_t& p = pixels[static_cast<size_t>(y) * w + x];
                    bool border = x == x0 || y == y0;
                    float a = border ? 0.8f : 0.4f;
                    auto mix = [&](int shift, float c) {
                        float v = ((p >> shift) & 0xFF) / 255.0f;
                        return static_cast<uint32_t>(clamp01(lerp(v, c, a)) * 255.0f + 0.5f) << shift;
                    };
                    p = 0xFF000000u | mix(16, heat.x) | mix(8, heat.y) | mix(0, heat.z);
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string assets = SR_ASSET_DIR;
    std::string startScene = "materials";
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--assets") assets = argv[++i];
        else if (a == "--scene") startScene = argv[++i];
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int winW = 1280, winH = 720;
    SDL_Window* window = SDL_CreateWindow("tiny-software-renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          winW, winH, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* presenter = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!window || !presenter) {
        std::fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    std::printf("%s", kHelp);

    Renderer renderer(0);
    const int allThreads = renderer.threadCount();
    SceneRenderer sceneRenderer;
    RenderSettings settings;
    settings.shadowMapSize = 1536;

    Scene scene;
    OrbitCamera camera;
    auto loadScene = [&](const std::string& name) {
        std::string error;
        Scene next;
        if (!buildScene(name, assets, next, &error)) {
            std::fprintf(stderr, "cannot load scene %s: %s\n", name.c_str(), error.c_str());
            return false;
        }
        scene = std::move(next);
        camera = scene.camera;
        return true;
    };
    if (!loadScene(startScene) && !loadScene("materials")) return 1;

    float renderScale = 1.0f;
    Framebuffer fb;
    SDL_Texture* texture = nullptr;
    int texW = 0, texH = 0;
    std::vector<uint32_t> pixels;

    bool running = true, paused = false, dragging = false, heatmap = false;
    float animTime = 0.0f;
    double smoothedMs = 0.0;
    int screenshotIndex = 0;
    auto last = std::chrono::steady_clock::now();
    auto lastTitle = last;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) dragging = true; break;
            case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) dragging = false; break;
            case SDL_MOUSEMOTION:
                if (dragging) camera.orbit(-e.motion.xrel * 0.008f, e.motion.yrel * 0.008f);
                break;
            case SDL_MOUSEWHEEL: camera.zoom(e.wheel.y > 0 ? 0.9f : 1.1f); break;
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = false;
                else if (k >= SDLK_1 && k <= SDLK_4) loadScene(sceneNames()[k - SDLK_1]);
                else if (k == SDLK_r) camera = scene.camera;
                else if (k == SDLK_v)
                    settings.view = static_cast<DebugView>((static_cast<int>(settings.view) + 1) %
                                                           static_cast<int>(DebugView::Count));
                else if (k == SDLK_f) settings.wireframe = !settings.wireframe;
                else if (k == SDLK_s) settings.shadows = !settings.shadows;
                else if (k == SDLK_t) renderer.setThreadCount(renderer.threadCount() == 1 ? allThreads : 1);
                else if (k == SDLK_h) heatmap = !heatmap;
                else if (k == SDLK_SPACE) paused = !paused;
                else if (k == SDLK_LEFTBRACKET) renderScale = std::max(0.25f, renderScale * 0.5f);
                else if (k == SDLK_RIGHTBRACKET) renderScale = std::min(1.0f, renderScale * 2.0f);
                else if (k == SDLK_p) {
                    char path[64];
                    std::snprintf(path, sizeof(path), "renders/screenshot_%03d.png", screenshotIndex++);
                    std::vector<uint8_t> rgba(static_cast<size_t>(fb.width()) * fb.height() * 4);
                    fb.resolveRGBA(rgba.data(), SceneRenderer::toneMapFor(settings.view), scene.exposure);
                    if (savePngRGBA(path, fb.width(), fb.height(), rgba.data())) std::printf("saved %s\n", path);
                }
                break;
            }
            }
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (!paused) animTime += dt;

        SDL_GetRendererOutputSize(presenter, &winW, &winH);
        int w = std::max(16, static_cast<int>(winW * renderScale));
        int h = std::max(16, static_cast<int>(winH * renderScale));
        if (w != texW || h != texH) {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(presenter, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            texW = w;
            texH = h;
            fb = Framebuffer(w, h);
            pixels.resize(static_cast<size_t>(w) * h);
        }

        renderer.setTileTiming(heatmap);
        auto t0 = std::chrono::steady_clock::now();
        sceneRenderer.render(renderer, scene, camera, animTime, fb, settings);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        smoothedMs = smoothedMs == 0.0 ? ms : lerp(static_cast<float>(smoothedMs), static_cast<float>(ms), 0.1f);

        fb.resolveARGB(pixels.data(), SceneRenderer::toneMapFor(settings.view), scene.exposure);
        if (heatmap) overlayHeatmap(pixels, w, h, renderer);

        SDL_UpdateTexture(texture, nullptr, pixels.data(), w * static_cast<int>(sizeof(uint32_t)));
        SDL_RenderCopy(presenter, texture, nullptr, nullptr);
        SDL_RenderPresent(presenter);

        if (std::chrono::duration<float>(now - lastTitle).count() > 0.25f) {
            lastTitle = now;
            const RenderStats& s = renderer.stats();
            char title[256];
            std::snprintf(title, sizeof(title),
                          "tiny-software-renderer | %s | %s%s | %dx%d | %.1f ms (%.0f fps) | %llu tris | %d threads",
                          scene.name.c_str(), debugViewName(settings.view), settings.wireframe ? "+wire" : "", w, h,
                          smoothedMs, 1000.0 / smoothedMs, static_cast<unsigned long long>(s.trianglesSubmitted),
                          renderer.threadCount());
            SDL_SetWindowTitle(window, title);
        }
    }

    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(presenter);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
