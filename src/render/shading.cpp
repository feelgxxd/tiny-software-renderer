#include "render/shading.h"

#include <cmath>

namespace sr {

const char* debugViewName(DebugView view)
{
    switch (view) {
    case DebugView::Shaded: return "shaded";
    case DebugView::Albedo: return "albedo";
    case DebugView::Normals: return "normals";
    case DebugView::Lighting: return "lighting";
    case DebugView::MipLevel: return "mips";
    case DebugView::Depth: return "depth";
    default: return "?";
    }
}

// ---------------------------------------------------------------------------
// Environment

Vec3 Environment::radiance(const Vec3& dir, float roughness) const
{
    // sqrt gives the gradient a fast falloff near the horizon (and is far cheaper than pow).
    Vec3 sharp = dir.y >= 0.0f ? lerp(horizon, zenith, std::sqrt(dir.y))
                               : lerp(horizon * 0.55f, ground, std::sqrt(std::sqrt(-dir.y)));
    sharp = sharp * intensity;
    if (roughness <= 0.0f) return sharp;
    // Cheap stand-in for a prefiltered environment map.
    return lerp(sharp, irradiance(dir), roughness * roughness);
}

Vec3 Environment::irradiance(const Vec3& n) const
{
    float t = n.y * 0.5f + 0.5f;
    Vec3 below = lerp(ground, horizon * 0.55f, 0.35f);
    Vec3 above = lerp(horizon, zenith, 0.5f);
    return lerp(below, above, smoothstep(0.0f, 1.0f, t)) * intensity;
}

// ---------------------------------------------------------------------------
// Shadow map

ShadowMap::ShadowMap(int size) : m_size(size), m_depth(size, size, /*hasColor=*/false) {}

void ShadowMap::resize(int size)
{
    if (size == m_size) return;
    m_size = size;
    m_depth = Framebuffer(size, size, false);
}

void ShadowMap::fit(const DirectionalLight& light, const Vec3& center, float radius)
{
    Vec3 dir = normalize(light.direction);
    Vec3 up = std::fabs(dir.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    Vec3 eye = center - dir * (radius * 2.0f);
    Mat4 view = Mat4::lookAt(eye, center, up);
    Mat4 proj = Mat4::orthographic(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f);
    m_viewProj = proj * view;
    m_texelWorld = (2.0f * radius) / static_cast<float>(m_size);
}

float ShadowMap::visibility(const Vec3& worldPos, const Vec3& normal, const Vec3& lightDir) const
{
    // Normal-offset bias: push the lookup along the surface normal, more at grazing angles.
    float nDotL = clamp01(dot(normal, lightDir));
    Vec3 p = worldPos + normal * (m_texelWorld * (1.0f + 2.0f * (1.0f - nDotL)));
    Vec4 clip = m_viewProj * Vec4(p, 1.0f);

    float u = clip.x * 0.5f + 0.5f;
    float v = 0.5f - clip.y * 0.5f;
    float d = clip.z * 0.5f + 0.5f - 0.0008f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f || d > 1.0f) return 1.0f;

    // 3x3 grid of bilinear PCF taps = 4x4 texels with tent weights. Smooth
    // penumbrae from 16 depth compares instead of a blocky 5x5 box.
    const float* depth = m_depth.depthData();
    float sx = u * m_size - 0.5f, sy = v * m_size - 0.5f;
    float bx = std::floor(sx), by = std::floor(sy);
    float fx = sx - bx, fy = sy - by;
    const float wx[4] = {1.0f - fx, 1.0f, 1.0f, fx};
    const float wy[4] = {1.0f - fy, 1.0f, 1.0f, fy};
    int x0 = static_cast<int>(bx) - 1, y0 = static_cast<int>(by) - 1;

    float lit = 0.0f;
    for (int j = 0; j < 4; ++j) {
        int y = std::clamp(y0 + j, 0, m_size - 1);
        const float* row = depth + static_cast<size_t>(y) * m_size;
        float rowLit = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int x = std::clamp(x0 + i, 0, m_size - 1);
            rowLit += d <= row[x] ? wx[i] : 0.0f;
        }
        lit += rowLit * wy[j];
    }
    return lit * (1.0f / 9.0f);
}

// ---------------------------------------------------------------------------
// PBR shader

namespace {

// Varying layout.
constexpr int kWorldPos = 0; // 3
constexpr int kNormal = 3;   // 3
constexpr int kUv = 6;       // 2
constexpr int kTangent = 8;  // 4

float distributionGgx(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float geometrySmith(float nDotV, float nDotL, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    float gv = nDotV / (nDotV * (1.0f - k) + k);
    float gl = nDotL / (nDotL * (1.0f - k) + k);
    return gv * gl;
}

Vec3 fresnelSchlick(float cosTheta, const Vec3& f0)
{
    float m = 1.0f - clamp01(cosTheta);
    float m2 = m * m;
    float f = m2 * m2 * m; // pow(m, 5) without the libm call
    return f0 + (Vec3(1.0f) - f0) * f;
}

// Karis' analytic approximation of the split-sum environment BRDF.
Vec3 envBrdfApprox(const Vec3& f0, float roughness, float nDotV)
{
    const Vec4 c0(-1.0f, -0.0275f, -0.572f, 0.022f);
    const Vec4 c1(1.0f, 0.0425f, 1.04f, -0.04f);
    Vec4 r = c0 * roughness + c1;
    float a004 = std::min(r.x * r.x, std::exp2(-9.28f * nDotV)) * r.x + r.y;
    float a = -1.04f * a004 + r.z;
    float b = 1.04f * a004 + r.w;
    return f0 * a + Vec3(b);
}

Vec3 mipColor(float lod)
{
    static const Vec3 palette[] = {{1.0f, 0.15f, 0.15f}, {1.0f, 0.55f, 0.1f}, {0.95f, 0.9f, 0.15f},
                                   {0.25f, 0.85f, 0.3f}, {0.15f, 0.8f, 0.9f}, {0.2f, 0.35f, 1.0f},
                                   {0.7f, 0.3f, 0.95f}};
    constexpr int n = sizeof(palette) / sizeof(palette[0]);
    lod = std::clamp(lod, 0.0f, float(n - 1));
    int i = std::min(static_cast<int>(lod), n - 2);
    return lerp(palette[i], palette[i + 1], lod - static_cast<float>(i));
}

Vec3 debugEncode(const Vec3& displayColor)
{
    // Debug colors are meant to be displayed as-is; undo the sRGB encode in resolve.
    return {srgbToLinear(clamp01(displayColor.x)), srgbToLinear(clamp01(displayColor.y)),
            srgbToLinear(clamp01(displayColor.z))};
}

} // namespace

PbrShader::PbrShader()
{
    varyingCount = 12;
    uvSlot = kUv;
}

Vec4 PbrShader::vertex(const Vertex& in, float* out) const
{
    Vec4 world = model * Vec4(in.position, 1.0f);
    Vec3 n = normalMatrix.transformDir(in.normal);
    Vec3 t = model.transformDir(in.tangent.xyz());
    Vec2 uv = in.uv * material.uvScale;

    out[kWorldPos + 0] = world.x;
    out[kWorldPos + 1] = world.y;
    out[kWorldPos + 2] = world.z;
    out[kNormal + 0] = n.x;
    out[kNormal + 1] = n.y;
    out[kNormal + 2] = n.z;
    out[kUv + 0] = uv.x;
    out[kUv + 1] = uv.y;
    out[kTangent + 0] = t.x;
    out[kTangent + 1] = t.y;
    out[kTangent + 2] = t.z;
    out[kTangent + 3] = in.tangent.w;
    return frame->viewProj * world;
}

bool PbrShader::fragment(const FragmentInput& in, Vec3& color) const
{
    const float* v = in.varyings;
    const FrameUniforms& f = *frame;
    const Vec3 p(v[kWorldPos], v[kWorldPos + 1], v[kWorldPos + 2]);
    const Vec2 uv(v[kUv], v[kUv + 1]);

    Vec3 n = normalize(Vec3(v[kNormal], v[kNormal + 1], v[kNormal + 2]));
    if (!in.frontFacing) n = -n;

    if (material.normalMap && f.view != DebugView::MipLevel) {
        Vec3 t = Vec3(v[kTangent], v[kTangent + 1], v[kTangent + 2]);
        t = normalize(t - n * dot(n, t));
        Vec3 b = cross(n, t) * (v[kTangent + 3] < 0.0f ? -1.0f : 1.0f);
        Vec4 s = material.normalMap->sample(uv, in.dUVdx, in.dUVdy);
        Vec3 tn(s.x * 2.0f - 1.0f, s.y * 2.0f - 1.0f, s.z * 2.0f - 1.0f);
        tn.x *= material.normalStrength;
        tn.y *= material.normalStrength;
        n = normalize(t * tn.x + b * tn.y + n * tn.z);
    }

    Vec3 albedo = material.albedo;
    if (material.albedoMap) albedo = albedo * material.albedoMap->sample(uv, in.dUVdx, in.dUVdy).xyz();

    switch (f.view) {
    case DebugView::Albedo:
        color = albedo;
        return true;
    case DebugView::Normals:
        color = debugEncode(n * 0.5f + Vec3(0.5f));
        return true;
    case DebugView::MipLevel: {
        float lod = material.albedoMap ? material.albedoMap->computeLod(in.dUVdx, in.dUVdy)
                                       : 0.5f * std::log2(std::max(1.0f, std::max(dot(in.dUVdx, in.dUVdx),
                                                                                  dot(in.dUVdy, in.dUVdy)) *
                                                                             1024.0f * 1024.0f));
        float luma = dot(albedo, Vec3(0.2126f, 0.7152f, 0.0722f));
        color = debugEncode(mipColor(lod) * (0.55f + 0.45f * std::sqrt(clamp01(luma))));
        return true;
    }
    case DebugView::Depth: {
        float zNdc = in.depth * 2.0f - 1.0f;
        float linear = (2.0f * f.zNear * f.zFar) / (f.zFar + f.zNear - zNdc * (f.zFar - f.zNear));
        float g = 1.0f - clamp01(std::log2(linear + 1.0f) / std::log2(f.zFar + 1.0f) * 1.6f);
        color = debugEncode(Vec3(g));
        return true;
    }
    case DebugView::Lighting:
        albedo = Vec3(0.75f);
        break;
    default:
        break;
    }

    const float metallic = material.metallic;
    const float roughness = std::clamp(material.roughness, 0.04f, 1.0f);
    const Vec3 V = normalize(f.cameraPos - p);
    const Vec3 L = -normalize(f.light.direction);
    const Vec3 H = normalize(V + L);
    const float nDotV = std::max(dot(n, V), 1e-4f);
    const float nDotL = std::max(dot(n, L), 0.0f);
    const Vec3 f0 = lerp(Vec3(0.04f), albedo, metallic);

    Vec3 result = material.emissive;

    // Direct light.
    if (nDotL > 0.0f) {
        float shadow = f.shadow ? f.shadow->visibility(p, n, L) : 1.0f;
        if (shadow > 0.0f) {
            float nDotH = std::max(dot(n, H), 0.0f);
            Vec3 F = fresnelSchlick(std::max(dot(H, V), 0.0f), f0);
            float D = distributionGgx(nDotH, roughness);
            float G = geometrySmith(nDotV, nDotL, roughness);
            Vec3 specular = F * (D * G / (4.0f * nDotV * nDotL + 1e-4f));
            Vec3 kd = (Vec3(1.0f) - F) * (1.0f - metallic);
            Vec3 radiance = f.light.color * f.light.intensity;
            result += (kd * albedo / kPi + specular) * radiance * (nDotL * shadow);
        }
    }

    // Ambient from the analytic sky (diffuse irradiance + split-sum specular).
    Vec3 envSpec = envBrdfApprox(f0, roughness, nDotV);
    Vec3 kdAmbient = (Vec3(1.0f) - envSpec) * (1.0f - metallic);
    Vec3 r = reflect(-V, n);
    // Cheap horizon occlusion: reflections pointing into the surface fade out.
    float horizonFade = clamp01(1.0f + 1.2f * dot(r, n));
    result += kdAmbient * albedo * f.env.irradiance(n);
    result += envSpec * f.env.radiance(r, roughness) * (horizonFade * horizonFade);

    // Distance fog blends into the sky color at the horizon, hiding the edge of the world.
    if (f.fogDensity > 0.0f) {
        float dist = length(f.cameraPos - p) * f.fogDensity;
        float fog = 1.0f - std::exp(-dist * dist);
        Vec3 viewDir = -V;
        Vec3 horizonDir = normalize(Vec3(viewDir.x, std::max(viewDir.y, 0.0f), viewDir.z));
        result = lerp(result, f.env.radiance(horizonDir), fog);
    }

    color = result;
    return true;
}

} // namespace sr
