#include "core/shader.h"

namespace sr {

void Shader::fragmentQuad(const QuadInput& in, V3& colorOut, M4& alive) const
{
    float vary[kMaxVaryings][4];
    const int count = std::min(varyingCount, kMaxVaryings);
    for (int k = 0; k < count; ++k) in.varyings[k].store(vary[k]);
    float bary[3][4], depth[4];
    for (int k = 0; k < 3; ++k) in.barycentric[k].store(bary[k]);
    in.depth.store(depth);

    FragmentInput frag;
    frag.frontFacing = in.frontFacing;
    if (uvSlot >= 0) {
        frag.dUVdx = {in.ddx(in.varyings[uvSlot]), in.ddx(in.varyings[uvSlot + 1])};
        frag.dUVdy = {in.ddy(in.varyings[uvSlot]), in.ddy(in.varyings[uvSlot + 1])};
    }

    float r[4] = {}, g[4] = {}, b[4] = {};
    float laneVaryings[kMaxVaryings];
    frag.varyings = laneVaryings;
    int bits = alive.bits();
    for (int lane = 0; lane < 4; ++lane) {
        if (!(bits & (1 << lane))) continue;
        for (int k = 0; k < count; ++k) laneVaryings[k] = vary[k][lane];
        frag.barycentric = {bary[0][lane], bary[1][lane], bary[2][lane]};
        frag.x = in.x + (lane & 1);
        frag.y = in.y + (lane >> 1);
        frag.depth = depth[lane];
        Vec3 c;
        if (fragment(frag, c)) {
            r[lane] = c.x;
            g[lane] = c.y;
            b[lane] = c.z;
        } else {
            bits &= ~(1 << lane);
        }
    }
    colorOut = V3(F4::load(r), F4::load(g), F4::load(b));
    alive = M4::fromBits(bits);
}

} // namespace sr
