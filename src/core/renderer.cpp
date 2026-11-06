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
    const int wanted = threadCount <= 0 ? ThreadPool::hardwareThreads() : threadCount;
    if (m_pool && wanted <= m_pool->capacity()) {
        m_pool->setActiveThreads(wanted); // no thread creation or teardown
        return;
    }
    m_pool.reset(); // join the old workers before spawning new ones
    m_pool = std::make_unique<ThreadPool>(wanted);
    // One cache line per counter so workers don't false-share.
    m_fragmentCounters.assign(static_cast<size_t>(m_pool->capacity()) * 8, 0);
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

    QuadInput quad;

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
        const bool depthTest = draw.state.depthTest;
        const bool depthWrite = draw.state.depthWrite;

        // Pixel rectangle this tile owns for this triangle.
        const int x0 = std::max(t.minX, tileX0), x1 = std::min(t.maxX, tileX1);
        const int y0 = std::max(t.minY, tileY0), y1 = std::min(t.maxY, tileY1);
        if (x0 > x1 || y0 > y1) continue;

        // Walk 2x2 quads aligned to even pixel coordinates. Tiles are 64-pixel
        // aligned, so a quad never straddles two tiles (i.e. two threads).
        const int qx0 = x0 & ~1, qy0 = y0 & ~1;

        // Edge i is opposite vertex i, so its value is vertex i's barycentric weight.
        const int32_t* X = t.x;
        const int32_t* Y = t.y;
        const int ea[3] = {1, 2, 0}, eb[3] = {2, 0, 1};
        int64_t stepX[3], stepY[3], rowStart[3], bias[3];
        float invEdgeLen[3] = {};
        const bool wire = m_wireframe && t.edgeMask && shade;
        const int64_t px = int64_t(qx0) * kSubpixelScale + kSubpixelScale / 2;
        const int64_t py = int64_t(qy0) * kSubpixelScale + kSubpixelScale / 2;
        for (int e = 0; e < 3; ++e) {
            int a = ea[e], b = eb[e];
            stepX[e] = -int64_t(Y[b] - Y[a]) * kSubpixelScale;
            stepY[e] = int64_t(X[b] - X[a]) * kSubpixelScale;
            rowStart[e] = edge(X[a], Y[a], X[b], Y[b], px, py);
            bias[e] = isTopLeft(X[a], Y[a], X[b], Y[b]) ? 0 : -1;
            if (wire) {
                float dx = float(X[b] - X[a]), dy = float(Y[b] - Y[a]);
                invEdgeLen[e] = 1.0f / (std::sqrt(dx * dx + dy * dy) * kSubpixelScale);
            }
        }

        const F4 invArea(1.0f / static_cast<float>(t.area));
        const F4 iw0(t.invW[0]), iw1(t.invW[1]), iw2(t.invW[2]);
        const F4 z0(t.z[0]), z1(t.z[1]), z2(t.z[2]);
        quad.frontFacing = t.frontFacing;

        for (int qy = qy0; qy <= y1; qy += 2) {
            int64_t e0 = rowStart[0], e1 = rowStart[1], e2 = rowStart[2];

            for (int qx = qx0; qx <= x1; qx += 2, e0 += 2 * stepX[0], e1 += 2 * stepX[1], e2 += 2 * stepX[2]) {
                // Edge values for the 4 lanes (lane order is documented in shader.h).
                const int64_t le[3][4] = {
                    {e0, e0 + stepX[0], e0 + stepY[0], e0 + stepX[0] + stepY[0]},
                    {e1, e1 + stepX[1], e1 + stepY[1], e1 + stepX[1] + stepY[1]},
                    {e2, e2 + stepX[2], e2 + stepY[2], e2 + stepX[2] + stepY[2]},
                };

                int cover = 0;
                for (int lane = 0; lane < 4; ++lane) {
                    int x = qx + (lane & 1), y = qy + (lane >> 1);
                    if (x < x0 || x > x1 || y < y0 || y > y1) continue; // owned by another tile or off-target
                    if ((le[0][lane] + bias[0]) < 0 || (le[1][lane] + bias[1]) < 0 || (le[2][lane] + bias[2]) < 0)
                        continue;
                    cover |= 1 << lane;
                }
                if (!cover) continue;

                // Screen-space barycentrics for all lanes, including helper lanes.
                const F4 l0 = F4(float(le[0][0]), float(le[0][1]), float(le[0][2]), float(le[0][3])) * invArea;
                const F4 l1 = F4(float(le[1][0]), float(le[1][1]), float(le[1][2]), float(le[1][3])) * invArea;
                const F4 l2 = F4(float(le[2][0]), float(le[2][1]), float(le[2][2]), float(le[2][3])) * invArea;
                const F4 z = l0 * z0 + l1 * z1 + l2 * z2;

                // Early depth test.
                float zs[4];
                z.store(zs);
                const size_t base = static_cast<size_t>(qy) * width + qx;
                const size_t index[4] = {base, base + 1, base + width, base + width + 1};
                int alive = 0;
                for (int lane = 0; lane < 4; ++lane)
                    if ((cover >> lane & 1) && (!depthTest || zs[lane] < depth[index[lane]])) alive |= 1 << lane;
                if (!alive) continue;

                if (!shade) {
                    if (depthWrite)
                        for (int lane = 0; lane < 4; ++lane)
                            if (alive >> lane & 1) depth[index[lane]] = zs[lane];
                    continue;
                }

                // Perspective-correct barycentrics: interpolate attribute/w and 1/w.
                const F4 w0 = l0 * iw0, w1 = l1 * iw1, w2 = l2 * iw2;
                const F4 sumW = max(w0 + w1 + w2, F4(1e-30f)); // helper lanes may extrapolate past w = 0
                const F4 inv = F4(1.0f) / sumW;
                const F4 b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
                quad.barycentric[0] = b0;
                quad.barycentric[1] = b1;
                quad.barycentric[2] = b2;
                quad.depth = z;
                quad.x = qx;
                quad.y = qy;
                for (int k = 0; k < varyingCount; ++k)
                    quad.varyings[k] = b0 * F4(vary[0][k]) + b1 * F4(vary[1][k]) + b2 * F4(vary[2][k]);

                V3 out;
                M4 mask = M4::fromBits(alive);
                fragments += static_cast<uint64_t>((alive & 1) + (alive >> 1 & 1) + (alive >> 2 & 1) + (alive >> 3 & 1));
                if (m_simd) shader.fragmentQuad(quad, out, mask);
                else shader.Shader::fragmentQuad(quad, out, mask); // scalar reference path
                alive = mask.bits();
                if (!alive) continue;

                float r[4], g[4], b[4], viewDist[4];
                out.x.store(r);
                out.y.store(g);
                out.z.store(b);
                if (wire) inv.store(viewDist); // 1 / (1/w) = view distance

                for (int lane = 0; lane < 4; ++lane) {
                    if (!(alive >> lane & 1)) continue;
                    Vec3 c(r[lane], g[lane], b[lane]);
                    if (wire) {
                        // Pixel distance to the nearest original mesh edge; edges created by
                        // clipping are skipped so the overlay shows the real topology.
                        constexpr float kFar = 1e30f;
                        float d = std::min({(t.edgeMask & 1) ? float(le[0][lane]) * invEdgeLen[0] : kFar,
                                            (t.edgeMask & 2) ? float(le[1][lane]) * invEdgeLen[1] : kFar,
                                            (t.edgeMask & 4) ? float(le[2][lane]) * invEdgeLen[2] : kFar});
                        // Fade with view distance so dense far-away geometry doesn't turn into ink.
                        float fade = 1.0f - smoothstep(12.0f, 45.0f, viewDist[lane]);
                        c = lerp(c, m_wireColor, (1.0f - smoothstep(0.4f, 1.2f, d)) * fade);
                    }
                    color[index[lane]] = c;
                    if (depthWrite) depth[index[lane]] = zs[lane];
                }
            }

            rowStart[0] += 2 * stepY[0];
            rowStart[1] += 2 * stepY[1];
            rowStart[2] += 2 * stepY[2];
        }
    }

    m_fragmentCounters[static_cast<size_t>(worker) * 8] += fragments;
    if (m_tileTiming) m_tileTimes[tileIndex] = static_cast<float>(msSince(start));
}

} // namespace sr
