#include "mesh.h"
#include <fstream>
#include <sstream>

Mesh loadOBJ(const char* path)
{
    std::vector<Vec3> vertices;
    Mesh mesh;

    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            vertices.emplace_back(x, y, z);
        }
        else if (type == "f") {
            int i0, i1, i2;
            ss >> i0 >> i1 >> i2;
            mesh.triangles.push_back({
                vertices[i0 - 1],
                vertices[i1 - 1],
                vertices[i2 - 1]
            });
        }
    }

    return mesh;
}