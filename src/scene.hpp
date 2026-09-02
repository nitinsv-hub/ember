#pragma once

#include "vecmath.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace ember {

enum class Bsdf : int {
    Diffuse = 0,
    Metal = 1,
    Dielectric = 2,
};

struct Material {
    std::string name;
    Vec3 albedo{0.75f, 0.75f, 0.75f};
    Vec3 emission{0.0f, 0.0f, 0.0f};
    Bsdf bsdf = Bsdf::Diffuse;
    float roughness = 0.35f;
    float ior = 1.5f;
};

struct Camera {
    Vec3 position{0.0f, 0.0f, -1.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_degrees = 40.0f;
};

struct RenderSettings {
    int width = 800;
    int height = 600;
    int samples = 256;
    int batch = 4;
    int cycles = 0;
    int bounces = 8;
    float exposure = 1.0f;
    float radiance_clamp = 0.0f;
};

struct Sky {
    Vec3 zenith{0.0f, 0.0f, 0.0f};
    Vec3 horizon{0.0f, 0.0f, 0.0f};
};

struct Sun {
    bool enabled = false;
    Vec3 direction{0.4f, 0.8f, -0.35f};
    Vec3 irradiance{2.6f, 2.45f, 2.2f};
    float angular_radius_degrees = 1.5f;
};

struct Scene {
    std::string name;
    std::vector<std::array<Vec3, 3>> positions;
    std::vector<std::array<Vec3, 3>> normals;
    std::vector<int> material_ids;
    std::vector<Material> materials;

    Camera camera;
    RenderSettings settings;
    Sky sky;
    Sun sun;

    Aabb bounds;
    bool camera_auto = false;

    size_t triangle_count() const { return positions.size(); }
    bool has_emitters() const;
};

bool is_scene_input(const std::filesystem::path& path);
Scene load_scene(const std::filesystem::path& path);
void frame_camera(Scene& scene);
std::vector<std::filesystem::path> collect_inputs(const std::filesystem::path& root);

}
