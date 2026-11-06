#include "viewer_app.h"

#include "core/image_io.h"

#include <algorithm>
#include <cstdio>

namespace sr {

ViewerApp::~ViewerApp()
{
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_presenter) SDL_DestroyRenderer(m_presenter);
    if (m_window) SDL_DestroyWindow(m_window);
    if (m_window) SDL_Quit();
}

bool ViewerApp::init(const ViewerOptions& options)
{
    m_options = options;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#ifdef __EMSCRIPTEN__
    // Render at the canvas' CSS size; high-DPI backing stores would quadruple the pixel count.
    Uint32 flags = 0;
#else
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | (options.resizable ? SDL_WINDOW_RESIZABLE : 0);
#endif
    m_window = SDL_CreateWindow("tiny-software-renderer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                options.width, options.height, flags);
    m_presenter = m_window ? SDL_CreateRenderer(m_window, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!m_window || !m_presenter) {
        std::fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        return false;
    }

    m_renderer = std::make_unique<Renderer>(options.threads);
    m_maxThreads = m_renderer->threadCount();
    m_settings.shadowMapSize = 1536;

    const auto& names = sceneNames();
    int start = static_cast<int>(std::find(names.begin(), names.end(), options.scene) - names.begin());
    if (!loadScene(start < static_cast<int>(names.size()) ? start : 0) && !loadScene(0)) return false;

    m_last = m_lastTitle = std::chrono::steady_clock::now();
    return true;
}

bool ViewerApp::loadScene(int index)
{
    const auto& names = sceneNames();
    if (index < 0 || index >= static_cast<int>(names.size())) return false;
    std::string error;
    Scene next;
    if (!buildScene(names[index], m_options.assets, next, &error)) {
        std::fprintf(stderr, "cannot load scene %s: %s\n", names[index].c_str(), error.c_str());
        return false;
    }
    m_scene = std::move(next);
    m_camera = m_scene.camera;
    m_sceneIndex = index;
    return true;
}

void ViewerApp::setThreadCount(int n)
{
    n = std::clamp(n, 1, m_maxThreads);
    if (n != m_renderer->threadCount()) m_renderer->setThreadCount(n);
}

void ViewerApp::setRenderScale(float scale)
{
    m_renderScale = std::clamp(scale, 0.25f, 1.0f);
}

void ViewerApp::handleEvent(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_QUIT: m_running = false; break;
    case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) m_dragging = true; break;
    case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) m_dragging = false; break;
    case SDL_MOUSEMOTION:
        if (m_dragging) m_camera.orbit(-e.motion.xrel * 0.008f, e.motion.yrel * 0.008f);
        break;
    case SDL_MOUSEWHEEL: m_camera.zoom(e.wheel.y > 0 ? 0.9f : 1.1f); break;
    case SDL_KEYDOWN: {
        SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_ESCAPE) m_running = false;
        else if (k >= SDLK_1 && k <= SDLK_4) loadScene(k - SDLK_1);
        else if (k == SDLK_r) resetCamera();
        else if (k == SDLK_v)
            m_settings.view = static_cast<DebugView>((static_cast<int>(m_settings.view) + 1) %
                                                     static_cast<int>(DebugView::Count));
        else if (k == SDLK_f) m_settings.wireframe = !m_settings.wireframe;
        else if (k == SDLK_s) m_settings.shadows = !m_settings.shadows;
        else if (k == SDLK_t) setThreadCount(threadCount() == 1 ? m_maxThreads : 1);
        else if (k == SDLK_h) m_heatmap = !m_heatmap;
        else if (k == SDLK_SPACE) m_paused = !m_paused;
        else if (k == SDLK_LEFTBRACKET) setRenderScale(m_renderScale * 0.5f);
        else if (k == SDLK_RIGHTBRACKET) setRenderScale(m_renderScale * 2.0f);
        else if (k == SDLK_p) saveScreenshot();
        break;
    }
    default: break;
    }
}

void ViewerApp::saveScreenshot()
{
#ifndef __EMSCRIPTEN__
    char path[64];
    std::snprintf(path, sizeof(path), "renders/screenshot_%03d.png", m_screenshotIndex++);
    std::vector<uint8_t> rgba(static_cast<size_t>(m_fb.width()) * m_fb.height() * 4);
    m_fb.resolveRGBA(rgba.data(), SceneRenderer::toneMapFor(m_settings.view), m_scene.exposure);
    if (savePngRGBA(path, m_fb.width(), m_fb.height(), rgba.data())) std::printf("saved %s\n", path);
#endif
}

// Blends a per-tile cost heatmap over the image (blue = cheap, red = expensive).
void ViewerApp::overlayHeatmap()
{
    const Renderer& r = *m_renderer;
    const auto& times = r.tileTimesMs();
    float maxT = 0.0f;
    for (float t : times) maxT = std::max(maxT, t);
    if (maxT <= 0.0f) return;

    const int w = m_texW, h = m_texH;
    for (int ty = 0; ty < r.tilesY(); ++ty) {
        for (int tx = 0; tx < r.tilesX(); ++tx) {
            float k = times[static_cast<size_t>(ty) * r.tilesX() + tx] / maxT;
            Vec3 heat = k < 0.5f ? lerp(Vec3(0.1f, 0.2f, 1.0f), Vec3(0.1f, 1.0f, 0.2f), k * 2.0f)
                                 : lerp(Vec3(0.1f, 1.0f, 0.2f), Vec3(1.0f, 0.1f, 0.05f), (k - 0.5f) * 2.0f);
            int x0 = tx * Renderer::kTileSize, y0 = ty * Renderer::kTileSize;
            int x1 = std::min(x0 + Renderer::kTileSize, w), y1 = std::min(y0 + Renderer::kTileSize, h);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    uint32_t& p = m_pixels[static_cast<size_t>(y) * w + x];
                    float a = (x == x0 || y == y0) ? 0.8f : 0.4f;
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

void ViewerApp::renderFrame()
{
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(m_presenter, &outW, &outH);
    int w = std::max(16, static_cast<int>(outW * m_renderScale));
    int h = std::max(16, static_cast<int>(outH * m_renderScale));
    if (w != m_texW || h != m_texH) {
        if (m_texture) SDL_DestroyTexture(m_texture);
        m_texture = SDL_CreateTexture(m_presenter, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        m_texW = w;
        m_texH = h;
        m_fb = Framebuffer(w, h);
        m_pixels.resize(static_cast<size_t>(w) * h);
    }

    m_renderer->setTileTiming(m_heatmap);
    auto t0 = std::chrono::steady_clock::now();
    m_sceneRenderer.render(*m_renderer, m_scene, m_camera, m_animTime, m_fb, m_settings);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_frameMs = m_frameMs == 0.0 ? ms : m_frameMs + (ms - m_frameMs) * 0.1;
    m_shadowMs = m_shadowMs + (m_sceneRenderer.shadowMs() - m_shadowMs) * 0.1;

    m_fb.resolveARGB(m_pixels.data(), SceneRenderer::toneMapFor(m_settings.view), m_scene.exposure);
    if (m_heatmap) overlayHeatmap();

    SDL_UpdateTexture(m_texture, nullptr, m_pixels.data(), w * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderCopy(m_presenter, m_texture, nullptr, nullptr);
    SDL_RenderPresent(m_presenter);
}

bool ViewerApp::tick()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);
    if (!m_running) return false;

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - m_last).count();
    m_last = now;
    if (!m_paused) m_animTime += std::min(dt, 0.1f);

    renderFrame();

    if (std::chrono::duration<float>(now - m_lastTitle).count() > 0.25f) {
        m_lastTitle = now;
        const RenderStats& s = m_renderer->stats();
        char title[256];
        std::snprintf(title, sizeof(title),
                      "tiny-software-renderer | %s | %s%s | %dx%d | %.1f ms (%.0f fps) | %llu tris | %d threads",
                      m_scene.name.c_str(), debugViewName(m_settings.view), m_settings.wireframe ? "+wire" : "",
                      m_texW, m_texH, m_frameMs, 1000.0 / m_frameMs,
                      static_cast<unsigned long long>(s.trianglesSubmitted), threadCount());
        SDL_SetWindowTitle(m_window, title);
    }
    return true;
}

std::string ViewerApp::statsJson() const
{
    const RenderStats& s = m_renderer->stats();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"frameMs\":%.2f,\"shadowMs\":%.2f,\"width\":%d,\"height\":%d,\"threads\":%d,"
                  "\"maxThreads\":%d,\"triangles\":%llu,\"rasterized\":%llu,\"culled\":%llu,"
                  "\"fragments\":%llu,\"scene\":%d,\"view\":%d}",
                  m_frameMs, m_shadowMs, m_texW, m_texH, threadCount(), m_maxThreads,
                  static_cast<unsigned long long>(s.trianglesSubmitted),
                  static_cast<unsigned long long>(s.trianglesRasterized),
                  static_cast<unsigned long long>(s.trianglesCulled),
                  static_cast<unsigned long long>(s.fragmentsShaded), m_sceneIndex,
                  static_cast<int>(m_settings.view));
    return buf;
}

} // namespace sr
