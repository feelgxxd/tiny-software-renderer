#pragma once
#include <vector>
#include "../math/vec3.h"

struct Triangle {
    Vec3 v0, v1, v2;
};

struct Mesh {
    std::vector<Triangle> triangles;
};