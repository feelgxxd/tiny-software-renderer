#pragma once
// Render target: linear HDR color (optional) + depth.
// Color stays in linear float until resolve(), which applies exposure,
// tone mapping and sRGB encoding to produce displayable 8-bit pixels.

#include "math/math.h"

#include <cstdint>
#include <vector>

namespace sr {

enum class ToneMap { None, AcesFilmic };

class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height, bool hasColor = true);

    void resize(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool hasColor() const { return !m_color.empty(); }

    void clear(const Vec3& color, float depth = 1.0f);
    void clearDepth(float depth = 1.0f);

    Vec3* colorData() { return m_color.data(); }
    float* depthData() { return m_depth.data(); }
    const Vec3* colorData() const { return m_color.data(); }
    const float* depthData() const { return m_depth.data(); }

    Vec3 color(int x, int y) const { return m_color[static_cast<size_t>(y) * m_width + x]; }
    float depth(int x, int y) const { return m_depth[static_cast<size_t>(y) * m_width + x]; }

    // Writes 0xAARRGGBB pixels (what SDL_PIXELFORMAT_ARGB8888 expects).
    void resolveARGB(uint32_t* out, ToneMap toneMap, float exposure = 1.0f) const;
    // Writes tightly packed RGBA8 (what PNG writers expect).
    void resolveRGBA(uint8_t* out, ToneMap toneMap, float exposure = 1.0f) const;

private:
    int m_width = 0;
    int m_height = 0;
    bool m_wantsColor = true;
    std::vector<Vec3> m_color;
    std::vector<float> m_depth;
};

// Shared color helpers.
float linearToSrgb(float c);
float srgbToLinear(float c);
Vec3 toneMapAces(const Vec3& c);

} // namespace sr
