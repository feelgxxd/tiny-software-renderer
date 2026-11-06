// sr_viewer: desktop window for the software renderer.

#define SDL_MAIN_HANDLED
#include "viewer_app.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef SR_ASSET_DIR
#define SR_ASSET_DIR "assets"
#endif

static const char* kHelp =
    "Controls\n"
    "  mouse drag     orbit camera        wheel      zoom\n"
    "  1-4            switch scene        R          reset camera\n"
    "  V              cycle debug view    F          wireframe overlay\n"
    "  S              toggle shadows      T          1 thread <-> all threads\n"
    "  H              tile cost heatmap   [ / ]      render scale\n"
    "  Space          pause animation     P          save screenshot\n"
    "  X              SIMD <-> scalar shading\n"
    "  Esc            quit\n";

int main(int argc, char** argv)
{
    sr::ViewerOptions options;
    options.assets = SR_ASSET_DIR;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--assets") options.assets = argv[++i];
        else if (a == "--scene") options.scene = argv[++i];
        else if (a == "--threads") options.threads = std::atoi(argv[++i]);
    }

    SDL_SetMainReady();
    sr::ViewerApp app;
    if (!app.init(options)) return 1;
    std::printf("%s", kHelp);
    while (app.tick()) {}
    return 0;
}
