#include "render/procedural.h"

#include "core/framebuffer.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace sr::procedural {

namespace {

// Deterministic hash noise in [0, 1).
float hash(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}

float valueNoise(float x, float y)
{
    int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    float tx = smoothstep(0.0f, 1.0f, x - xi), ty = smoothstep(0.0f, 1.0f, y - yi);
    float a = lerp(hash(xi, yi), hash(xi + 1, yi), tx);
    float b = lerp(hash(xi, yi + 1), hash(xi + 1, yi + 1), tx);
    return lerp(a, b, ty);
}

Vec3 fromSrgb(Vec3 c) { return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)}; }

} // namespace

Texture checker(int size, int tiles, Vec3 a, Vec3 b)
{
    std::vector<Vec4> texels(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            bool odd = ((x * tiles / size) + (y * tiles / size)) & 1;
            texels[static_cast<size_t>(y) * size + x] = Vec4(odd ? a : b, 1.0f);
        }
    return Texture::fromLinear(size, size, std::move(texels));
}

Texture uvGrid(int size, int cells)
{
    std::vector<Vec4> texels(static_cast<size_t>(size) * size);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int cx = x / cell, cy = y / cell;
            float u = (cx + 0.5f) / cells, v = (cy + 0.5f) / cells;
            Vec3 base = ((cx + cy) & 1) ? Vec3(0.92f) : Vec3(0.18f);
            Vec3 tint(0.35f + 0.65f * u, 0.35f + 0.5f * (1.0f - v), 0.45f + 0.55f * v);
            Vec3 c = base * tint;
            int lx = x % cell, ly = y % cell;
            if (lx < 2 || ly < 2) c = Vec3(0.02f);                             // grid lines
            if (std::abs(lx - cell / 2) <= 1 || std::abs(ly - cell / 2) <= 1) // cross in each cell
                if (std::abs(lx - cell / 2) < cell / 6 && std::abs(ly - cell / 2) < cell / 6) c = Vec3(1.0f, 0.3f, 0.1f);
            texels[static_cast<size_t>(y) * size + x] = Vec4(fromSrgb(c), 1.0f);
        }
    }
    return Texture::fromLinear(size, size, std::move(texels));
}

void bricks(int size, Texture& albedoOut, Texture& normalOut)
{
    const int rows = 8, cols = 4;
    const float mortar = 0.06f;
    std::vector<float> height(static_cast<size_t>(size) * size);
    std::vector<Vec4> albedo(height.size());

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fy = static_cast<float>(y) / size * rows;
            int row = static_cast<int>(fy);
            float fx = static_cast<float>(x) / size * cols + (row & 1 ? 0.5f : 0.0f);
            int col = static_cast<int>(std::floor(fx));
            float lx = fx - col, ly = fy - row;

            // Distance to the nearest mortar line, in brick-space units.
            float edge = std::min({lx, 1.0f - lx, (ly) * 0.5f, (1.0f - ly) * 0.5f});
            float brick = smoothstep(mortar * 0.5f, mortar, edge);
            float grain = valueNoise(x * 0.08f, y * 0.08f) * 0.6f + valueNoise(x * 0.3f, y * 0.3f) * 0.4f;
            float h = brick * (0.85f + 0.15f * grain);
            height[static_cast<size_t>(y) * size + x] = h;

            float tone = hash(col + 17 * row, row) * 0.25f;
            Vec3 brickColor = fromSrgb(Vec3(0.62f + tone, 0.27f + tone * 0.5f, 0.2f)) * (0.8f + 0.3f * grain);
            Vec3 mortarColor = fromSrgb(Vec3(0.62f, 0.6f, 0.56f)) * (0.85f + 0.2f * grain);
            albedo[static_cast<size_t>(y) * size + x] = Vec4(lerp(mortarColor, brickColor, brick), 1.0f);
        }
    }

    // Height -> tangent-space normal via central differences (+Y up in texture = -v).
    std::vector<Vec4> normals(height.size());
    const float strength = 3.0f;
    auto h = [&](int x, int y) {
        x = (x % size + size) % size;
        y = (y % size + size) % size;
        return height[static_cast<size_t>(y) * size + x];
    };
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = (h(x + 1, y) - h(x - 1, y)) * strength;
            float dy = (h(x, y - 1) - h(x, y + 1)) * strength;
            Vec3 n = normalize(Vec3(-dx, -dy, 1.0f));
            normals[static_cast<size_t>(y) * size + x] = Vec4(n * 0.5f + Vec3(0.5f), 1.0f);
        }

    albedoOut = Texture::fromLinear(size, size, std::move(albedo));
    normalOut = Texture::fromLinear(size, size, std::move(normals));
}

} // namespace sr::procedural
