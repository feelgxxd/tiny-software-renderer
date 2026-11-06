#pragma once
// Tile-based, multithreaded triangle rasterizer.
//
// Frame flow:
//   begin(target)
//   draw(mesh, shader, state)   x N   -> vertex shading, clipping, culling,
//                                        triangle setup (parallel per draw)
//   end()                             -> bin triangles into 64x64 tiles, then
//                                        rasterize + shade tiles in parallel
//
// Every tile is owned by exactly one thread and processes its triangles in
// submission order, so no locks are needed on the color/depth buffers and the
// output is bit-identical regardless of thread count.
//
// Shaders passed to draw() are referenced until end(): keep them alive and do
// not modify their uniforms in between (use one shader instance per object).

#include "core/framebuffer.h"
#include "core/shader.h"
#include "core/thread_pool.h"
#include "geometry/mesh.h"

#include <memory>
#include <vector>

namespace sr {

enum class CullMode { None, Back, Front };

struct DrawState {
    CullMode cull = CullMode::Back;
    bool depthTest = true;
    bool depthWrite = true;
};

struct RenderStats {
    uint64_t trianglesSubmitted = 0;
    uint64_t trianglesCulled = 0;     // back-facing or degenerate
    uint64_t trianglesOutside = 0;    // rejected by frustum clipping
    uint64_t trianglesRasterized = 0; // after clipping (clipped polygons are fanned)
    uint64_t fragmentsShaded = 0;
    double geometryMs = 0.0;
    double rasterMs = 0.0;
};

class Renderer {
public:
    static constexpr int kTileSize = 64;
    static constexpr int kSubpixelBits = 8;

    explicit Renderer(int threadCount = 0);
    ~Renderer();

    void setThreadCount(int threadCount); // <= 0 = all hardware threads
    int threadCount() const;
    // Shared worker pool, also usable for full-screen passes (sky, resolve).
    ThreadPool& pool() { return *m_pool; }

    // Debug: draws triangle edges on top of shaded fragments.
    void setWireframeOverlay(bool enabled, Vec3 color = {0.02f, 0.02f, 0.02f});
    // Debug: records time spent per tile (see tileTimesMs()).
    void setTileTiming(bool enabled) { m_tileTiming = enabled; }
    // SIMD quad shading (default) vs. the scalar per-pixel reference path.
    void setSimdShading(bool enabled) { m_simd = enabled; }
    bool simdShading() const { return m_simd; }
    static const char* simdBackend() { return SR_SIMD_BACKEND; }

    void begin(Framebuffer& target);
    void draw(const Mesh& mesh, const Shader& shader, const DrawState& state = {});
    void end();

    const RenderStats& stats() const { return m_stats; }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    const std::vector<float>& tileTimesMs() const { return m_tileTimes; }

private:
    struct Triangle;
    struct DrawRecord {
        const Shader* shader;
        DrawState state;
    };
    struct ClipVertex;
    struct GeometryChunk;

    void rasterizeTile(int tileIndex, int worker);
    void emitTriangle(const ClipVertex* v[3], uint8_t edgeMask, uint32_t drawIndex, int varyingCount, GeometryChunk& out) const;
    void clipAndEmit(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, uint32_t drawIndex,
                     int varyingCount, GeometryChunk& out) const;

    std::unique_ptr<ThreadPool> m_pool;
    Framebuffer* m_target = nullptr;

    bool m_wireframe = false;
    Vec3 m_wireColor;
    bool m_tileTiming = false;
    bool m_simd = true;

    std::vector<DrawRecord> m_draws;
    std::vector<Triangle> m_triangles;
    std::vector<float> m_varyings; // 3 * varyingCount floats per triangle
    std::vector<ClipVertex> m_clipVerts;
    std::vector<GeometryChunk> m_chunks;

    int m_tilesX = 0, m_tilesY = 0;
    std::vector<std::vector<uint32_t>> m_bins;
    std::vector<float> m_tileTimes;
    std::vector<uint64_t> m_fragmentCounters; // per worker, padded

    RenderStats m_stats;
};

} // namespace sr
