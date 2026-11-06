#pragma once
// Procedural textures so every demo scene works without downloading assets.

#include "core/texture.h"

namespace sr::procedural {

Texture checker(int size, int tiles, Vec3 a, Vec3 b);
// Colored UV test grid: shows texture orientation, perspective and filtering.
Texture uvGrid(int size, int cells);
// Running-bond brick pattern; returns albedo and a tangent-space normal map.
void bricks(int size, Texture& albedo, Texture& normal);

} // namespace sr::procedural
