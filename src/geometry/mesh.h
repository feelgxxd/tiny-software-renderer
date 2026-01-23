#pragma once
#include <vector>
#include <string>
#include "../math/vec3.h"

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 texCoord;  // UV coordinates (stored as Vec3 for now)
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;  // Index buffer

    // Get triangle by index
    void getTriangle(uint32_t triangleIndex, Vertex& v0, Vertex& v1, Vertex& v2) const {
        if (triangleIndex * 3 + 2 >= indices.size()) return;
        
        uint32_t idx0 = indices[triangleIndex * 3];
        uint32_t idx1 = indices[triangleIndex * 3 + 1];
        uint32_t idx2 = indices[triangleIndex * 3 + 2];
        
        v0 = vertices[idx0];
        v1 = vertices[idx1];
        v2 = vertices[idx2];
    }
    
    uint32_t getTriangleCount() const {
        return indices.size() / 3;
    }

    // Static method to load OBJ file
    static Mesh loadOBJ(const std::string& filepath);
    
    // Static method to create a simple cube
    static Mesh createCube(float size = 1.0f);
};
