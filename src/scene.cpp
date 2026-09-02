#include "scene.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace ember {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream is(line);
    std::string tok;
    while (is >> tok) out.push_back(tok);
    return out;
}

struct FaceRef {
    int position = -1;
    int normal = -1;
};

FaceRef parse_face_ref(const std::string& token, size_t position_count, size_t normal_count) {
    FaceRef ref;
    int field = 0;
    std::string current;
    auto commit = [&]() {
        if (!current.empty()) {
            int value = std::stoi(current);
            if (field == 0) {
                ref.position = value > 0 ? value - 1 : static_cast<int>(position_count) + value;
            } else if (field == 2) {
                ref.normal = value > 0 ? value - 1 : static_cast<int>(normal_count) + value;
            }
        }
        current.clear();
        ++field;
    };
    for (char c : token) {
        if (c == '/') {
            commit();
        } else {
            current.push_back(c);
        }
    }
    commit();
    return ref;
}

std::vector<Material> load_mtl(const fs::path& path, std::map<std::string, int>& index_by_name) {
    std::vector<Material> materials;
    std::ifstream file(path);
    if (!file) return materials;

    auto current = [&]() -> Material& {
        if (materials.empty()) {
            materials.push_back(Material{});
            index_by_name["__default"] = 0;
        }
        return materials.back();
    };

    std::string line;
    while (std::getline(file, line)) {
        const auto tokens = tokenize(line);
        if (tokens.empty() || tokens[0][0] == '#') continue;
        const std::string key = lower(tokens[0]);

        if (key == "newmtl" && tokens.size() >= 2) {
            Material m;
            m.name = tokens[1];
            index_by_name[m.name] = static_cast<int>(materials.size());
            materials.push_back(m);
            continue;
        }
        if (materials.empty()) continue;
        Material& m = current();

        auto vec = [&](size_t offset) {
            return Vec3{std::stof(tokens[offset]), std::stof(tokens[offset + 1]),
                        std::stof(tokens[offset + 2])};
        };

        if (key == "kd" && tokens.size() >= 4) m.albedo = vec(1);
        else if (key == "ke" && tokens.size() >= 4) m.emission = vec(1);
        else if (key == "ks" && tokens.size() >= 4) {
            const Vec3 ks = vec(1);
            if (luminance(ks) > 0.25f) {
                m.bsdf = Bsdf::Metal;
                m.albedo = ks;
            }
        } else if (key == "ns" && tokens.size() >= 2) {
            const float shininess = std::stof(tokens[1]);
            m.roughness = std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.01f, 1.0f);
        } else if (key == "ni" && tokens.size() >= 2) {
            m.ior = std::stof(tokens[1]);
        } else if (key == "d" && tokens.size() >= 2) {
            if (std::stof(tokens[1]) < 0.999f) m.bsdf = Bsdf::Dielectric;
        } else if (key == "illum" && tokens.size() >= 2) {
            const int illum = std::stoi(tokens[1]);
            if (illum == 7 || illum == 6) m.bsdf = Bsdf::Dielectric;
            else if (illum == 3 || illum == 5) m.bsdf = Bsdf::Metal;
        }
    }
    return materials;
}

void build_normals(Scene& scene, const std::vector<std::array<int, 3>>& position_refs,
                   size_t unique_positions) {
    std::vector<Vec3> accumulated(unique_positions, Vec3{});
    for (size_t t = 0; t < scene.positions.size(); ++t) {
        const auto& p = scene.positions[t];
        const Vec3 face = cross(p[1] - p[0], p[2] - p[0]);
        for (int k = 0; k < 3; ++k) {
            const int index = position_refs[t][k];
            if (index >= 0 && static_cast<size_t>(index) < unique_positions)
                accumulated[static_cast<size_t>(index)] += face;
        }
    }
    for (size_t t = 0; t < scene.positions.size(); ++t) {
        const auto& p = scene.positions[t];
        const Vec3 face = normalize(cross(p[1] - p[0], p[2] - p[0]));
        for (int k = 0; k < 3; ++k) {
            const int index = position_refs[t][k];
            Vec3 n = face;
            if (index >= 0 && static_cast<size_t>(index) < unique_positions) {
                const Vec3 smooth = accumulated[static_cast<size_t>(index)];
                if (length(smooth) > 1e-12f && dot(normalize(smooth), face) > 0.5f)
                    n = normalize(smooth);
            }
            scene.normals[t][k] = n;
        }
    }
}

void load_obj_into(Scene& scene, const fs::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open mesh: " + path.string());

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::map<std::string, int> material_index;
    std::vector<std::array<int, 3>> position_refs;
    int active_material = -1;
    bool any_normals = false;

    std::string line;
    while (std::getline(file, line)) {
        const auto tokens = tokenize(line);
        if (tokens.empty() || tokens[0][0] == '#') continue;
        const std::string key = tokens[0];

        if (key == "v" && tokens.size() >= 4) {
            positions.push_back({std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])});
        } else if (key == "vn" && tokens.size() >= 4) {
            normals.push_back({std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])});
        } else if (key == "mtllib" && tokens.size() >= 2) {
            const auto loaded = load_mtl(path.parent_path() / tokens[1], material_index);
            if (!loaded.empty()) scene.materials = loaded;
        } else if (key == "usemtl" && tokens.size() >= 2) {
            const auto it = material_index.find(tokens[1]);
            active_material = (it == material_index.end()) ? -1 : it->second;
        } else if (key == "f" && tokens.size() >= 4) {
            std::vector<FaceRef> refs;
            refs.reserve(tokens.size() - 1);
            for (size_t i = 1; i < tokens.size(); ++i)
                refs.push_back(parse_face_ref(tokens[i], positions.size(), normals.size()));

            for (size_t i = 1; i + 1 < refs.size(); ++i) {
                const FaceRef tri[3] = {refs[0], refs[i], refs[i + 1]};
                std::array<Vec3, 3> p{};
                std::array<Vec3, 3> n{};
                std::array<int, 3> pref{};
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    if (tri[k].position < 0 || static_cast<size_t>(tri[k].position) >= positions.size()) {
                        ok = false;
                        break;
                    }
                    p[k] = positions[static_cast<size_t>(tri[k].position)];
                    pref[k] = tri[k].position;
                    if (tri[k].normal >= 0 && static_cast<size_t>(tri[k].normal) < normals.size()) {
                        n[k] = normals[static_cast<size_t>(tri[k].normal)];
                        any_normals = true;
                    }
                }
                if (!ok) continue;
                scene.positions.push_back(p);
                scene.normals.push_back(n);
                scene.material_ids.push_back(active_material);
                position_refs.push_back(pref);
            }
        }
    }

    if (scene.positions.empty()) throw std::runtime_error("mesh contains no triangles: " + path.string());

    if (scene.materials.empty()) scene.materials.push_back(Material{});
    for (int& id : scene.material_ids)
        if (id < 0 || static_cast<size_t>(id) >= scene.materials.size()) id = 0;

    if (!any_normals) build_normals(scene, position_refs, positions.size());

    for (const auto& tri : scene.positions)
        for (const Vec3& v : tri) scene.bounds.grow(v);
}

void apply_default_lighting(Scene& scene) {
    if (scene.has_emitters()) return;
    scene.sky.zenith = Vec3{0.12f, 0.22f, 0.50f};
    scene.sky.horizon = Vec3{0.42f, 0.50f, 0.66f};
    scene.sun.enabled = true;
}

fs::path resolve_relative(const fs::path& base, const std::string& value) {
    const fs::path candidate(value);
    if (candidate.is_absolute()) return candidate;
    return base.parent_path() / candidate;
}

Scene load_scene_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open scene: " + path.string());

    Scene scene;
    scene.name = path.stem().string();
    bool mesh_loaded = false;
    bool camera_set = false;
    bool lighting_specified = false;

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const auto tokens = tokenize(line);
        if (tokens.empty() || tokens[0][0] == '#') continue;
        const std::string key = lower(tokens[0]);

        auto require = [&](size_t count) {
            if (tokens.size() < count) {
                throw std::runtime_error(path.filename().string() + ":" +
                                         std::to_string(line_number) + ": '" + key +
                                         "' needs " + std::to_string(count - 1) + " values");
            }
        };

        if (key == "mesh") {
            require(2);
            load_obj_into(scene, resolve_relative(path, tokens[1]));
            mesh_loaded = true;
        } else if (key == "camera") {
            require(8);
            scene.camera.position = {std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])};
            scene.camera.target = {std::stof(tokens[4]), std::stof(tokens[5]), std::stof(tokens[6])};
            scene.camera.fov_degrees = std::stof(tokens[7]);
            camera_set = true;
        } else if (key == "resolution") {
            require(3);
            scene.settings.width = std::stoi(tokens[1]);
            scene.settings.height = std::stoi(tokens[2]);
        } else if (key == "samples") {
            require(2);
            scene.settings.samples = std::stoi(tokens[1]);
        } else if (key == "cycles") {
            require(2);
            scene.settings.cycles = std::max(0, std::stoi(tokens[1]));
        } else if (key == "bounces") {
            require(2);
            scene.settings.bounces = std::stoi(tokens[1]);
        } else if (key == "clamp") {
            require(2);
            scene.settings.radiance_clamp = std::max(0.0f, std::stof(tokens[1]));
        } else if (key == "exposure") {
            require(2);
            scene.settings.exposure = std::stof(tokens[1]);
        } else if (key == "environment") {
            require(4);
            const Vec3 constant{std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])};
            scene.sky.zenith = constant;
            scene.sky.horizon = constant;
            lighting_specified = true;
        } else if (key == "sky") {
            require(7);
            scene.sky.zenith = {std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])};
            scene.sky.horizon = {std::stof(tokens[4]), std::stof(tokens[5]), std::stof(tokens[6])};
            lighting_specified = true;
        } else if (key == "sun") {
            require(8);
            scene.sun.enabled = true;
            scene.sun.direction = normalize(
                Vec3{std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])});
            scene.sun.irradiance = {std::stof(tokens[4]), std::stof(tokens[5]), std::stof(tokens[6])};
            scene.sun.angular_radius_degrees = std::clamp(std::stof(tokens[7]), 0.05f, 60.0f);
            lighting_specified = true;
        } else {
            throw std::runtime_error(path.filename().string() + ":" + std::to_string(line_number) +
                                     ": unknown directive '" + tokens[0] + "'");
        }
    }

    if (!mesh_loaded) throw std::runtime_error("scene has no 'mesh' directive: " + path.string());
    if (!camera_set) frame_camera(scene);
    if (!lighting_specified) apply_default_lighting(scene);
    scene.sun.direction = normalize(scene.sun.direction);
    return scene;
}

}

void frame_camera(Scene& scene) {
    const Aabb& box = scene.bounds;
    if (!box.valid()) return;

    const Vec3 center = box.center();
    const float half_fov = scene.camera.fov_degrees * 3.14159265f / 360.0f;
    const float tan_v = std::tan(half_fov);
    const float aspect = static_cast<float>(scene.settings.width) /
                         std::max(1.0f, static_cast<float>(scene.settings.height));
    const float tan_h = tan_v * aspect;

    const Vec3 offset = normalize({0.5f, 0.34f, -1.0f});
    const Vec3 right = normalize(cross(-offset, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 up = cross(right, -offset);

    float distance = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? box.hi.x : box.lo.x, (i & 2) ? box.hi.y : box.lo.y,
                          (i & 4) ? box.hi.z : box.lo.z};
        const Vec3 d = corner - center;
        const float depth = dot(d, -offset);
        distance = std::max(distance, std::abs(dot(d, up)) / tan_v + depth);
        distance = std::max(distance, std::abs(dot(d, right)) / tan_h + depth);
    }
    distance = std::max(distance * 1.06f, 1e-3f);

    scene.camera.target = center;
    scene.camera.position = center + offset * distance;
    scene.camera_auto = true;

    const float clearance = box.lo.y + std::max(box.extent().y, 1e-3f) * 0.08f;
    if (scene.camera.position.y < clearance) scene.camera.position.y = clearance;
}

bool Scene::has_emitters() const {
    for (int id : material_ids) {
        if (id >= 0 && static_cast<size_t>(id) < materials.size() &&
            luminance(materials[static_cast<size_t>(id)].emission) > 0.0f) {
            return true;
        }
    }
    return false;
}

bool is_scene_input(const fs::path& path) {
    const std::string ext = lower(path.extension().string());
    return ext == ".obj" || ext == ".scene";
}

Scene load_scene(const fs::path& path) {
    if (lower(path.extension().string()) == ".scene") return load_scene_file(path);

    Scene scene;
    scene.name = path.stem().string();
    load_obj_into(scene, path);
    frame_camera(scene);
    apply_default_lighting(scene);
    scene.sun.direction = normalize(scene.sun.direction);
    return scene;
}

std::vector<fs::path> collect_inputs(const fs::path& root) {
    std::vector<fs::path> inputs;
    if (fs::is_regular_file(root)) {
        inputs.push_back(root);
        return inputs;
    }
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (is_scene_input(entry.path())) inputs.push_back(entry.path());
    }
    std::sort(inputs.begin(), inputs.end());

    std::vector<fs::path> filtered;
    for (const auto& candidate : inputs) {
        if (lower(candidate.extension().string()) != ".obj") {
            filtered.push_back(candidate);
            continue;
        }
        fs::path paired = candidate;
        paired.replace_extension(".scene");
        if (!fs::exists(paired)) filtered.push_back(candidate);
    }
    return filtered;
}

}
