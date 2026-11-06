#pragma once
// Interactive viewer shared by the desktop app (sr_viewer) and the
// WebAssembly demo (sr_web). SDL only presents the finished frame.

#include "core/renderer.h"
#include "render/scene.h"

#include <SDL.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sr {

struct ViewerOptions {
    std::string assets;
    std::string scene = "materials";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    int threads = 0; // 0 = all hardware threads
};

class ViewerApp {
public:
    ~ViewerApp();

    bool init(const ViewerOptions& options);
    // Polls events and renders + presents one frame. Returns false on quit.
    bool tick();

    // Controls (keyboard shortcuts and the web UI both go through these).
    bool loadScene(int index);
    void setView(DebugView view) { m_settings.view = view; }
    void setWireframe(bool on) { m_settings.wireframe = on; }
    void setShadows(bool on) { m_settings.shadows = on; }
    void setHeatmap(bool on) { m_heatmap = on; }
    void setPaused(bool on) { m_paused = on; }
    void setThreadCount(int n);
    void setRenderScale(float scale);
    void resetCamera() { m_camera = m_scene.camera; }

    int sceneIndex() const { return m_sceneIndex; }
    int threadCount() const { return m_renderer ? m_renderer->threadCount() : 1; }
    int maxThreads() const { return m_maxThreads; }
    const RenderSettings& settings() const { return m_settings; }
    bool heatmap() const { return m_heatmap; }
    bool paused() const { return m_paused; }
    float renderScale() const { return m_renderScale; }

    // One-line JSON with timing and pipeline statistics for UI overlays.
    std::string statsJson() const;

private:
    void handleEvent(const SDL_Event& e);
    void renderFrame();
    void overlayHeatmap();
    void saveScreenshot();

    ViewerOptions m_options;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_presenter = nullptr;
    SDL_Texture* m_texture = nullptr;
    int m_texW = 0, m_texH = 0;

    std::unique_ptr<Renderer> m_renderer;
    int m_maxThreads = 1;
    SceneRenderer m_sceneRenderer;
    RenderSettings m_settings;
    Scene m_scene;
    int m_sceneIndex = 0;
    OrbitCamera m_camera;
    Framebuffer m_fb;
    std::vector<uint32_t> m_pixels;

    float m_renderScale = 1.0f;
    bool m_running = true;
    bool m_paused = false;
    bool m_dragging = false;
    bool m_heatmap = false;
    float m_animTime = 0.0f;
    double m_frameMs = 0.0;   // smoothed render time
    double m_shadowMs = 0.0;  // smoothed shadow pass time
    [[maybe_unused]] int m_screenshotIndex = 0; // desktop only
    std::chrono::steady_clock::time_point m_last;
    std::chrono::steady_clock::time_point m_lastTitle;
};

} // namespace sr
