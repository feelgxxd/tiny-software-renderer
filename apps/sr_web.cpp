// sr_web: WebAssembly entry point. The page (web/index.html) drives the
// viewer through the exported sr_* functions below.

#include "viewer_app.h"

#include <emscripten/emscripten.h>
#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>
#endif

#include <algorithm>
#include <string>

namespace {

sr::ViewerApp* g_app = nullptr;
std::string g_stats;

void mainLoop()
{
    if (g_app) g_app->tick();
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void sr_set_scene(int index) { g_app->loadScene(index); }
EMSCRIPTEN_KEEPALIVE void sr_set_view(int view) { g_app->setView(static_cast<sr::DebugView>(view)); }
EMSCRIPTEN_KEEPALIVE void sr_set_wireframe(int on) { g_app->setWireframe(on != 0); }
EMSCRIPTEN_KEEPALIVE void sr_set_shadows(int on) { g_app->setShadows(on != 0); }
EMSCRIPTEN_KEEPALIVE void sr_set_heatmap(int on) { g_app->setHeatmap(on != 0); }
EMSCRIPTEN_KEEPALIVE void sr_set_paused(int on) { g_app->setPaused(on != 0); }
EMSCRIPTEN_KEEPALIVE void sr_set_threads(int n) { g_app->setThreadCount(n); }
EMSCRIPTEN_KEEPALIVE void sr_set_scale(float scale) { g_app->setRenderScale(scale); }
EMSCRIPTEN_KEEPALIVE void sr_reset_camera() { g_app->resetCamera(); }
EMSCRIPTEN_KEEPALIVE const char* sr_stats()
{
    g_stats = g_app->statsJson();
    return g_stats.c_str();
}

} // extern "C"

int main()
{
    // Only take keyboard input while the canvas has focus, so the page stays usable.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

    sr::ViewerOptions options;
    options.assets = "/assets";
    options.width = 960;
    options.height = 540;
    options.resizable = false;
#ifdef __EMSCRIPTEN_PTHREADS__
    // Must not exceed PTHREAD_POOL_SIZE (min(cores, 16)): extra threads would never start.
    options.threads = std::min(emscripten_num_logical_cores(), 16);
#else
    options.threads = 1;
#endif

    static sr::ViewerApp app;
    if (!app.init(options)) return 1;
    g_app = &app;
    emscripten_run_script("if (Module.onRendererReady) Module.onRendererReady();");
    emscripten_set_main_loop(mainLoop, 0, 1);
    return 0;
}
