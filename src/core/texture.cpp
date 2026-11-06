#include "core/texture.h"

#include "core/framebuffer.h"

#include <stb_image.h>

#include <cmath>

namespace sr {

namespace {

inline int wrap(int i, int n)
{
    if ((n & (n - 1)) == 0) return i & (n - 1); // power of two: two's-complement AND handles negatives
    int r = i % n;
    return r < 0 ? r + n : r;
}

const float* srgbDecodeTable()
{
    static float table[256];
    static bool init = [] {
        for (int i = 0; i < 256; ++i) table[i] = srgbToLinear(i / 255.0f);
        return true;
    }();
    (void)init;
    return table;
}

} // namespace

Texture Texture::load(const std::string& path, bool srgb)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) return Texture();
    Texture t = fromRGBA8(w, h, pixels, srgb);
    stbi_image_free(pixels);
    return t;
}

Texture Texture::fromRGBA8(int width, int height, const uint8_t* rgba8, bool srgb)
{
    const float* decode = srgbDecodeTable();
    std::vector<Vec4> texels(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < texels.size(); ++i) {
        const uint8_t* p = rgba8 + i * 4;
        if (srgb)
            texels[i] = {decode[p[0]], decode[p[1]], decode[p[2]], p[3] / 255.0f};
        else
            texels[i] = {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    }
    return fromLinear(width, height, std::move(texels));
}

Texture Texture::fromLinear(int width, int height, std::vector<Vec4> texels)
{
    Texture t;
    Level base;
    base.width = width;
    base.height = height;
    base.texels = std::move(texels);
    t.m_levels.push_back(std::move(base));
    t.buildMips();
    return t;
}

void Texture::buildMips()
{
    // Box-filter down to 1x1. Odd sizes clamp the second tap to the edge.
    while (m_levels.back().width > 1 || m_levels.back().height > 1) {
        const Level& src = m_levels.back();
        Level dst;
        dst.width = std::max(1, src.width / 2);
        dst.height = std::max(1, src.height / 2);
        dst.texels.resize(static_cast<size_t>(dst.width) * dst.height);

        for (int y = 0; y < dst.height; ++y) {
            int y0 = std::min(y * 2, src.height - 1);
            int y1 = std::min(y * 2 + 1, src.height - 1);
            for (int x = 0; x < dst.width; ++x) {
                int x0 = std::min(x * 2, src.width - 1);
                int x1 = std::min(x * 2 + 1, src.width - 1);
                dst.texels[static_cast<size_t>(y) * dst.width + x] =
                    (src.at(x0, y0) + src.at(x1, y0) + src.at(x0, y1) + src.at(x1, y1)) * 0.25f;
            }
        }
        m_levels.push_back(std::move(dst));
    }
}

Vec4 Texture::sampleNearest(Vec2 uv, int level) const
{
    const Level& l = m_levels[std::clamp(level, 0, levelCount() - 1)];
    int x = wrap(static_cast<int>(std::floor(uv.x * l.width)), l.width);
    int y = wrap(static_cast<int>(std::floor(uv.y * l.height)), l.height);
    return l.at(x, y);
}

Vec4 Texture::sampleBilinear(Vec2 uv, int level) const
{
    const Level& l = m_levels[std::clamp(level, 0, levelCount() - 1)];
    float fx = uv.x * l.width - 0.5f;
    float fy = uv.y * l.height - 0.5f;
    float flx = std::floor(fx), fly = std::floor(fy);
    float tx = fx - flx, ty = fy - fly;
    int x0 = wrap(static_cast<int>(flx), l.width), x1 = wrap(x0 + 1, l.width);
    int y0 = wrap(static_cast<int>(fly), l.height), y1 = wrap(y0 + 1, l.height);

    Vec4 top = l.at(x0, y0) * (1.0f - tx) + l.at(x1, y0) * tx;
    Vec4 bottom = l.at(x0, y1) * (1.0f - tx) + l.at(x1, y1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

Vec4 Texture::sampleTrilinear(Vec2 uv, float lod) const
{
    float maxLod = static_cast<float>(levelCount() - 1);
    lod = std::clamp(lod, 0.0f, maxLod);
    int l0 = static_cast<int>(lod);
    float t = lod - static_cast<float>(l0);
    Vec4 a = sampleBilinear(uv, l0);
    if (t <= 0.0f || l0 + 1 >= levelCount()) return a;
    Vec4 b = sampleBilinear(uv, l0 + 1);
    return a * (1.0f - t) + b * t;
}

float Texture::computeLod(Vec2 dUVdx, Vec2 dUVdy) const
{
    Vec2 size(static_cast<float>(width()), static_cast<float>(height()));
    float rho2 = std::max(dot(dUVdx * size, dUVdx * size), dot(dUVdy * size, dUVdy * size));
    return rho2 <= 1.0f ? 0.0f : 0.5f * std::log2(rho2);
}

Vec4 Texture::sample(Vec2 uv, Vec2 dUVdx, Vec2 dUVdy) const
{
    return sampleTrilinear(uv, computeLod(dUVdx, dUVdy));
}

} // namespace sr
