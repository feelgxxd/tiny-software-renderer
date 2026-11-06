#pragma once
// Demo scenes and the code that renders them (shadow pass + main pass).

#include "core/renderer.h"
#include "render/camera.h"
#include "render/shading.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sr {

struct SceneObject {
    std::shared_ptr<const Mesh> mesh;
    Mat4 transform = Mat4::identity();
    std::function<Mat4(float time)> animate; // optional; overrides transform
    Material material;
    bool castsShadow = true;

    // One shader instance per object: the renderer references shaders until end().
    PbrShader shader;
    ShadowDepthShader shadowShader;
};

struct Scene {
    std::string name;
    std::string description;
    std::vector<SceneObject> objects;
    std::vector<std::shared_ptr<Texture>> textures; // owned resources
    DirectionalLight light;
    Environment env;
    OrbitCamera camera;
    float exposure = 1.0f;
    float fogDensity = 0.02f;
    Vec3 shadowCenter{0.0f};
    float shadowRadius = 5.0f;
    // Optional idle camera motion (used by the CLI to render turntables).
    float turntableSpeed = 0.35f;
};

const std::vector<std::string>& sceneNames();
// assetDir is where optional downloaded models live (see assets/README.md).
bool buildScene(const std::string& name, const std::string& assetDir, Scene& out, std::string* error = nullptr);

struct RenderSettings {
    DebugView view = DebugView::Shaded;
    bool shadows = true;
    int shadowMapSize = 2048;
    bool wireframe = false;
};

class SceneRenderer {
public:
    void render(Renderer& renderer, Scene& scene, const OrbitCamera& camera, float time, Framebuffer& target,
                const RenderSettings& settings);

    static ToneMap toneMapFor(DebugView view)
    {
        return (view == DebugView::Shaded || view == DebugView::Lighting) ? ToneMap::AcesFilmic : ToneMap::None;
    }

    double shadowMs() const { return m_shadowMs; }
    double mainMs() const { return m_mainMs; }

private:
    void drawSky(Renderer& renderer, Framebuffer& target, const Mat4& invViewProj, const Vec3& cameraPos,
                 const Environment& env);

    ShadowMap m_shadow{2048};
    FrameUniforms m_frame;
    double m_shadowMs = 0.0;
    double m_mainMs = 0.0;
};

} // namespace sr
