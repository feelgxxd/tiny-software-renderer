#include "render/scene.h"

#include "render/procedural.h"

#include <cmath>

namespace sr {

namespace {

std::shared_ptr<Texture> share(Texture t) { return std::make_shared<Texture>(std::move(t)); }

SceneObject& addObject(Scene& scene, std::shared_ptr<const Mesh> mesh, const Mat4& transform, const Material& material)
{
    SceneObject obj;
    obj.mesh = std::move(mesh);
    obj.transform = transform;
    obj.material = material;
    scene.objects.push_back(std::move(obj));
    return scene.objects.back();
}

std::shared_ptr<Texture> groundChecker(Scene& scene)
{
    auto tex = share(procedural::checker(512, 8, Vec3(0.42f), Vec3(0.30f)));
    scene.textures.push_back(tex);
    return tex;
}

// 12 spheres: dielectric and metal rows, roughness increasing left to right.
void buildMaterials(Scene& s)
{
    s.description = "PBR metallic/roughness sweep";
    auto sphere = std::make_shared<Mesh>(Mesh::uvSphere(0.42f, 64, 40));
    auto ground = std::make_shared<Mesh>(Mesh::plane(400.0f, 1.0f, 40));
    auto checker = groundChecker(s);

    Material groundMat;
    groundMat.albedoMap = checker.get();
    groundMat.roughness = 0.85f;
    groundMat.uvScale = {80.0f, 80.0f};
    addObject(s, ground, Mat4::identity(), groundMat).castsShadow = false;

    const int cols = 6;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < cols; ++col) {
            Material m;
            m.roughness = 0.06f + 0.94f * static_cast<float>(col) / (cols - 1);
            if (row == 0) {
                m.metallic = 1.0f;
                m.albedo = {1.0f, 0.76f, 0.33f}; // gold
            } else {
                m.metallic = 0.0f;
                m.albedo = {0.75f, 0.06f, 0.05f};
            }
            float x = (static_cast<float>(col) - (cols - 1) * 0.5f) * 1.0f;
            float y = row == 0 ? 1.52f : 0.5f;
            addObject(s, sphere, Mat4::translation({x, y, 0.0f}), m);
        }
    }

    s.light.direction = normalize(Vec3(-0.45f, -0.8f, -0.55f));
    s.light.intensity = 3.2f;
    s.camera.target = {0.0f, 1.0f, 0.0f};
    s.camera.distance = 7.4f;
    s.camera.pitch = 0.1f;
    s.camera.yaw = 0.0f;
    s.camera.fovY = radians(40.0f);
    s.shadowCenter = {0.0f, 0.8f, 0.0f};
    s.shadowRadius = 4.5f;
    s.turntableSpeed = 0.0f;
}

void buildShadows(Scene& s)
{
    s.description = "Shadow mapping, normal mapping, animation";
    auto brickAlbedo = std::make_shared<Texture>();
    auto brickNormal = std::make_shared<Texture>();
    procedural::bricks(512, *brickAlbedo, *brickNormal);
    s.textures.push_back(brickAlbedo);
    s.textures.push_back(brickNormal);
    auto checker = groundChecker(s);

    Material groundMat;
    groundMat.albedoMap = checker.get();
    groundMat.roughness = 0.8f;
    groundMat.uvScale = {100.0f, 100.0f};
    addObject(s, std::make_shared<Mesh>(Mesh::plane(400.0f, 1.0f, 40)), Mat4::identity(), groundMat).castsShadow = false;

    Material brick;
    brick.albedoMap = brickAlbedo.get();
    brick.normalMap = brickNormal.get();
    brick.roughness = 0.75f;
    auto& cube = addObject(s, std::make_shared<Mesh>(Mesh::cube(1.3f)), Mat4::identity(), brick);
    cube.animate = [](float t) { return Mat4::translation({-1.5f, 0.65f, -0.2f}) * Mat4::rotationY(t * 0.5f + 0.5f); };

    Material copper;
    copper.albedo = {0.95f, 0.64f, 0.54f};
    copper.metallic = 1.0f;
    copper.roughness = 0.3f;
    auto& torus = addObject(s, std::make_shared<Mesh>(Mesh::torus(0.62f, 0.22f, 96, 32)), Mat4::identity(), copper);
    torus.animate = [](float t) {
        return Mat4::translation({1.4f, 1.0f, -0.3f}) * Mat4::rotationZ(0.5f) * Mat4::rotationX(t * 1.0f + 1.2f);
    };

    Material plastic;
    plastic.albedo = {0.1f, 0.35f, 0.8f};
    plastic.roughness = 0.25f;
    addObject(s, std::make_shared<Mesh>(Mesh::uvSphere(0.55f, 64, 40)), Mat4::translation({0.1f, 0.55f, 1.3f}),
              plastic);

    s.light.direction = normalize(Vec3(-0.75f, -0.55f, -0.45f));
    s.light.intensity = 3.5f;
    s.camera.target = {0.0f, 0.6f, 0.0f};
    s.camera.distance = 6.2f;
    s.camera.pitch = 0.38f;
    s.camera.yaw = 0.35f;
    s.shadowCenter = {0.0f, 0.5f, 0.0f};
    s.shadowRadius = 4.0f;
}

void buildTextures(Scene& s)
{
    s.description = "Perspective-correct texturing and trilinear mipmapping";
    s.fogDensity = 0.012f;
    auto grid = share(procedural::uvGrid(1024, 8));
    s.textures.push_back(grid);
    auto brickAlbedo = std::make_shared<Texture>();
    auto brickNormal = std::make_shared<Texture>();
    procedural::bricks(512, *brickAlbedo, *brickNormal);
    s.textures.push_back(brickAlbedo);
    s.textures.push_back(brickNormal);

    Material floor;
    floor.albedoMap = grid.get();
    floor.roughness = 0.9f;
    floor.uvScale = {60.0f, 60.0f};
    addObject(s, std::make_shared<Mesh>(Mesh::plane(240.0f, 1.0f, 48)), Mat4::identity(), floor).castsShadow = false;

    Material wall;
    wall.albedoMap = brickAlbedo.get();
    wall.normalMap = brickNormal.get();
    wall.roughness = 0.8f;
    wall.uvScale = {3.0f, 1.0f};
    auto wallMesh = std::make_shared<Mesh>(Mesh::cube(1.0f));
    for (int i = 0; i < 6; ++i) {
        float z = -2.0f - static_cast<float>(i) * 6.0f;
        addObject(s, wallMesh, Mat4::translation({-3.2f, 1.0f, z}) * Mat4::scale({0.4f, 2.0f, 5.0f}), wall);
        addObject(s, wallMesh, Mat4::translation({3.2f, 1.0f, z}) * Mat4::scale({0.4f, 2.0f, 5.0f}), wall);
    }

    s.light.direction = normalize(Vec3(-0.3f, -0.7f, -0.6f));
    s.light.intensity = 3.0f;
    s.camera.target = {0.0f, 0.9f, -6.0f};
    s.camera.distance = 7.5f;
    s.camera.pitch = 0.06f;
    s.camera.yaw = 0.0f;
    s.camera.fovY = radians(60.0f);
    s.shadowCenter = {0.0f, 0.0f, -10.0f};
    s.shadowRadius = 22.0f;
    s.turntableSpeed = 0.0f;
}

bool buildSpot(Scene& s, const std::string& assetDir, std::string* error)
{
    s.description = "Spot the cow (Keenan Crane, CC0): OBJ loading + texturing";
    auto mesh = std::make_shared<Mesh>();
    std::string objPath = assetDir + "/spot/spot_triangulated.obj";
    if (!mesh->loadObj(objPath, error)) return false;
    mesh->normalizeToRadius(1.0f);
    float minY = mesh->bounds().min.y;

    auto tex = share(Texture::load(assetDir + "/spot/spot_texture.png", true));
    if (!tex->valid()) {
        if (error) *error = "cannot load " + assetDir + "/spot/spot_texture.png";
        return false;
    }
    s.textures.push_back(tex);
    auto checker = groundChecker(s);

    Material groundMat;
    groundMat.albedoMap = checker.get();
    groundMat.roughness = 0.85f;
    groundMat.uvScale = {114.0f, 114.0f};
    addObject(s, std::make_shared<Mesh>(Mesh::plane(400.0f, 1.0f, 40)), Mat4::identity(), groundMat).castsShadow = false;

    Material cow;
    cow.albedoMap = tex.get();
    cow.roughness = 0.55f;
    auto& obj = addObject(s, mesh, Mat4::identity(), cow);
    obj.animate = [minY](float t) { return Mat4::translation({0.0f, -minY, 0.0f}) * Mat4::rotationY(2.4f + t * 0.5f); };

    s.light.direction = normalize(Vec3(-0.55f, -0.75f, -0.35f));
    s.light.intensity = 3.2f;
    s.camera.target = {0.0f, 0.75f, 0.0f};
    s.camera.distance = 4.2f;
    s.camera.pitch = 0.3f;
    s.camera.yaw = 0.3f;
    s.shadowCenter = {0.0f, 0.6f, 0.0f};
    s.shadowRadius = 2.6f;
    s.turntableSpeed = 0.0f;
    return true;
}

} // namespace

const std::vector<std::string>& sceneNames()
{
    static const std::vector<std::string> names = {"materials", "shadows", "textures", "spot"};
    return names;
}

bool buildScene(const std::string& name, const std::string& assetDir, Scene& out, std::string* error)
{
    out = Scene{};
    out.name = name;
    if (name == "materials") buildMaterials(out);
    else if (name == "shadows") buildShadows(out);
    else if (name == "textures") buildTextures(out);
    else if (name == "spot") return buildSpot(out, assetDir, error);
    else {
        if (error) *error = "unknown scene '" + name + "'";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

void SceneRenderer::drawSky(Renderer& renderer, Framebuffer& target, const Mat4& invViewProj, const Vec3& cameraPos,
                            const Environment& env)
{
    const int w = target.width(), h = target.height();
    Vec3* color = target.colorData();
    renderer.pool().parallelFor(h, [&](int y, int) {
        float ndcY = 1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float ndcX = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 1.0f;
            Vec4 far = invViewProj * Vec4(ndcX, ndcY, 1.0f, 1.0f);
            Vec3 dir = normalize(far.xyz() / far.w - cameraPos);
            // Below the horizon the background shows the fogged-out ground, i.e. the horizon color.
            dir = normalize(Vec3(dir.x, std::max(dir.y, 0.0f), dir.z));
            color[static_cast<size_t>(y) * w + x] = env.radiance(dir);
        }
    });
}

void SceneRenderer::render(Renderer& renderer, Scene& scene, const OrbitCamera& camera, float time,
                           Framebuffer& target, const RenderSettings& settings)
{
    // Update object transforms and per-object shader uniforms.
    for (SceneObject& obj : scene.objects) {
        if (obj.animate) obj.transform = obj.animate(time);
        obj.shader.frame = &m_frame;
        obj.shader.material = obj.material;
        obj.shader.setModel(obj.transform);
    }

    // Shadow pass.
    m_shadowMs = 0.0;
    const bool shadows = settings.shadows && settings.view != DebugView::Albedo && settings.view != DebugView::Normals;
    if (shadows) {
        m_shadow.resize(settings.shadowMapSize);
        m_shadow.fit(scene.light, scene.shadowCenter, scene.shadowRadius);
        m_shadow.target().clearDepth(1.0f);
        renderer.setWireframeOverlay(false);
        renderer.begin(m_shadow.target());
        DrawState state;
        state.cull = CullMode::None; // thin/open meshes must still cast shadows
        for (SceneObject& obj : scene.objects) {
            if (!obj.castsShadow) continue;
            obj.shadowShader.lightMvp = m_shadow.lightViewProj() * obj.transform;
            renderer.draw(*obj.mesh, obj.shadowShader, state);
        }
        renderer.end();
        m_shadowMs = renderer.stats().geometryMs + renderer.stats().rasterMs;
    }

    // Main pass.
    const float aspect = static_cast<float>(target.width()) / static_cast<float>(target.height());
    const Mat4 view = camera.view();
    const Mat4 proj = camera.projection(aspect);
    m_frame.viewProj = proj * view;
    m_frame.cameraPos = camera.position();
    m_frame.zNear = camera.zNear;
    m_frame.zFar = camera.zFar;
    m_frame.light = scene.light;
    m_frame.env = scene.env;
    m_frame.shadow = shadows ? &m_shadow : nullptr;
    m_frame.view = settings.view;
    m_frame.fogDensity = scene.fogDensity;

    if (toneMapFor(settings.view) == ToneMap::AcesFilmic) {
        target.clearDepth(1.0f);
        drawSky(renderer, target, m_frame.viewProj.inverse(), m_frame.cameraPos, scene.env);
    } else {
        target.clear(Vec3(0.02f));
    }

    renderer.setWireframeOverlay(settings.wireframe);
    renderer.begin(target);
    for (SceneObject& obj : scene.objects) renderer.draw(*obj.mesh, obj.shader);
    renderer.end();
    m_mainMs = renderer.stats().geometryMs + renderer.stats().rasterMs;
}

} // namespace sr
