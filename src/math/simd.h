#pragma once
// 4-wide float SIMD with one API over SSE2, NEON (AArch64), WebAssembly
// SIMD128 and a portable scalar fallback. The rasterizer shades 2x2 pixel
// quads with these types, one pixel per lane.
//
// Define SR_SIMD_SCALAR to force the fallback (useful for testing).

#include "math/math.h"

#include <cmath>
#include <cstdint>

#if defined(SR_SIMD_SCALAR)
#define SR_SIMD_BACKEND "scalar"
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define SR_SIMD_WASM 1
#define SR_SIMD_BACKEND "wasm-simd128"
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define SR_SIMD_SSE 1
#define SR_SIMD_BACKEND "sse2"
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define SR_SIMD_NEON 1
#define SR_SIMD_BACKEND "neon"
#else
#define SR_SIMD_BACKEND "scalar"
#endif

namespace sr {

// ---------------------------------------------------------------------------
// Lane mask (result of comparisons).
struct M4 {
#if SR_SIMD_SSE
    __m128 v;
#elif SR_SIMD_NEON
    uint32x4_t v;
#elif SR_SIMD_WASM
    v128_t v;
#else
    bool v[4];
#endif

    // Bit i set = lane i active.
    int bits() const
    {
#if SR_SIMD_SSE
        return _mm_movemask_ps(v);
#elif SR_SIMD_NEON
        return int(vgetq_lane_u32(v, 0) >> 31) | int(vgetq_lane_u32(v, 1) >> 31) << 1 |
               int(vgetq_lane_u32(v, 2) >> 31) << 2 | int(vgetq_lane_u32(v, 3) >> 31) << 3;
#elif SR_SIMD_WASM
        return static_cast<int>(wasm_i32x4_bitmask(v));
#else
        return int(v[0]) | int(v[1]) << 1 | int(v[2]) << 2 | int(v[3]) << 3;
#endif
    }

    static M4 fromBits(int b)
    {
        M4 m;
#if SR_SIMD_SSE
        m.v = _mm_castsi128_ps(_mm_set_epi32(b & 8 ? -1 : 0, b & 4 ? -1 : 0, b & 2 ? -1 : 0, b & 1 ? -1 : 0));
#elif SR_SIMD_NEON
        const uint32_t lanes[4] = {b & 1 ? ~0u : 0u, b & 2 ? ~0u : 0u, b & 4 ? ~0u : 0u, b & 8 ? ~0u : 0u};
        m.v = vld1q_u32(lanes);
#elif SR_SIMD_WASM
        m.v = wasm_i32x4_make(b & 1 ? -1 : 0, b & 2 ? -1 : 0, b & 4 ? -1 : 0, b & 8 ? -1 : 0);
#else
        for (int i = 0; i < 4; ++i) m.v[i] = (b >> i) & 1;
#endif
        return m;
    }

    bool any() const { return bits() != 0; }

    M4 operator&(const M4& o) const
    {
        M4 r;
#if SR_SIMD_SSE
        r.v = _mm_and_ps(v, o.v);
#elif SR_SIMD_NEON
        r.v = vandq_u32(v, o.v);
#elif SR_SIMD_WASM
        r.v = wasm_v128_and(v, o.v);
#else
        for (int i = 0; i < 4; ++i) r.v[i] = v[i] && o.v[i];
#endif
        return r;
    }
};

// ---------------------------------------------------------------------------
struct F4 {
#if SR_SIMD_SSE
    __m128 v;
#elif SR_SIMD_NEON
    float32x4_t v;
#elif SR_SIMD_WASM
    v128_t v;
#else
    float v[4];
#endif

    F4() = default;

    // Broadcast.
    F4(float s)
    {
#if SR_SIMD_SSE
        v = _mm_set1_ps(s);
#elif SR_SIMD_NEON
        v = vdupq_n_f32(s);
#elif SR_SIMD_WASM
        v = wasm_f32x4_splat(s);
#else
        v[0] = v[1] = v[2] = v[3] = s;
#endif
    }

    F4(float a, float b, float c, float d)
    {
#if SR_SIMD_SSE
        v = _mm_setr_ps(a, b, c, d);
#elif SR_SIMD_NEON
        const float t[4] = {a, b, c, d};
        v = vld1q_f32(t);
#elif SR_SIMD_WASM
        v = wasm_f32x4_make(a, b, c, d);
#else
        v[0] = a; v[1] = b; v[2] = c; v[3] = d;
#endif
    }

    static F4 load(const float* p)
    {
        F4 r;
#if SR_SIMD_SSE
        r.v = _mm_loadu_ps(p);
#elif SR_SIMD_NEON
        r.v = vld1q_f32(p);
#elif SR_SIMD_WASM
        r.v = wasm_v128_load(p);
#else
        for (int i = 0; i < 4; ++i) r.v[i] = p[i];
#endif
        return r;
    }

    void store(float* p) const
    {
#if SR_SIMD_SSE
        _mm_storeu_ps(p, v);
#elif SR_SIMD_NEON
        vst1q_f32(p, v);
#elif SR_SIMD_WASM
        wasm_v128_store(p, v);
#else
        for (int i = 0; i < 4; ++i) p[i] = v[i];
#endif
    }

    float lane(int i) const
    {
        float t[4];
        store(t);
        return t[i];
    }
};

#if SR_SIMD_SSE
#define SR_F4_BINOP(op, intrin) \
    inline F4 operator op(const F4& a, const F4& b) { F4 r; r.v = intrin(a.v, b.v); return r; }
#define SR_F4_CMP(op, intrin) \
    inline M4 operator op(const F4& a, const F4& b) { M4 r; r.v = intrin(a.v, b.v); return r; }
SR_F4_BINOP(+, _mm_add_ps)
SR_F4_BINOP(-, _mm_sub_ps)
SR_F4_BINOP(*, _mm_mul_ps)
SR_F4_BINOP(/, _mm_div_ps)
SR_F4_CMP(<, _mm_cmplt_ps)
SR_F4_CMP(<=, _mm_cmple_ps)
SR_F4_CMP(>, _mm_cmpgt_ps)
SR_F4_CMP(>=, _mm_cmpge_ps)
inline F4 operator-(const F4& a) { F4 r; r.v = _mm_xor_ps(a.v, _mm_set1_ps(-0.0f)); return r; }
inline F4 min(const F4& a, const F4& b) { F4 r; r.v = _mm_min_ps(a.v, b.v); return r; }
inline F4 max(const F4& a, const F4& b) { F4 r; r.v = _mm_max_ps(a.v, b.v); return r; }
inline F4 sqrt(const F4& a) { F4 r; r.v = _mm_sqrt_ps(a.v); return r; }
// Lanes where m is set take a, the others take b.
inline F4 select(const M4& m, const F4& a, const F4& b)
{
    F4 r;
    r.v = _mm_or_ps(_mm_and_ps(m.v, a.v), _mm_andnot_ps(m.v, b.v));
    return r;
}
#elif SR_SIMD_NEON
#define SR_F4_BINOP(op, intrin) \
    inline F4 operator op(const F4& a, const F4& b) { F4 r; r.v = intrin(a.v, b.v); return r; }
#define SR_F4_CMP(op, intrin) \
    inline M4 operator op(const F4& a, const F4& b) { M4 r; r.v = intrin(a.v, b.v); return r; }
SR_F4_BINOP(+, vaddq_f32)
SR_F4_BINOP(-, vsubq_f32)
SR_F4_BINOP(*, vmulq_f32)
SR_F4_BINOP(/, vdivq_f32)
SR_F4_CMP(<, vcltq_f32)
SR_F4_CMP(<=, vcleq_f32)
SR_F4_CMP(>, vcgtq_f32)
SR_F4_CMP(>=, vcgeq_f32)
inline F4 operator-(const F4& a) { F4 r; r.v = vnegq_f32(a.v); return r; }
inline F4 min(const F4& a, const F4& b) { F4 r; r.v = vminq_f32(a.v, b.v); return r; }
inline F4 max(const F4& a, const F4& b) { F4 r; r.v = vmaxq_f32(a.v, b.v); return r; }
inline F4 sqrt(const F4& a) { F4 r; r.v = vsqrtq_f32(a.v); return r; }
inline F4 select(const M4& m, const F4& a, const F4& b) { F4 r; r.v = vbslq_f32(m.v, a.v, b.v); return r; }
#elif SR_SIMD_WASM
#define SR_F4_BINOP(op, intrin) \
    inline F4 operator op(const F4& a, const F4& b) { F4 r; r.v = intrin(a.v, b.v); return r; }
#define SR_F4_CMP(op, intrin) \
    inline M4 operator op(const F4& a, const F4& b) { M4 r; r.v = intrin(a.v, b.v); return r; }
SR_F4_BINOP(+, wasm_f32x4_add)
SR_F4_BINOP(-, wasm_f32x4_sub)
SR_F4_BINOP(*, wasm_f32x4_mul)
SR_F4_BINOP(/, wasm_f32x4_div)
SR_F4_CMP(<, wasm_f32x4_lt)
SR_F4_CMP(<=, wasm_f32x4_le)
SR_F4_CMP(>, wasm_f32x4_gt)
SR_F4_CMP(>=, wasm_f32x4_ge)
inline F4 operator-(const F4& a) { F4 r; r.v = wasm_f32x4_neg(a.v); return r; }
// pmin/pmax match the SSE semantics (and compile to a single instruction there).
inline F4 min(const F4& a, const F4& b) { F4 r; r.v = wasm_f32x4_pmin(a.v, b.v); return r; }
inline F4 max(const F4& a, const F4& b) { F4 r; r.v = wasm_f32x4_pmax(a.v, b.v); return r; }
inline F4 sqrt(const F4& a) { F4 r; r.v = wasm_f32x4_sqrt(a.v); return r; }
inline F4 select(const M4& m, const F4& a, const F4& b) { F4 r; r.v = wasm_v128_bitselect(a.v, b.v, m.v); return r; }
#else
#define SR_F4_BINOP(op)                                                     \
    inline F4 operator op(const F4& a, const F4& b)                         \
    {                                                                       \
        F4 r;                                                               \
        for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] op b.v[i];              \
        return r;                                                           \
    }
#define SR_F4_CMP(op)                                                       \
    inline M4 operator op(const F4& a, const F4& b)                         \
    {                                                                       \
        M4 r;                                                               \
        for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] op b.v[i];              \
        return r;                                                           \
    }
SR_F4_BINOP(+)
SR_F4_BINOP(-)
SR_F4_BINOP(*)
SR_F4_BINOP(/)
SR_F4_CMP(<)
SR_F4_CMP(<=)
SR_F4_CMP(>)
SR_F4_CMP(>=)
inline F4 operator-(const F4& a) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = -a.v[i]; return r; }
inline F4 min(const F4& a, const F4& b) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i]; return r; }
inline F4 max(const F4& a, const F4& b) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i]; return r; }
inline F4 sqrt(const F4& a) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = std::sqrt(a.v[i]); return r; }
inline F4 select(const M4& m, const F4& a, const F4& b) { F4 r; for (int i = 0; i < 4; ++i) r.v[i] = m.v[i] ? a.v[i] : b.v[i]; return r; }
#endif
#undef SR_F4_BINOP
#undef SR_F4_CMP

inline F4 floor(const F4& a)
{
    F4 r;
#if SR_SIMD_SSE
    // SSE2 has no floor: truncate, then subtract 1 where truncation rounded up (negatives).
    __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v));
    r.v = _mm_sub_ps(t, _mm_and_ps(_mm_cmpgt_ps(t, a.v), _mm_set1_ps(1.0f)));
#elif SR_SIMD_NEON
    r.v = vrndmq_f32(a.v);
#elif SR_SIMD_WASM
    r.v = wasm_f32x4_floor(a.v);
#else
    for (int i = 0; i < 4; ++i) r.v[i] = std::floor(a.v[i]);
#endif
    return r;
}

// 2^x: the nearest integer goes straight into the float exponent bits, the
// remainder in [-0.5, 0.5] uses the Cephes exp2f minimax polynomial (~1e-7 relative error).
inline F4 exp2(const F4& x)
{
#if SR_SIMD_SSE || SR_SIMD_NEON || SR_SIMD_WASM
    const F4 xc = min(max(x, F4(-126.0f)), F4(126.0f));
    const F4 xi = floor(xc + F4(0.5f));
    const F4 f = xc - xi;
    const F4 p = F4(1.0f) +
                 f * (F4(6.931472028550421e-1f) +
                      f * (F4(2.402264791363012e-1f) +
                           f * (F4(5.550332471162809e-2f) +
                                f * (F4(9.618437357674640e-3f) +
                                     f * (F4(1.339887440266574e-3f) + f * F4(1.535336188319500e-4f))))));
    F4 scale;
#if SR_SIMD_SSE
    __m128i e = _mm_slli_epi32(_mm_add_epi32(_mm_cvttps_epi32(xi.v), _mm_set1_epi32(127)), 23);
    scale.v = _mm_castsi128_ps(e);
#elif SR_SIMD_NEON
    int32x4_t e = vshlq_n_s32(vaddq_s32(vcvtq_s32_f32(xi.v), vdupq_n_s32(127)), 23);
    scale.v = vreinterpretq_f32_s32(e);
#else
    scale.v = wasm_i32x4_shl(wasm_i32x4_add(wasm_i32x4_trunc_sat_f32x4(xi.v), wasm_i32x4_splat(127)), 23);
#endif
    return p * scale;
#else
    F4 r;
    for (int i = 0; i < 4; ++i) r.v[i] = std::exp2(x.v[i]);
    return r;
#endif
}

inline F4 exp(const F4& x) { return exp2(x * F4(1.44269504f)); }

inline F4& operator+=(F4& a, const F4& b) { return a = a + b; }
inline F4& operator-=(F4& a, const F4& b) { return a = a - b; }
inline F4& operator*=(F4& a, const F4& b) { return a = a * b; }

inline F4 clamp01(const F4& x) { return min(max(x, F4(0.0f)), F4(1.0f)); }
inline F4 lerp(const F4& a, const F4& b, const F4& t) { return a + (b - a) * t; }
inline F4 smoothstep(float e0, float e1, const F4& x)
{
    F4 t = clamp01((x - F4(e0)) * F4(1.0f / (e1 - e0)));
    return t * t * (F4(3.0f) - F4(2.0f) * t);
}

// Applies a scalar function lane by lane (for exp/log/pow and texture fetches).
template <typename Fn>
inline F4 map(const F4& x, Fn fn)
{
    float t[4];
    x.store(t);
    return F4(fn(t[0]), fn(t[1]), fn(t[2]), fn(t[3]));
}

// ---------------------------------------------------------------------------
// Three-component vectors of 4 lanes (structure of arrays).
struct V3 {
    F4 x, y, z;
    V3() = default;
    V3(const F4& x_, const F4& y_, const F4& z_) : x(x_), y(y_), z(z_) {}
    V3(const Vec3& s) : x(s.x), y(s.y), z(s.z) {} // broadcast

    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(const V3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    V3 operator*(const F4& s) const { return {x * s, y * s, z * s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    Vec3 lane(int i) const { return {x.lane(i), y.lane(i), z.lane(i)}; }
};

inline F4 dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 normalize(const V3& v)
{
    F4 len2 = dot(v, v);
    F4 inv = F4(1.0f) / sqrt(max(len2, F4(1e-30f)));
    return v * inv;
}
inline V3 lerp(const V3& a, const V3& b, const F4& t) { return a + (b - a) * t; }
inline V3 select(const M4& m, const V3& a, const V3& b)
{
    return {select(m, a.x, b.x), select(m, a.y, b.y), select(m, a.z, b.z)};
}
inline V3 reflect(const V3& i, const V3& n) { return i - n * (F4(2.0f) * dot(i, n)); }

} // namespace sr
