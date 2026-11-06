#pragma once
// 2D texture with a full mip chain and trilinear filtering.
// Texels are stored as linear float RGBA; sRGB sources are decoded at load
// time so filtering and mip generation happen in linear space.

#include "math/math.h"

#include <string>
#include <vector>

namespace sr {

class Texture {
public:
    Texture() = default;

    // Returns an invalid texture (valid() == false) if the file can't be read.
    static Texture load(const std::string& path, bool srgb);
    // rgba8 is tightly packed; srgb selects color vs. data decoding.
    static Texture fromRGBA8(int width, int height, const uint8_t* rgba8, bool srgb);
    static Texture fromLinear(int width, int height, std::vector<Vec4> texels);

    bool valid() const { return !m_levels.empty(); }
    int width() const { return valid() ? m_levels[0].width : 0; }
    int height() const { return valid() ? m_levels[0].height : 0; }
    int levelCount() const { return static_cast<int>(m_levels.size()); }

    // Wrap mode is repeat. uv (0,0) is the top-left of the image.
    Vec4 sampleNearest(Vec2 uv, int level = 0) const;
    Vec4 sampleBilinear(Vec2 uv, int level = 0) const;
    Vec4 sampleTrilinear(Vec2 uv, float lod) const;

    // Picks the LOD from screen-space UV derivatives (like GPU hardware does).
    Vec4 sample(Vec2 uv, Vec2 dUVdx, Vec2 dUVdy) const;
    float computeLod(Vec2 dUVdx, Vec2 dUVdy) const;

private:
    struct Level {
        int width = 0;
        int height = 0;
        std::vector<Vec4> texels;
        const Vec4& at(int x, int y) const { return texels[static_cast<size_t>(y) * width + x]; }
    };

    void buildMips();

    std::vector<Level> m_levels;
};

} // namespace sr
