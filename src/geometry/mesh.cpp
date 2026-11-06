#include "geometry/mesh.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace sr {

Bounds Mesh::bounds() const
{
    Bounds b;
    if (vertices.empty()) return b;
    b.min = b.max = vertices[0].position;
    for (const Vertex& v : vertices) {
        b.min = min(b.min, v.position);
        b.max = max(b.max, v.position);
    }
    return b;
}

void Mesh::computeNormals()
{
    for (Vertex& v : vertices) v.normal = Vec3(0.0f);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        Vec3 n = cross(b.position - a.position, c.position - a.position); // length = 2 * area
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : vertices) v.normal = normalize(v.normal);
}

void Mesh::computeTangents()
{
    std::vector<Vec3> tan(vertices.size(), Vec3(0.0f));
    std::vector<Vec3> bitan(vertices.size(), Vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        Vec3 e1 = v1.position - v0.position;
        Vec3 e2 = v2.position - v0.position;
        Vec2 d1 = v1.uv - v0.uv;
        Vec2 d2 = v2.uv - v0.uv;

        float det = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.0f / det;

        Vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        Vec3 b = (e2 * d1.x - e1 * d2.x) * r;
        for (uint32_t idx : {i0, i1, i2}) {
            tan[idx] += t;
            bitan[idx] += b;
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        Vec3 n = vertices[i].normal;
        // Gram-Schmidt orthogonalize against the normal.
        Vec3 t = tan[i] - n * dot(n, tan[i]);
        if (dot(t, t) < 1e-12f) {
            // No usable UV gradient: pick any vector perpendicular to n.
            t = std::fabs(n.x) < 0.9f ? cross(n, Vec3(1, 0, 0)) : cross(n, Vec3(0, 1, 0));
        }
        t = normalize(t);
        // UVs here are top-down (v grows downwards), while normal maps use the
        // OpenGL convention (+Y = up in the image), so the bitangent is -dP/dv.
        float handedness = dot(cross(n, t), -bitan[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = Vec4(t, handedness);
    }
}

void Mesh::normalizeToRadius(float radius)
{
    Bounds b = bounds();
    Vec3 c = b.center();
    float r = b.radius();
    float s = r > 0.0f ? radius / r : 1.0f;
    for (Vertex& v : vertices) v.position = (v.position - c) * s;
}

// ---------------------------------------------------------------------------
// OBJ loading

namespace {

struct ObjKey {
    int v, vt, vn;
    bool operator==(const ObjKey& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
};

struct ObjKeyHash {
    size_t operator()(const ObjKey& k) const
    {
        return (static_cast<size_t>(k.v) * 73856093u) ^ (static_cast<size_t>(k.vt) * 19349663u) ^
               (static_cast<size_t>(k.vn) * 83492791u);
    }
};

// Resolves 1-based / negative OBJ indices to 0-based; -1 means absent.
int resolveIndex(const char* s, size_t count)
{
    if (!s || !*s) return -1;
    long i = std::strtol(s, nullptr, 10);
    if (i > 0) return static_cast<int>(i - 1);
    if (i < 0) return static_cast<int>(static_cast<long>(count) + i);
    return -1;
}

} // namespace

bool Mesh::loadObj(const std::string& path, std::string* error)
{
    vertices.clear();
    indices.clear();

    std::ifstream file(path);
    if (!file) {
        if (error) *error = "cannot open " + path;
        return false;
    }

    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> cache;
    bool hasNormals = true;

    std::string line;
    std::vector<uint32_t> face;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v") {
            Vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (type == "vt") {
            Vec2 t;
            ss >> t.x >> t.y;
            uvs.push_back({t.x, 1.0f - t.y}); // OBJ has v=0 at the bottom; textures here are top-down
        } else if (type == "vn") {
            Vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(normalize(n));
        } else if (type == "f") {
            face.clear();
            std::string token;
            while (ss >> token) {
                // Split "v", "v/vt", "v//vn", "v/vt/vn".
                std::string parts[3];
                int p = 0;
                for (char ch : token) {
                    if (ch == '/') { if (++p > 2) break; }
                    else parts[p] += ch;
                }
                ObjKey key{resolveIndex(parts[0].c_str(), positions.size()),
                           resolveIndex(parts[1].c_str(), uvs.size()),
                           resolveIndex(parts[2].c_str(), normals.size())};
                if (key.v < 0 || key.v >= static_cast<int>(positions.size())) {
                    if (error) *error = "invalid vertex index in " + path;
                    vertices.clear();
                    indices.clear();
                    return false;
                }

                auto it = cache.find(key);
                if (it == cache.end()) {
                    Vertex v;
                    v.position = positions[key.v];
                    if (key.vt >= 0 && key.vt < static_cast<int>(uvs.size())) v.uv = uvs[key.vt];
                    if (key.vn >= 0 && key.vn < static_cast<int>(normals.size())) v.normal = normals[key.vn];
                    else hasNormals = false;
                    it = cache.emplace(key, static_cast<uint32_t>(vertices.size())).first;
                    vertices.push_back(v);
                }
                face.push_back(it->second);
            }
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                indices.push_back(face[0]);
                indices.push_back(face[i]);
                indices.push_back(face[i + 1]);
            }
        }
    }

    if (indices.empty()) {
        if (error) *error = "no faces in " + path;
        vertices.clear();
        return false;
    }

    if (!hasNormals) computeNormals();
    computeTangents();
    return true;
}

// ---------------------------------------------------------------------------
// Procedural primitives

Mesh Mesh::cube(float size)
{
    Mesh mesh;
    float s = size * 0.5f;
    struct Face { Vec3 n, u, v; };
    const Face faces[6] = {
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    };
    for (const Face& f : faces) {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& c : corners) {
            Vertex v;
            v.position = (f.n + f.u * c[0] + f.v * c[1]) * s;
            v.normal = f.n;
            v.uv = {(c[0] + 1) * 0.5f, 1.0f - (c[1] + 1) * 0.5f};
            mesh.vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    }
    mesh.computeTangents();
    return mesh;
}

Mesh Mesh::plane(float size, float uvScale, int subdivisions)
{
    Mesh mesh;
    const int n = std::max(1, subdivisions);
    for (int z = 0; z <= n; ++z) {
        for (int x = 0; x <= n; ++x) {
            float u = static_cast<float>(x) / n, v = static_cast<float>(z) / n;
            Vertex vert;
            vert.position = {(u - 0.5f) * size, 0.0f, (v - 0.5f) * size};
            vert.normal = {0, 1, 0};
            vert.uv = {u * uvScale, v * uvScale};
            mesh.vertices.push_back(vert);
        }
    }
    const uint32_t stride = static_cast<uint32_t>(n + 1);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            uint32_t a = z * stride + x, b = a + 1, c = a + stride, d = c + 1;
            // Counter-clockwise seen from +Y.
            mesh.indices.insert(mesh.indices.end(), {a, c, d, a, d, b});
        }
    }
    mesh.computeTangents();
    return mesh;
}

Mesh Mesh::uvSphere(float radius, int segments, int rings)
{
    Mesh mesh;
    for (int r = 0; r <= rings; ++r) {
        float v = static_cast<float>(r) / rings;
        float phi = v * kPi;
        for (int s = 0; s <= segments; ++s) {
            float u = static_cast<float>(s) / segments;
            float theta = u * 2.0f * kPi;
            Vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), -std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = n * radius;
            vert.normal = n;
            vert.uv = {u, v};
            mesh.vertices.push_back(vert);
        }
    }
    uint32_t stride = static_cast<uint32_t>(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t a = r * stride + s, b = a + stride;
            if (r != 0) mesh.indices.insert(mesh.indices.end(), {a, b, a + 1});
            if (r != rings - 1) mesh.indices.insert(mesh.indices.end(), {a + 1, b, b + 1});
        }
    }
    mesh.computeTangents();
    return mesh;
}

Mesh Mesh::torus(float majorRadius, float minorRadius, int majorSegments, int minorSegments)
{
    Mesh mesh;
    for (int i = 0; i <= majorSegments; ++i) {
        float u = static_cast<float>(i) / majorSegments;
        float a = u * 2.0f * kPi;
        Vec3 center(std::cos(a) * majorRadius, 0.0f, -std::sin(a) * majorRadius);
        for (int j = 0; j <= minorSegments; ++j) {
            float v = static_cast<float>(j) / minorSegments;
            float b = v * 2.0f * kPi;
            Vec3 n(std::cos(a) * std::cos(b), std::sin(b), -std::sin(a) * std::cos(b));
            Vertex vert;
            vert.position = center + n * minorRadius;
            vert.normal = n;
            vert.uv = {u * 4.0f, v};
            mesh.vertices.push_back(vert);
        }
    }
    uint32_t stride = static_cast<uint32_t>(minorSegments + 1);
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            uint32_t a = i * stride + j, b = a + stride;
            mesh.indices.insert(mesh.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
    }
    mesh.computeTangents();
    return mesh;
}

} // namespace sr
