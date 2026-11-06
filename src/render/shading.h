#pragma once
// Materials, lights and the shader implementations used by the demo scenes.

#include "core/framebuffer.h"
#include "core/shader.h"
#include "core/texture.h"
#include "math/math.h"

namespace sr {

enum class DebugView { Shaded, Albedo, Normals, Lighting, MipLevel, Depth, Count };
const char* debugViewName(DebugView view);

struct Material {
    Vec3 albedo{0.8f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    Vec3 emissive{0.0f};
    const Texture* albedoMap = nullptr; // sRGB color
    const Texture* normalMap = nullptr; // tangent space, OpenGL convention (+Y up)
    float normalStrength = 1.0f;
    Vec2 uvScale{1.0f, 1.0f};
};

struct DirectionalLight {
    Vec3 direction = normalize(Vec3(-0.4f, -1.0f, -0.3f)); // direction light travels
    Vec3 color{1.0f};
    float intensity = 3.0f;
};

// Analytic sky used for the background and for image-based ambient light.
struct Environment {
    Vec3 zenith{0.18f, 0.32f, 0.62f};
    Vec3 horizon{0.70f, 0.78f, 0.88f};
    Vec3 ground{0.20f, 0.18f, 0.16f};
    float intensity = 1.0f;

    // Radiance arriving from direction dir; roughness blurs toward irradiance.
    Vec3 radiance(const Vec3& dir, float roughness = 0.0f) const;
    Vec3 irradiance(const Vec3& normal) const;
};

class ShadowMap {
public:
    explicit ShadowMap(int size = 2048);

    void resize(int size);
    // Fits an orthographic light frustum around a bounding sphere.
    void fit(const DirectionalLight& light, const Vec3& center, float radius);

    Framebuffer& target() { return m_depth; }
    const Mat4& lightViewProj() const { return m_viewProj; }
    int size() const { return m_size; }

    // Fraction of light reaching worldPos in [0, 1], 5x5 PCF.
    float visibility(const Vec3& worldPos, const Vec3& normal, const Vec3& lightDir) const;

private:
    int m_size;
    Framebuffer m_depth;
    Mat4 m_viewProj = Mat4::identity();
    float m_texelWorld = 0.01f;
};

// Per-frame data shared by every object's shader.
struct FrameUniforms {
    Mat4 viewProj = Mat4::identity();
    Vec3 cameraPos;
    float zNear = 0.1f, zFar = 100.0f;
    DirectionalLight light;
    Environment env;
    const ShadowMap* shadow = nullptr;
    DebugView view = DebugView::Shaded;
    float fogDensity = 0.0f; // exponential-squared distance fog toward the horizon color
};

// Metallic-roughness PBR: Cook-Torrance (GGX, Smith, Schlick) + sky ambient.
class PbrShader final : public Shader {
public:
    PbrShader();

    const FrameUniforms* frame = nullptr;
    Mat4 model = Mat4::identity();
    Mat4 normalMatrix = Mat4::identity();
    Material material;

    void setModel(const Mat4& m)
    {
        model = m;
        normalMatrix = m.normalMatrix();
    }

    Vec4 vertex(const Vertex& in, float* out) const override;
    bool fragment(const FragmentInput& in, Vec3& color) const override;
};

// Depth-only pass from the light's point of view.
class ShadowDepthShader final : public Shader {
public:
    ShadowDepthShader() { hasFragmentStage = false; }

    Mat4 lightMvp = Mat4::identity();

    Vec4 vertex(const Vertex& in, float*) const override { return lightMvp * Vec4(in.position, 1.0f); }
    bool fragment(const FragmentInput&, Vec3&) const override { return true; }
};

} // namespace sr
