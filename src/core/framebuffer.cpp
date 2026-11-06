#include "core/framebuffer.h"

#include <algorithm>
#include <cmath>

namespace sr {

float linearToSrgb(float c)
{
    c = clamp01(c);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Krzysztof Narkowicz's fit of the ACES filmic curve.
Vec3 toneMapAces(const Vec3& c)
{
    auto f = [](float x) {
        const float a = 2.51f, b = 0.03f, cc = 2.43f, d = 0.59f, e = 0.14f;
        return clamp01((x * (a * x + b)) / (x * (cc * x + d) + e));
    };
    return {f(c.x), f(c.y), f(c.z)};
}

namespace {

// 4096-entry LUT for linear [0,1] -> sRGB 8-bit; resolve runs per pixel per frame.
struct SrgbLut {
    static constexpr int kSize = 4096;
    uint8_t table[kSize];
    SrgbLut()
    {
        for (int i = 0; i < kSize; ++i)
            table[i] = static_cast<uint8_t>(std::lround(linearToSrgb(i / float(kSize - 1)) * 255.0f));
    }
    uint8_t operator()(float c) const
    {
        int i = static_cast<int>(clamp01(c) * (kSize - 1) + 0.5f);
        return table[i];
    }
};

const SrgbLut& srgbLut()
{
    static const SrgbLut lut;
    return lut;
}

template <typename Write>
void resolvePixels(const std::vector<Vec3>& color, ToneMap toneMap, float exposure, Write write)
{
    const SrgbLut& lut = srgbLut();
    for (size_t i = 0; i < color.size(); ++i) {
        Vec3 c = color[i] * exposure;
        if (toneMap == ToneMap::AcesFilmic) c = toneMapAces(c);
        write(i, lut(c.x), lut(c.y), lut(c.z));
    }
}

} // namespace

Framebuffer::Framebuffer(int width, int height, bool hasColor) : m_wantsColor(hasColor)
{
    resize(width, height);
}

void Framebuffer::resize(int width, int height)
{
    m_width = width;
    m_height = height;
    size_t n = static_cast<size_t>(width) * height;
    m_depth.assign(n, 1.0f);
    if (m_wantsColor) m_color.assign(n, Vec3(0.0f));
}

void Framebuffer::clear(const Vec3& color, float depth)
{
    std::fill(m_color.begin(), m_color.end(), color);
    std::fill(m_depth.begin(), m_depth.end(), depth);
}

void Framebuffer::clearDepth(float depth)
{
    std::fill(m_depth.begin(), m_depth.end(), depth);
}

void Framebuffer::resolveARGB(uint32_t* out, ToneMap toneMap, float exposure) const
{
    resolvePixels(m_color, toneMap, exposure, [out](size_t i, uint8_t r, uint8_t g, uint8_t b) {
        out[i] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    });
}

void Framebuffer::resolveRGBA(uint8_t* out, ToneMap toneMap, float exposure) const
{
    resolvePixels(m_color, toneMap, exposure, [out](size_t i, uint8_t r, uint8_t g, uint8_t b) {
        out[i * 4 + 0] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = 255;
    });
}

} // namespace sr
