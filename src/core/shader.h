#pragma once
// Programmable stages. A Shader runs on many threads at once, so vertex()
// and the fragment functions must be const and must not write shared state.
//
// Fragments are rasterized in 2x2 quads (like GPUs). Lane order:
//   lane 0 = (x, y)     lane 1 = (x+1, y)
//   lane 2 = (x, y+1)   lane 3 = (x+1, y+1)
// Lanes outside the triangle still receive extrapolated attributes ("helper
// lanes") so screen-space derivatives are well defined, but they are never written.

#include "geometry/mesh.h"
#include "math/math.h"
#include "math/simd.h"

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

struct QuadInput {
    F4 varyings[kMaxVaryings]; // one lane per pixel
    F4 barycentric[3];
    F4 depth;
    int x = 0, y = 0; // top-left pixel of the quad
    bool frontFacing = true;

    // Coarse screen-space derivatives (one value per quad), like dFdx/dFdy.
    float ddx(const F4& v) const { return v.lane(1) - v.lane(0); }
    float ddy(const F4& v) const { return v.lane(2) - v.lane(0); }
};

class Shader {
public:
    virtual ~Shader() = default;

    // Number of floats vertex() writes into its varying output (<= kMaxVaryings).
    int varyingCount = 0;
    // If >= 0, varyings[uvSlot] and varyings[uvSlot + 1] are texture
    // coordinates; the scalar path receives their derivatives in FragmentInput.
    int uvSlot = -1;
    // Depth-only passes (e.g. shadow maps) can skip the fragment stage.
    bool hasFragmentStage = true;

    // Returns the clip-space position and writes varyingCount floats.
    virtual Vec4 vertex(const Vertex& in, float* varyingsOut) const = 0;

    // Scalar fragment shader. Returns false to discard.
    virtual bool fragment(const FragmentInput& in, Vec3& colorOut) const = 0;

    // Quad fragment shader. `alive` holds the lanes to shade on input and the
    // lanes to write on output (clear bits to discard). The default runs the
    // scalar fragment() once per live lane; shaders override it with SIMD code.
    virtual void fragmentQuad(const QuadInput& in, V3& colorOut, M4& alive) const;
};

} // namespace sr
