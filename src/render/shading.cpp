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
        float g = std::exp(-linear / 3.5f); // near = white, fades to black with distance
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

// ---------------------------------------------------------------------------
// SIMD quad path. Mirrors fragment() above; texture and shadow-map fetches
// are gathers, so they run per lane, everything else runs 4-wide.

namespace {

inline V3 splat(float s) { return V3(Vec3(s)); }

} // namespace

V3 Environment::irradiance4(const V3& n) const
{
    const Vec3 below = lerp(ground, horizon * 0.55f, 0.35f);
    const Vec3 above = lerp(horizon, zenith, 0.5f);
    F4 t = smoothstep(0.0f, 1.0f, n.y * F4(0.5f) + F4(0.5f));
    return lerp(V3(below), V3(above), t) * F4(intensity);
}

V3 Environment::radiance4(const V3& dir, float roughness) const
{
    const F4 zero(0.0f);
    F4 up = sqrt(max(dir.y, zero));
    F4 down = sqrt(sqrt(max(-dir.y, zero)));
    V3 sky = lerp(V3(horizon), V3(zenith), up);
    V3 below = lerp(V3(horizon * 0.55f), V3(ground), down);
    V3 sharp = select(dir.y >= zero, sky, below) * F4(intensity);
    if (roughness <= 0.0f) return sharp;
    return lerp(sharp, irradiance4(dir), F4(roughness * roughness));
}

F4 ShadowMap::visibility4(const V3& worldPos, const V3& normal, const Vec3& lightDir, int laneMask) const
{
    // Normal-offset bias and light-space projection, 4 lanes at once.
    const F4 one(1.0f);
    const F4 nDotL = clamp01(dot(normal, V3(lightDir)));
    const V3 p = worldPos + normal * (F4(m_texelWorld) * (one + F4(2.0f) * (one - nDotL)));
    const Mat4& m = m_viewProj;
    const F4 cx = F4(m.m[0][0]) * p.x + F4(m.m[0][1]) * p.y + F4(m.m[0][2]) * p.z + F4(m.m[0][3]);
    const F4 cy = F4(m.m[1][0]) * p.x + F4(m.m[1][1]) * p.y + F4(m.m[1][2]) * p.z + F4(m.m[1][3]);
    const F4 cz = F4(m.m[2][0]) * p.x + F4(m.m[2][1]) * p.y + F4(m.m[2][2]) * p.z + F4(m.m[2][3]);

    float us[4], vs[4], ds[4];
    (cx * F4(0.5f) + F4(0.5f)).store(us);
    (F4(0.5f) - cy * F4(0.5f)).store(vs);
    (cz * F4(0.5f) + F4(0.5f) - F4(0.0008f)).store(ds);

    const float* depth = m_depth.depthData();
    float result[4] = {};
    for (int lane = 0; lane < 4; ++lane) {
        if (!(laneMask >> lane & 1)) continue;
        const float u = us[lane], v = vs[lane], d = ds[lane];
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f || d > 1.0f) {
            result[lane] = 1.0f;
            continue;
        }
        const float sx = u * m_size - 0.5f, sy = v * m_size - 0.5f;
        const float bx = std::floor(sx), by = std::floor(sy);
        const float fx = sx - bx, fy = sy - by;
        const int x0 = static_cast<int>(bx) - 1, y0 = static_cast<int>(by) - 1;
        const float wy[4] = {1.0f - fy, 1.0f, 1.0f, fy};
        const F4 wx(1.0f - fx, 1.0f, 1.0f, fx);
        const F4 ref(d);

        if (x0 >= 0 && x0 + 3 < m_size && y0 >= 0 && y0 + 3 < m_size) {
            // Interior: each 4-texel row is one unaligned SIMD load + compare.
            F4 acc(0.0f);
            const float* row = depth + static_cast<size_t>(y0) * m_size + x0;
            for (int j = 0; j < 4; ++j, row += m_size)
                acc += select(ref <= F4::load(row), wx, F4(0.0f)) * F4(wy[j]);
            float a[4];
            acc.store(a);
            result[lane] = (a[0] + a[1] + a[2] + a[3]) * (1.0f / 9.0f);
        } else {
            result[lane] = visibility(worldPos.lane(lane), normal.lane(lane), lightDir);
        }
    }
    return F4::load(result);
}

void PbrShader::fragmentQuad(const QuadInput& in, V3& color, M4& alive) const
{
    const FrameUniforms& f = *frame;
    // Rare debug views reuse the scalar shader (still fed with quad derivatives).
    if (f.view == DebugView::MipLevel || f.view == DebugView::Depth) {
        Shader::fragmentQuad(in, color, alive);
        return;
    }

    const F4* v = in.varyings;
    const int bits = alive.bits();
    const V3 p(v[kWorldPos], v[kWorldPos + 1], v[kWorldPos + 2]);
    V3 n = normalize(V3(v[kNormal], v[kNormal + 1], v[kNormal + 2]));
    if (!in.frontFacing) n = -n;

    // Texture coordinates, and one LOD per quad from coarse derivatives (like GPUs).
    const Vec2 dUVdx(in.ddx(v[kUv]), in.ddx(v[kUv + 1]));
    const Vec2 dUVdy(in.ddy(v[kUv]), in.ddy(v[kUv + 1]));
    auto sampleLanes = [&](const Texture& tex) {
        return tex.sampleQuad(v[kUv], v[kUv + 1], tex.computeLod(dUVdx, dUVdy), bits);
    };

    if (material.normalMap) {
        V3 t(v[kTangent], v[kTangent + 1], v[kTangent + 2]);
        t = normalize(t - n * dot(n, t));
        V3 b = cross(n, t) * select(v[kTangent + 3] < F4(0.0f), F4(-1.0f), F4(1.0f));
        V3 s = sampleLanes(*material.normalMap);
        const F4 strength(material.normalStrength);
        F4 tx = (s.x * F4(2.0f) - F4(1.0f)) * strength;
        F4 ty = (s.y * F4(2.0f) - F4(1.0f)) * strength;
        F4 tz = s.z * F4(2.0f) - F4(1.0f);
        n = normalize(t * tx + b * ty + n * tz);
    }

    V3 albedo(material.albedo);
    if (material.albedoMap) albedo = albedo * sampleLanes(*material.albedoMap);

    if (f.view == DebugView::Albedo) {
        color = albedo;
        return;
    }
    if (f.view == DebugView::Normals) {
        auto encode = [](float c) { return srgbToLinear(clamp01(c * 0.5f + 0.5f)); };
        color = V3(map(n.x, encode), map(n.y, encode), map(n.z, encode));
        return;
    }
    if (f.view == DebugView::Lighting) albedo = splat(0.75f);

    const float metallic = material.metallic;
    const float roughness = std::clamp(material.roughness, 0.04f, 1.0f);
    const F4 one(1.0f);
    const Vec3 Ls = -normalize(f.light.direction);
    const V3 L(Ls);
    const V3 V = normalize(V3(f.cameraPos) - p);
    const V3 H = normalize(V + L);
    const F4 nDotV = max(dot(n, V), F4(1e-4f));
    const F4 nDotL = max(dot(n, L), F4(0.0f));
    const V3 f0 = lerp(splat(0.04f), albedo, F4(metallic));

    V3 result(material.emissive);

    // Direct light.
    const M4 lit = (nDotL > F4(0.0f)) & alive;
    if (lit.any()) {
        const F4 shadow = f.shadow ? f.shadow->visibility4(p, n, Ls, lit.bits()) : one;

        const F4 nDotH = max(dot(n, H), F4(0.0f));
        const F4 m = one - clamp01(dot(H, V));
        const F4 m2 = m * m;
        const V3 F = f0 + (splat(1.0f) - f0) * (m2 * m2 * m);

        const float a = roughness * roughness, a2 = a * a;
        const F4 d = nDotH * nDotH * F4(a2 - 1.0f) + one;
        const F4 D = F4(a2) / (F4(kPi) * d * d);

        const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        const F4 G = (nDotV / (nDotV * F4(1.0f - k) + F4(k))) * (nDotL / (nDotL * F4(1.0f - k) + F4(k)));

        const V3 specular = F * (D * G / (F4(4.0f) * nDotV * nDotL + F4(1e-4f)));
        const V3 kd = (splat(1.0f) - F) * F4(1.0f - metallic);
        const Vec3 radiance = f.light.color * f.light.intensity;
        result += (kd * albedo * F4(1.0f / kPi) + specular) * V3(radiance) * (nDotL * shadow);
    }

    // Ambient: split-sum environment BRDF (Karis) with the analytic sky.
    {
        const float r0 = -1.0f * roughness + 1.0f, r1 = -0.0275f * roughness + 0.0425f;
        const float r2 = -0.572f * roughness + 1.04f, r3 = 0.022f * roughness - 0.04f;
        const F4 a004 = min(F4(r0 * r0), exp2(F4(-9.28f) * nDotV)) * F4(r0) + F4(r1);
        const F4 A = F4(-1.04f) * a004 + F4(r2);
        const F4 B = F4(1.04f) * a004 + F4(r3);
        const V3 envSpec = f0 * A + V3(B, B, B);
        const V3 kdAmbient = (splat(1.0f) - envSpec) * F4(1.0f - metallic);
        const V3 R = reflect(-V, n);
        const F4 horizonFade = clamp01(one + F4(1.2f) * dot(R, n));
        result += kdAmbient * albedo * f.env.irradiance4(n);
        result += envSpec * f.env.radiance4(R, roughness) * (horizonFade * horizonFade);
    }

    if (f.fogDensity > 0.0f) {
        const V3 toCam = V3(f.cameraPos) - p;
        const F4 dist = sqrt(dot(toCam, toCam)) * F4(f.fogDensity);
        const F4 fog = one - exp(-(dist * dist));
        const V3 viewDir = -V;
        const V3 horizonDir = normalize(V3(viewDir.x, max(viewDir.y, F4(0.0f)), viewDir.z));
        result = lerp(result, f.env.radiance4(horizonDir, 0.0f), fog);
    }

    color = result;
}

} // namespace sr
