#pragma once
// Programmable stages. A Shader runs on many threads at once, so vertex()
// and fragment() must be const and must not write shared state.

#include "geometry/mesh.h"
#include "math/math.h"

namespace sr {

constexpr int kMaxVaryings = 16;

struct FragmentInput {
    const float* varyings = nullptr; // perspective-correct, varyingCount floats
    Vec3 barycentric;                // perspective-correct barycentrics
    Vec2 dUVdx, dUVdy;               // screen derivatives of the uv varying (see Shader::uvSlot)
    int x = 0, y = 0;                // pixel coordinates
    float depth = 0.0f;              // window-space depth in [0, 1]
    bool frontFacing = true;
};

class Shader {
public:
    virtual ~Shader() = default;

    // Number of floats vertex() writes into its varying output (<= kMaxVaryings).
    int varyingCount = 0;
    // If >= 0, varyings[uvSlot] and varyings[uvSlot + 1] are treated as texture
    // coordinates and the rasterizer computes their screen-space derivatives,
    // enabling mip selection exactly like dFdx/dFdy on a GPU.
    int uvSlot = -1;
    // Depth-only passes (e.g. shadow maps) can skip the fragment stage.
    bool hasFragmentStage = true;

    // Returns the clip-space position and writes varyingCount floats.
    virtual Vec4 vertex(const Vertex& in, float* varyingsOut) const = 0;
    // Returns false to discard the fragment.
    virtual bool fragment(const FragmentInput& in, Vec3& colorOut) const = 0;
};

} // namespace sr
