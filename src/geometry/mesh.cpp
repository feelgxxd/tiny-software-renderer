#include "mesh.h"
#include <fstream>
#include <sstream>
#include <cmath>

Mesh Mesh::loadOBJ(const std::string& filepath)
{
    Mesh mesh;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec3> texCoords;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return mesh;  // Return empty mesh if file can't be opened
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::stringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            positions.push_back(Vec3(x, y, z));
        }
        else if (type == "vn") {
            float x, y, z;
            ss >> x >> y >> z;
            normals.push_back(Vec3(x, y, z).normalized());
        }
        else if (type == "vt") {
            float x, y;
            ss >> x >> y;
            texCoords.push_back(Vec3(x, y, 0.0f));
        }
        else if (type == "f") {
            // Parse face - supports "v", "v/vt", "v/vt/vn", "v//vn"
            std::vector<uint32_t> faceIndices;
            std::string vertexStr;
            
            while (ss >> vertexStr) {
                std::stringstream vs(vertexStr);
                uint32_t posIdx = 0, texIdx = 0, normIdx = 0;
                char slash;
                
                vs >> posIdx;
                if (vs.peek() == '/') {
                    vs >> slash;
                    if (vs.peek() != '/') {
                        vs >> texIdx;
                    }
                    if (vs.peek() == '/') {
                        vs >> slash >> normIdx;
                    }
                }
                
                posIdx--;  // OBJ indices are 1-based
                
                Vertex vertex;
                vertex.position = (posIdx < positions.size()) ? positions[posIdx] : Vec3(0, 0, 0);
                vertex.normal = (normIdx > 0 && normIdx <= normals.size()) ? normals[normIdx - 1] : Vec3(0, 0, 1);
                vertex.texCoord = (texIdx > 0 && texIdx <= texCoords.size()) ? texCoords[texIdx - 1] : Vec3(0, 0, 0);
                
                mesh.vertices.push_back(vertex);
                faceIndices.push_back(mesh.vertices.size() - 1);
            }
            
            // Triangulate polygon (fan triangulation)
            for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                mesh.indices.push_back(faceIndices[0]);
                mesh.indices.push_back(faceIndices[i]);
                mesh.indices.push_back(faceIndices[i + 1]);
            }
        }
    }

    file.close();
    return mesh;
}

Mesh Mesh::createCube(float size)
{
    Mesh mesh;
    float s = size / 2.0f;

    // Define cube vertices
    Vec3 positions[] = {
        // Front face
        Vec3(-s, -s,  s), Vec3( s, -s,  s), Vec3( s,  s,  s), Vec3(-s,  s,  s),
        // Back face
        Vec3(-s, -s, -s), Vec3(-s,  s, -s), Vec3( s,  s, -s), Vec3( s, -s, -s),
        // Top face
        Vec3(-s,  s, -s), Vec3(-s,  s,  s), Vec3( s,  s,  s), Vec3( s,  s, -s),
        // Bottom face
        Vec3(-s, -s, -s), Vec3( s, -s, -s), Vec3( s, -s,  s), Vec3(-s, -s,  s),
        // Right face
        Vec3( s, -s, -s), Vec3( s,  s, -s), Vec3( s,  s,  s), Vec3( s, -s,  s),
        // Left face
        Vec3(-s, -s, -s), Vec3(-s, -s,  s), Vec3(-s,  s,  s), Vec3(-s,  s, -s)
    };

    Vec3 normals[] = {
        // Front
        Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1),
        // Back
        Vec3(0, 0, -1), Vec3(0, 0, -1), Vec3(0, 0, -1), Vec3(0, 0, -1),
        // Top
        Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0),
        // Bottom
        Vec3(0, -1, 0), Vec3(0, -1, 0), Vec3(0, -1, 0), Vec3(0, -1, 0),
        // Right
        Vec3(1, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 0),
        // Left
        Vec3(-1, 0, 0), Vec3(-1, 0, 0), Vec3(-1, 0, 0), Vec3(-1, 0, 0)
    };

    // Create vertices
    for (int i = 0; i < 24; i++) {
        Vertex v;
        v.position = positions[i];
        v.normal = normals[i];
        v.texCoord = Vec3(0, 0, 0);  // TODO: proper UV mapping
        mesh.vertices.push_back(v);
    }

    // Create indices (2 triangles per face)
    uint32_t faceIndices[] = {
        0, 1, 2,   0, 2, 3,    // Front
        4, 6, 5,   4, 7, 6,    // Back
        8, 9, 10,  8, 10, 11,  // Top
        12, 14, 13, 12, 15, 14,// Bottom
        16, 17, 18, 16, 18, 19,// Right
        20, 22, 21, 20, 23, 22 // Left
    };

    for (int i = 0; i < 36; i++) {
        mesh.indices.push_back(faceIndices[i]);
    }

    return mesh;
}
