#include "test.h"

#include "core/texture.h"
#include "geometry/mesh.h"
#include "math/math.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace sr;

static bool nearlyIdentity(const Mat4& m, float eps)
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::fabs(m.m[i][j] - (i == j ? 1.0f : 0.0f)) > eps) return false;
    return true;
}

TEST_CASE("math: inverse of a composed transform")
{
    Mat4 m = Mat4::translation({1, -2, 3}) * Mat4::rotationY(0.7f) * Mat4::rotationX(-0.3f) * Mat4::scale({2, 0.5f, 3});
    CHECK(nearlyIdentity(m * m.inverse(), 1e-5f));
    CHECK(nearlyIdentity(m.inverse() * m, 1e-5f));
}

TEST_CASE("math: lookAt puts the eye at the origin looking down -Z")
{
    Vec3 eye(3, 4, 5), target(0, 1, 0);
    Mat4 v = Mat4::lookAt(eye, target, {0, 1, 0});
    Vec3 e = v.transformPoint(eye);
    CHECK_NEAR(length(e), 0.0, 1e-5);
    Vec3 t = v.transformPoint(target);
    CHECK_NEAR(t.x, 0.0, 1e-5);
    CHECK_NEAR(t.y, 0.0, 1e-5);
    CHECK_NEAR(t.z, -length(target - eye), 1e-4);
}

TEST_CASE("math: perspective maps near/far to NDC -1/+1")
{
    Mat4 p = Mat4::perspective(radians(60.0f), 1.5f, 0.5f, 50.0f);
    Vec4 n = p * Vec4(0, 0, -0.5f, 1);
    Vec4 f = p * Vec4(0, 0, -50.0f, 1);
    CHECK_NEAR(n.z / n.w, -1.0, 1e-5);
    CHECK_NEAR(f.z / f.w, 1.0, 1e-4);
    CHECK_NEAR(n.w, 0.5, 1e-6); // w = -z_view
}

TEST_CASE("math: normal matrix keeps normals perpendicular under non-uniform scale")
{
    Mat4 m = Mat4::rotationZ(0.4f) * Mat4::scale({4, 1, 1});
    Vec3 tangent = normalize(Vec3(1, 1, 0));
    Vec3 normal = normalize(Vec3(1, -1, 0));
    Vec3 t2 = m.transformDir(tangent);
    Vec3 n2 = m.normalMatrix().transformDir(normal);
    CHECK_NEAR(dot(normalize(t2), normalize(n2)), 0.0, 1e-5);
}

TEST_CASE("texture: full mip chain with box filtering")
{
    std::vector<Vec4> texels(256 * 256);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) texels[y * 256 + x] = ((x + y) & 1) ? Vec4(1, 1, 1, 1) : Vec4(0, 0, 0, 1);
    Texture t = Texture::fromLinear(256, 256, std::move(texels));
    CHECK(t.levelCount() == 9);
    Vec4 top = t.sampleNearest({0.3f, 0.6f}, 8);
    CHECK_NEAR(top.x, 0.5, 1e-6);
    // Magnification keeps LOD 0, minification by 4x selects LOD 2.
    CHECK_NEAR(t.computeLod({1.0f / 512, 0}, {0, 1.0f / 512}), 0.0, 1e-6);
    CHECK_NEAR(t.computeLod({4.0f / 256, 0}, {0, 4.0f / 256}), 2.0, 1e-5);
}

TEST_CASE("mesh: OBJ quads, negative indices and vertex de-duplication")
{
    auto path = std::filesystem::temp_directory_path() / "sr_test_quad.obj";
    {
        std::ofstream f(path);
        f << "# quad\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
             "f -4/-4 -3/-3 -2/-2 -1/-1\nf 1/1 2/2 3/3\n";
    }
    Mesh m;
    std::string err;
    CHECK(m.loadObj(path.string(), &err));
    CHECK(m.triangleCount() == 3);
    CHECK(m.vertices.size() == 4);
    CHECK_NEAR(m.vertices[0].normal.z, 1.0, 1e-5); // generated normals face +Z
    CHECK_NEAR(m.vertices[2].uv.y, 0.0, 1e-6);    // OBJ v is flipped to top-down
    std::filesystem::remove(path);

    Mesh missing;
    CHECK(!missing.loadObj("does/not/exist.obj"));
}

TEST_CASE("mesh: procedural primitives are closed and outward-facing")
{
    for (const Mesh& m : {Mesh::cube(1.0f), Mesh::uvSphere(1.0f, 16, 8), Mesh::torus(1.0f, 0.3f, 16, 8)}) {
        // Signed volume via the divergence theorem is positive for outward winding.
        double volume = 0.0;
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            Vec3 a = m.vertices[m.indices[i]].position;
            Vec3 b = m.vertices[m.indices[i + 1]].position;
            Vec3 c = m.vertices[m.indices[i + 2]].position;
            volume += dot(a, cross(b, c)) / 6.0;
        }
        CHECK(volume > 0.0);
    }
}
