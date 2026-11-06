#include "core/renderer.h"

#include <chrono>
#include <cstring>

namespace sr {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr int kSubpixelScale = 1 << Renderer::kSubpixelBits;
constexpr int kVertexChunk = 2048;
constexpr int kTriangleChunk = 1024;

// Clip polygons against x/y planes this many times wider than the viewport.
// Geometry inside the guard band is handled by the bounding-box scissor, which
// avoids generating extra triangles for the common "slightly off-screen" case
// while keeping fixed-point coordinates far away from overflow.
constexpr float kGuardBand = 4.0f;
constexpr float kMinW = 1e-5f;

// Signed distance of a clip-space point to each frustum plane (>= 0 is inside).
enum Plane { kPlaneW, kPlaneNear, kPlaneFar, kPlaneLeft, kPlaneRight, kPlaneBottom, kPlaneTop, kPlaneCount };

inline float planeDistance(int plane, const Vec4& p)
{
    switch (plane) {
    case kPlaneW: return p.w - kMinW;
    case kPlaneNear: return p.z + p.w;
    case kPlaneFar: return p.w - p.z;
    case kPlaneLeft: return kGuardBand * p.w + p.x;
    case kPlaneRight: return kGuardBand * p.w - p.x;
    case kPlaneBottom: return kGuardBand * p.w + p.y;
    default: return kGuardBand * p.w - p.y;
    }
}

inline int outcode(const Vec4& p)
{
    int code = 0;
    for (int i = 0; i < kPlaneCount; ++i)
        if (planeDistance(i, p) < 0.0f) code |= 1 << i;
    return code;
}

// Edge function in 24.8 fixed point: twice the signed area of (a, b, p).
inline int64_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int64_t px, int64_t py)
{
    return int64_t(bx - ax) * (py - ay) - int64_t(by - ay) * (px - ax);
}

// Top-left fill rule for y-down screen space and positive-area winding:
// pixels exactly on a shared edge belong to exactly one of the two triangles.
inline bool isTopLeft(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int32_t dx = bx - ax, dy = by - ay;
    return dy < 0 || (dy == 0 && dx > 0);
}

} // namespace

// ---------------------------------------------------------------------------

struct Renderer::ClipVertex {
    Vec4 clip;
    float varyings[kMaxVaryings];
    // True if the polygon edge starting at this vertex lies on an original
    // mesh edge (false for edges created by clipping). Used by the wireframe.
    bool edgeFlag = true;
};

struct Renderer::Triangle {
    int32_t x[3], y[3];          // 24.8 fixed-point window coordinates
    float z[3];                  // window depth in [0, 1]
    float invW[3];               // 1 / clip.w, for perspective-correct interpolation
    int minX, minY, maxX, maxY;  // pixel bounding box, clamped to the target
    int64_t area;                // twice the signed area, > 0 after setup
    uint32_t varyingOffset;      // into Renderer::m_varyings
    uint32_t drawIndex;
    uint8_t edgeMask;            // bit i: edge opposite vertex i is an original mesh edge
    bool frontFacing;
};

struct Renderer::GeometryChunk {
    std::vector<Triangle> triangles;
    std::vector<float> varyings;
    uint64_t culled = 0;
    uint64_t outside = 0;
};

Renderer::Renderer(int threadCount)
{
    setThreadCount(threadCount);
}

Renderer::~Renderer() = default;

void Renderer::setThreadCount(int threadCount)
{
    m_pool = std::make_unique<ThreadPool>(threadCount);
    // One cache line per counter so workers don't false-share.
    m_fragmentCounters.assign(static_cast<size_t>(m_pool->threadCount()) * 8, 0);
}

int Renderer::threadCount() const
{
    return m_pool->threadCount();
}

void Renderer::setWireframeOverlay(bool enabled, Vec3 color)
{
    m_wireframe = enabled;
    m_wireColor = color;
}

void Renderer::begin(Framebuffer& target)
{
    m_target = &target;
    m_draws.clear();
    m_triangles.clear();
    m_varyings.clear();
    m_stats = RenderStats{};

    m_tilesX = (target.width() + kTileSize - 1) / kTileSize;
    m_tilesY = (target.height() + kTileSize - 1) / kTileSize;
    m_bins.resize(static_cast<size_t>(m_tilesX) * m_tilesY);
    for (auto& bin : m_bins) bin.clear();
}

// ---------------------------------------------------------------------------
// Geometry stage

void Renderer::draw(const Mesh& mesh, const Shader& shader, const DrawState& state)
{
    if (!m_target || mesh.indices.empty()) return;
    Clock::time_point start = Clock::now();

    const int varyingCount = std::min(shader.varyingCount, kMaxVaryings);
    const uint32_t drawIndex = static_cast<uint32_t>(m_draws.size());
    m_draws.push_back({&shader, state});

    // 1. Vertex shading: each unique vertex once.
    const int vertexCount = static_cast<int>(mesh.vertices.size());
    m_clipVerts.resize(mesh.vertices.size());
    m_pool->parallelFor((vertexCount + kVertexChunk - 1) / kVertexChunk, [&](int chunk, int) {
        int begin = chunk * kVertexChunk;
        int end = std::min(begin + kVertexChunk, vertexCount);
        for (int i = begin; i < end; ++i)
            m_clipVerts[i].clip = shader.vertex(mesh.vertices[i], m_clipVerts[i].varyings);
    });

    // 2. Primitive assembly, clipping, culling and setup, in ordered chunks.
    const int triCount = static_cast<int>(mesh.triangleCount());
    const int chunkCount = (triCount + kTriangleChunk - 1) / kTriangleChunk;
    if (static_cast<int>(m_chunks.size()) < chunkCount) m_chunks.resize(chunkCount);

    m_pool->parallelFor(chunkCount, [&](int chunk, int) {
        GeometryChunk& out = m_chunks[chunk];
        out.triangles.clear();
        out.varyings.clear();
        out.culled = out.outside = 0;

        int begin = chunk * kTriangleChunk;
        int end = std::min(begin + kTriangleChunk, triCount);
        for (int t = begin; t < end; ++t) {
            const ClipVertex& a = m_clipVerts[mesh.indices[t * 3 + 0]];
            const ClipVertex& b = m_clipVerts[mesh.indices[t * 3 + 1]];
            const ClipVertex& c = m_clipVerts[mesh.indices[t * 3 + 2]];

            int ca = outcode(a.clip), cb = outcode(b.clip), cc = outcode(c.clip);
            if (ca & cb & cc) {
                ++out.outside; // entirely outside one plane
                continue;
            }
            if ((ca | cb | cc) == 0) {
                const ClipVertex* v[3] = {&a, &b, &c};
                emitTriangle(v, 0x7, drawIndex, varyingCount, out);
            } else {
                clipAndEmit(a, b, c, drawIndex, varyingCount, out);
            }
        }
    });

    // 3. Concatenate chunk output in submission order.
    for (int i = 0; i < chunkCount; ++i) {
        GeometryChunk& chunk = m_chunks[i];
        uint32_t base = static_cast<uint32_t>(m_varyings.size());
        for (Triangle& tri : chunk.triangles) {
            tri.varyingOffset += base;
            m_triangles.push_back(tri);
        }
        m_varyings.insert(m_varyings.end(), chunk.varyings.begin(), chunk.varyings.end());
        m_stats.trianglesCulled += chunk.culled;
        m_stats.trianglesOutside += chunk.outside;
    }

    m_stats.trianglesSubmitted += static_cast<uint64_t>(triCount);
    m_stats.geometryMs += msSince(start);
}

void Renderer::clipAndEmit(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, uint32_t drawIndex,
                           int varyingCount, GeometryChunk& out) const
{
    // Sutherland-Hodgman in homogeneous clip space. Interpolating varyings
    // linearly here is correct because clip space is pre-perspective-divide.
    constexpr int kMaxPoly = 3 + kPlaneCount;
    ClipVertex bufA[kMaxPoly], bufB[kMaxPoly];
    ClipVertex* in = bufA;
    ClipVertex* outPoly = bufB;
    int count = 3;
    in[0] = a;
    in[1] = b;
    in[2] = c;

    for (int plane = 0; plane < kPlaneCount && count >= 3; ++plane) {
        int outCount = 0;
        for (int i = 0; i < count; ++i) {
            const ClipVertex& cur = in[i];
            const ClipVertex& next = in[(i + 1) % count];
            float dCur = planeDistance(plane, cur.clip);
            float dNext = planeDistance(plane, next.clip);

            if (dCur >= 0.0f) outPoly[outCount++] = cur;
            if ((dCur >= 0.0f) != (dNext >= 0.0f)) {
                float t = dCur / (dCur - dNext);
                ClipVertex& v = outPoly[outCount++];
                // Entering: the new edge continues the original edge. Leaving: it runs along the plane.
                v.edgeFlag = dCur < 0.0f ? cur.edgeFlag : false;
                v.clip = cur.clip + (next.clip - cur.clip) * t;
                for (int k = 0; k < varyingCount; ++k)
                    v.varyings[k] = cur.varyings[k] + (next.varyings[k] - cur.varyings[k]) * t;
            }
        }
        std::swap(in, outPoly);
        count = outCount;
    }

    if (count < 3) {
        ++out.outside;
        return;
    }
    for (int i = 1; i + 1 < count; ++i) {
        const ClipVertex* v[3] = {&in[0], &in[i], &in[i + 1]};
        // Bit k = edge opposite vertex k. Interior fan diagonals are not mesh edges.
        uint8_t mask = static_cast<uint8_t>((in[i].edgeFlag ? 1 : 0) |
                                            ((i + 2 == count && in[count - 1].edgeFlag) ? 2 : 0) |
                                            ((i == 1 && in[0].edgeFlag) ? 4 : 0));
        emitTriangle(v, mask, drawIndex, varyingCount, out);
    }
}

void Renderer::emitTriangle(const ClipVertex* v[3], uint8_t edgeMask, uint32_t drawIndex, int varyingCount, GeometryChunk& out) const
{
    const int width = m_target->width();
    const int height = m_target->height();

    Triangle tri;
    for (int i = 0; i < 3; ++i) {
        const Vec4& p = v[i]->clip;
        float invW = 1.0f / p.w;
        float sx = (p.x * invW * 0.5f + 0.5f) * static_cast<float>(width);
        float sy = (0.5f - p.y * invW * 0.5f) * static_cast<float>(height);
        tri.x[i] = static_cast<int32_t>(std::lround(sx * kSubpixelScale));
        tri.y[i] = static_cast<int32_t>(std::lround(sy * kSubpixelScale));
        tri.z[i] = clamp01(p.z * invW * 0.5f + 0.5f);
        tri.invW[i] = invW;
    }

    int64_t area = edge(tri.x[0], tri.y[0], tri.x[1], tri.y[1], tri.x[2], tri.y[2]);
    if (area == 0) {
        ++out.culled;
        return;
    }

    // Counter-clockwise in NDC (y up) becomes negative area in y-down window space.
    tri.frontFacing = area < 0;
    const DrawState& state = m_draws[drawIndex].state;
    if ((state.cull == CullMode::Back && !tri.frontFacing) || (state.cull == CullMode::Front && tri.frontFacing)) {
        ++out.culled;
        return;
    }

    // Normalize winding so the rasterizer only handles positive area.
    int order[3] = {0, 1, 2};
    if (area < 0) {
        std::swap(tri.x[1], tri.x[2]);
        std::swap(tri.y[1], tri.y[2]);
        std::swap(tri.z[1], tri.z[2]);
        std::swap(tri.invW[1], tri.invW[2]);
        std::swap(order[1], order[2]);
        area = -area;
    }
    tri.area = area;

    int32_t minFx = std::min({tri.x[0], tri.x[1], tri.x[2]});
    int32_t maxFx = std::max({tri.x[0], tri.x[1], tri.x[2]});
    int32_t minFy = std::min({tri.y[0], tri.y[1], tri.y[2]});
    int32_t maxFy = std::max({tri.y[0], tri.y[1], tri.y[2]});
    tri.minX = std::max(0, minFx >> kSubpixelBits);
    tri.minY = std::max(0, minFy >> kSubpixelBits);
    tri.maxX = std::min(width - 1, maxFx >> kSubpixelBits);
    tri.maxY = std::min(height - 1, maxFy >> kSubpixelBits);
    if (tri.minX > tri.maxX || tri.minY > tri.maxY) {
        ++out.outside;
        return;
    }

    tri.drawIndex = drawIndex;
    tri.edgeMask = 0;
    for (int i = 0; i < 3; ++i)
        if (edgeMask & (1 << order[i])) tri.edgeMask |= static_cast<uint8_t>(1 << i);
    tri.varyingOffset = static_cast<uint32_t>(out.varyings.size());
    for (int i = 0; i < 3; ++i) {
        const float* src = v[order[i]]->varyings;
        out.varyings.insert(out.varyings.end(), src, src + varyingCount);
    }
    out.triangles.push_back(tri);
}

// ---------------------------------------------------------------------------
// Raster stage

void Renderer::end()
{
    if (!m_target) return;
    Clock::time_point start = Clock::now();

    // Bin triangles into every tile their bounding box touches. Bins are
    // filled in submission order, which preserves draw order inside a tile.
    for (uint32_t i = 0; i < m_triangles.size(); ++i) {
        const Triangle& t = m_triangles[i];
        int tx0 = t.minX / kTileSize, tx1 = t.maxX / kTileSize;
        int ty0 = t.minY / kTileSize, ty1 = t.maxY / kTileSize;
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx)
                m_bins[static_cast<size_t>(ty) * m_tilesX + tx].push_back(i);
    }

    std::fill(m_fragmentCounters.begin(), m_fragmentCounters.end(), 0);
    m_tileTimes.assign(m_bins.size(), 0.0f);

    m_pool->parallelFor(static_cast<int>(m_bins.size()), [this](int tile, int worker) {
        rasterizeTile(tile, worker);
    });

    m_stats.trianglesRasterized = m_triangles.size();
    for (size_t i = 0; i < m_fragmentCounters.size(); i += 8) m_stats.fragmentsShaded += m_fragmentCounters[i];
    m_stats.rasterMs = msSince(start);
    m_target = nullptr;
}

void Renderer::rasterizeTile(int tileIndex, int worker)
{
    const std::vector<uint32_t>& bin = m_bins[tileIndex];
    if (bin.empty()) return;
    Clock::time_point start;
    if (m_tileTiming) start = Clock::now();

    const int width = m_target->width();
    const int tileX0 = (tileIndex % m_tilesX) * kTileSize;
    const int tileY0 = (tileIndex / m_tilesX) * kTileSize;
    const int tileX1 = std::min(tileX0 + kTileSize, m_target->width()) - 1;
    const int tileY1 = std::min(tileY0 + kTileSize, m_target->height()) - 1;

    Vec3* color = m_target->colorData();
    float* depth = m_target->depthData();
    const bool hasColor = m_target->hasColor();
    uint64_t fragments = 0;

    float varyings[kMaxVaryings];
    FragmentInput frag;
    frag.varyings = varyings;

    for (uint32_t triIndex : bin) {
        const Triangle& t = m_triangles[triIndex];
        const DrawRecord& draw = m_draws[t.drawIndex];
        const Shader& shader = *draw.shader;
        const int varyingCount = std::min(shader.varyingCount, kMaxVaryings);
        const float* vary[3] = {
            &m_varyings[t.varyingOffset],
            &m_varyings[t.varyingOffset + varyingCount],
            &m_varyings[t.varyingOffset + 2 * varyingCount],
        };
        const bool shade = hasColor && shader.hasFragmentStage;
        const int uvSlot = shader.uvSlot;

        const int x0 = std::max(t.minX, tileX0), x1 = std::min(t.maxX, tileX1);
        const int y0 = std::max(t.minY, tileY0), y1 = std::min(t.maxY, tileY1);
        if (x0 > x1 || y0 > y1) continue;

        // Edge i is opposite vertex i, so its value is vertex i's barycentric weight.
        const int32_t* X = t.x;
        const int32_t* Y = t.y;
        const int ea[3] = {1, 2, 0}, eb[3] = {2, 0, 1};
        int64_t stepX[3], stepY[3], rowStart[3];
        int64_t bias[3];
        float invEdgeLen[3];
        const int64_t px = int64_t(x0) * kSubpixelScale + kSubpixelScale / 2;
        const int64_t py = int64_t(y0) * kSubpixelScale + kSubpixelScale / 2;
        for (int e = 0; e < 3; ++e) {
            int a = ea[e], b = eb[e];
            stepX[e] = -int64_t(Y[b] - Y[a]) * kSubpixelScale;
            stepY[e] = int64_t(X[b] - X[a]) * kSubpixelScale;
            rowStart[e] = edge(X[a], Y[a], X[b], Y[b], px, py);
            bias[e] = isTopLeft(X[a], Y[a], X[b], Y[b]) ? 0 : -1;
            float dx = float(X[b] - X[a]), dy = float(Y[b] - Y[a]);
            invEdgeLen[e] = 1.0f / (std::sqrt(dx * dx + dy * dy) * kSubpixelScale);
        }

        const float invArea = 1.0f / static_cast<float>(t.area);
        frag.frontFacing = t.frontFacing;

        // Perspective-correct barycentrics from (unnormalized) screen weights.
        auto perspective = [&](float l0, float l1, float l2, float out[3]) {
            float w0 = l0 * t.invW[0], w1 = l1 * t.invW[1], w2 = l2 * t.invW[2];
            float inv = 1.0f / (w0 + w1 + w2);
            out[0] = w0 * inv;
            out[1] = w1 * inv;
            out[2] = w2 * inv;
        };
        auto interpolateUv = [&](const float b[3]) {
            return Vec2(b[0] * vary[0][uvSlot] + b[1] * vary[1][uvSlot] + b[2] * vary[2][uvSlot],
                        b[0] * vary[0][uvSlot + 1] + b[1] * vary[1][uvSlot + 1] + b[2] * vary[2][uvSlot + 1]);
        };

        for (int y = y0; y <= y1; ++y) {
            int64_t e0 = rowStart[0], e1 = rowStart[1], e2 = rowStart[2];
            size_t index = static_cast<size_t>(y) * width + x0;

            for (int x = x0; x <= x1; ++x, ++index, e0 += stepX[0], e1 += stepX[1], e2 += stepX[2]) {
                if ((e0 + bias[0]) < 0 || (e1 + bias[1]) < 0 || (e2 + bias[2]) < 0) continue;

                float l0 = static_cast<float>(e0) * invArea;
                float l1 = static_cast<float>(e1) * invArea;
                float l2 = static_cast<float>(e2) * invArea;
                float z = l0 * t.z[0] + l1 * t.z[1] + l2 * t.z[2];

                if (draw.state.depthTest && !(z < depth[index])) continue;

                if (!shade) {
                    if (draw.state.depthWrite) depth[index] = z;
                    continue;
                }

                float b[3];
                perspective(l0, l1, l2, b);
                for (int k = 0; k < varyingCount; ++k)
                    varyings[k] = b[0] * vary[0][k] + b[1] * vary[1][k] + b[2] * vary[2][k];

                if (uvSlot >= 0) {
                    // Evaluate the uv one pixel right and one pixel down (like a 2x2 quad).
                    float bx[3], by[3];
                    perspective(float(e0 + stepX[0]) * invArea, float(e1 + stepX[1]) * invArea,
                                float(e2 + stepX[2]) * invArea, bx);
                    perspective(float(e0 + stepY[0]) * invArea, float(e1 + stepY[1]) * invArea,
                                float(e2 + stepY[2]) * invArea, by);
                    Vec2 uv(varyings[uvSlot], varyings[uvSlot + 1]);
                    frag.dUVdx = interpolateUv(bx) - uv;
                    frag.dUVdy = interpolateUv(by) - uv;
                }

                frag.barycentric = {b[0], b[1], b[2]};
                frag.x = x;
                frag.y = y;
                frag.depth = z;

                Vec3 out;
                ++fragments;
                if (!shader.fragment(frag, out)) continue;

                if (m_wireframe && t.edgeMask) {
                    // Pixel distance to the nearest original mesh edge; edges created by
                    // clipping are skipped so the overlay shows the real topology.
                    constexpr float kFar = 1e30f;
                    float d = std::min({(t.edgeMask & 1) ? float(e0) * invEdgeLen[0] : kFar,
                                        (t.edgeMask & 2) ? float(e1) * invEdgeLen[1] : kFar,
                                        (t.edgeMask & 4) ? float(e2) * invEdgeLen[2] : kFar});
                    // Fade with view distance (1/w interpolates linearly in screen space)
                    // so dense far-away geometry doesn't turn into solid ink.
                    float viewDist = 1.0f / (l0 * t.invW[0] + l1 * t.invW[1] + l2 * t.invW[2]);
                    float fade = 1.0f - smoothstep(12.0f, 45.0f, viewDist);
                    out = lerp(out, m_wireColor, (1.0f - smoothstep(0.4f, 1.2f, d)) * fade);
                }

                color[index] = out;
                if (draw.state.depthWrite) depth[index] = z;
            }

            rowStart[0] += stepY[0];
            rowStart[1] += stepY[1];
            rowStart[2] += stepY[2];
        }
    }

    m_fragmentCounters[static_cast<size_t>(worker) * 8] += fragments;
    if (m_tileTiming) m_tileTimes[tileIndex] = static_cast<float>(msSince(start));
}

} // namespace sr
