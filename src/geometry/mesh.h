#pragma once

#include "math/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sr {

struct Vertex {
    Vec3 position;
    Vec3 normal{0, 1, 0};
    Vec2 uv;
    Vec4 tangent{1, 0, 0, 1}; // xyz = tangent, w = bitangent sign
};

struct Bounds {
    Vec3 min{0.0f};
    Vec3 max{0.0f};
    Vec3 center() const { return (min + max) * 0.5f; }
    float radius() const { return length(max - min) * 0.5f; }
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // triangle list, counter-clockwise front faces

    size_t triangleCount() const { return indices.size() / 3; }
    Bounds bounds() const;

    // Area-weighted smooth normals from triangle geometry.
    void computeNormals();
    // Per-vertex tangent frames from UVs (needed for normal mapping).
    void computeTangents();
    // Re-centers on the origin and scales so the bounding sphere has the given radius.
    void normalizeToRadius(float radius);

    // Wavefront OBJ: v/vt/vn, polygon faces (fan-triangulated), negative indices.
    // Vertices are de-duplicated per unique (v, vt, vn) triple.
    // Returns false and leaves the mesh empty if the file can't be read.
    bool loadObj(const std::string& path, std::string* error = nullptr);

    static Mesh cube(float size = 1.0f);
    static Mesh plane(float size, float uvScale = 1.0f, int subdivisions = 1);
    static Mesh uvSphere(float radius, int segments, int rings);
    static Mesh torus(float majorRadius, float minorRadius, int majorSegments, int minorSegments);
};

} // namespace sr
