#include "renderer.hpp"

#include "bvh.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ember {
namespace {

constexpr const char* kBuildOptions = "-cl-std=CL1.2";

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open kernel: " + path.string());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

struct DeviceScene {
    std::vector<Float4> vertices;
    std::vector<Float4> normals;
    std::vector<std::int32_t> triangle_materials;
    std::vector<std::int32_t> triangle_order;
    std::vector<Float4> materials;
    std::vector<std::int32_t> light_triangles;
    std::vector<float> light_cdf;
    float light_area = 0.0f;
};

DeviceScene flatten(const Scene& scene, const Bvh& bvh) {
    DeviceScene out;
    const size_t count = scene.triangle_count();

    out.vertices.resize(count * 3);
    out.normals.resize(count * 3);
    out.triangle_materials.resize(count);

    for (size_t i = 0; i < count; ++i) {
        for (int k = 0; k < 3; ++k) {
            out.vertices[i * 3 + static_cast<size_t>(k)] = to_float4(scene.positions[i][static_cast<size_t>(k)]);
            out.normals[i * 3 + static_cast<size_t>(k)] = to_float4(scene.normals[i][static_cast<size_t>(k)]);
        }
        out.triangle_materials[i] = scene.material_ids[i];
    }

    out.triangle_order = bvh.triangle_indices;

    out.materials.resize(scene.materials.size() * 3);
    for (size_t i = 0; i < scene.materials.size(); ++i) {
        const Material& m = scene.materials[i];
        out.materials[i * 3 + 0] = to_float4(m.albedo, int_bits_to_float(static_cast<std::int32_t>(m.bsdf)));
        out.materials[i * 3 + 1] = to_float4(m.emission, std::clamp(m.roughness, 0.001f, 1.0f));
        out.materials[i * 3 + 2] = Float4{m.ior, 0.0f, 0.0f, 0.0f};
    }

    std::vector<float> areas;
    for (size_t i = 0; i < count; ++i) {
        const int id = scene.material_ids[i];
        if (id < 0 || static_cast<size_t>(id) >= scene.materials.size()) continue;
        if (luminance(scene.materials[static_cast<size_t>(id)].emission) <= 0.0f) continue;
        const auto& tri = scene.positions[i];
        const float area = 0.5f * length(cross(tri[1] - tri[0], tri[2] - tri[0]));
        if (area <= 0.0f) continue;
        out.light_triangles.push_back(static_cast<std::int32_t>(i));
        areas.push_back(area);
        out.light_area += area;
    }

    out.light_cdf.resize(areas.size());
    float running = 0.0f;
    for (size_t i = 0; i < areas.size(); ++i) {
        running += areas[i];
        out.light_cdf[i] = out.light_area > 0.0f ? running / out.light_area : 1.0f;
    }
    if (!out.light_cdf.empty()) out.light_cdf.back() = 1.0f;

    if (out.light_triangles.empty()) {
        out.light_triangles.push_back(0);
        out.light_cdf.push_back(1.0f);
    }
    return out;
}

}

Renderer::Renderer(const DeviceInfo& device, const fs::path& kernel_path, bool enable_vulkan)
    : context_(device) {
    program_ = context_.build(read_text(kernel_path), kBuildOptions);
    kernel_ = context_.kernel(program_, "trace");

    const size_t budget = context_.kernel_max_work_group(kernel_)
                              ? context_.kernel_max_work_group(kernel_)
                              : 64;
    local_x_ = 8;
    local_y_ = 8;
    while (local_x_ * local_y_ > budget && local_y_ > 1) local_y_ /= 2;
    while (local_x_ * local_y_ > budget && local_x_ > 1) local_x_ /= 2;

    if (enable_vulkan) {
        vulkan_ = std::make_unique<VulkanContext>();
        const VulkanCapabilities& caps = vulkan_->capabilities();
        if (!caps.loaded) {
            tier_ = InteropTier::StagedCopy;
        } else if (device.has("cl_khr_external_memory_win32") && caps.external_memory_handle &&
                   device.has("cl_khr_semaphore")) {
            tier_ = InteropTier::ExternalHandle;
        } else if (caps.external_memory_host) {
            tier_ = InteropTier::SharedHostMemory;
        } else {
            tier_ = InteropTier::StagedCopy;
        }
    }
}

Renderer::~Renderer() {
    if (kernel_) cl().ReleaseKernel(kernel_);
    if (program_) cl().ReleaseProgram(program_);
}

const VulkanCapabilities& Renderer::vulkan() const {
    static const VulkanCapabilities empty;
    return vulkan_ ? vulkan_->capabilities() : empty;
}

ImageF Renderer::render(const Scene& scene, RenderStats& stats) {
    if (scene.triangle_count() == 0) throw std::runtime_error("scene has no geometry");

    const Bvh bvh = build_bvh(scene);
    const DeviceScene data = flatten(scene, bvh);

    stats.bvh_seconds = bvh.build_seconds;
    stats.triangles = scene.triangle_count();
    stats.bvh_nodes = bvh.nodes.size();
    stats.bvh_depth = bvh.max_depth;
    stats.emitters = static_cast<int>(data.light_area > 0.0f ? data.light_triangles.size() : 0);
    stats.tier = tier_;

    const RenderSettings& settings = scene.settings;
    const size_t pixels = static_cast<size_t>(settings.width) * static_cast<size_t>(settings.height);
    const size_t accumulator_bytes = pixels * sizeof(Float4);

    HostAllocation host{};
    bool shared_host = false;
    if (tier_ == InteropTier::SharedHostMemory && vulkan_) {
        const size_t alignment = std::max<size_t>(vulkan().min_imported_host_pointer_alignment, 4096);
        host = allocate_aligned(accumulator_bytes, alignment);
        if (host.pointer && vulkan_->import_host_buffer(host.pointer, host.bytes)) {
            shared_host = true;
        } else {
            free_aligned(host);
            tier_ = InteropTier::StagedCopy;
            stats.tier = tier_;
        }
    }

    std::vector<Float4> staging;
    Float4* accumulator = nullptr;
    if (shared_host) {
        accumulator = static_cast<Float4*>(host.pointer);
    } else {
        staging.resize(pixels);
        accumulator = staging.data();
    }

    cl_mem buffer_accumulator =
        context_.create_buffer(accumulator_bytes, CL_MEM_READ_WRITE, accumulator);
    cl_mem buffer_nodes = context_.create_buffer(bvh.nodes.size() * sizeof(BvhNode),
                                                 CL_MEM_READ_ONLY, bvh.nodes.data());
    cl_mem buffer_vertices = context_.create_buffer(data.vertices.size() * sizeof(Float4),
                                                    CL_MEM_READ_ONLY, data.vertices.data());
    cl_mem buffer_normals = context_.create_buffer(data.normals.size() * sizeof(Float4),
                                                   CL_MEM_READ_ONLY, data.normals.data());
    cl_mem buffer_tri_materials =
        context_.create_buffer(data.triangle_materials.size() * sizeof(std::int32_t),
                               CL_MEM_READ_ONLY, data.triangle_materials.data());
    cl_mem buffer_order = context_.create_buffer(data.triangle_order.size() * sizeof(std::int32_t),
                                                 CL_MEM_READ_ONLY, data.triangle_order.data());
    cl_mem buffer_materials = context_.create_buffer(data.materials.size() * sizeof(Float4),
                                                     CL_MEM_READ_ONLY, data.materials.data());
    cl_mem buffer_lights = context_.create_buffer(data.light_triangles.size() * sizeof(std::int32_t),
                                                  CL_MEM_READ_ONLY, data.light_triangles.data());
    cl_mem buffer_cdf = context_.create_buffer(data.light_cdf.size() * sizeof(float),
                                               CL_MEM_READ_ONLY, data.light_cdf.data());

    const Vec3 forward = normalize(scene.camera.target - scene.camera.position);
    const Vec3 right = normalize(cross(forward, scene.camera.up));
    const Vec3 up = cross(right, forward);
    const float tan_half_fov = std::tan(scene.camera.fov_degrees * 3.14159265f / 360.0f);

    const size_t gx = ((static_cast<size_t>(settings.width) + local_x_ - 1) / local_x_) * local_x_;
    const size_t gy = ((static_cast<size_t>(settings.height) + local_y_ - 1) / local_y_) * local_y_;

    const float sun_cos_max =
        std::cos(std::clamp(scene.sun.angular_radius_degrees, 0.05f, 60.0f) * 3.14159265f / 180.0f);
    const float sun_solid_angle = std::max(2.0f * 3.14159265f * (1.0f - sun_cos_max), 1e-9f);
    const Vec3 sun_radiance = scene.sun.irradiance / sun_solid_angle;

    const std::int32_t light_count =
        data.light_area > 0.0f ? static_cast<std::int32_t>(data.light_triangles.size()) : 0;

    const auto start = std::chrono::steady_clock::now();
    int remaining = settings.samples;
    int frame = 0;
    while (remaining > 0) {
        const std::int32_t batch = std::min(settings.batch, remaining);
        cl_uint slot = 0;
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_accumulator);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_nodes);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_vertices);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_normals);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_tri_materials);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_order);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_materials);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_lights);
        context_.set_arg(kernel_, slot++, sizeof(cl_mem), &buffer_cdf);
        context_.set_arg(kernel_, slot++, light_count);
        context_.set_arg(kernel_, slot++, data.light_area);
        context_.set_arg(kernel_, slot++, to_float4(scene.sky.zenith));
        context_.set_arg(kernel_, slot++, to_float4(scene.sky.horizon));
        context_.set_arg(kernel_, slot++, to_float4(scene.sun.direction, sun_cos_max));
        context_.set_arg(kernel_, slot++, to_float4(sun_radiance, scene.sun.enabled ? 1.0f : 0.0f));
        context_.set_arg(kernel_, slot++, static_cast<std::int32_t>(settings.width));
        context_.set_arg(kernel_, slot++, static_cast<std::int32_t>(settings.height));
        context_.set_arg(kernel_, slot++, static_cast<std::int32_t>(frame));
        context_.set_arg(kernel_, slot++, batch);
        context_.set_arg(kernel_, slot++, static_cast<std::int32_t>(settings.bounces));
        context_.set_arg(kernel_, slot++, to_float4(scene.camera.position));
        context_.set_arg(kernel_, slot++, to_float4(forward));
        context_.set_arg(kernel_, slot++, to_float4(right));
        context_.set_arg(kernel_, slot++, to_float4(up));
        context_.set_arg(kernel_, slot++, tan_half_fov);
        context_.set_arg(kernel_, slot++, settings.radiance_clamp);

        context_.run2d(kernel_, gx, gy, local_x_, local_y_);
        context_.finish();

        remaining -= batch;
        ++frame;
    }
    stats.trace_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    stats.cycles = frame;
    stats.samples_per_cycle = settings.batch;

    context_.read(buffer_accumulator, accumulator, accumulator_bytes);

    if (shared_host && vulkan_) stats.vulkan_resolve = vulkan_->run_resolve_pass(accumulator_bytes);

    ImageF image;
    image.width = settings.width;
    image.height = settings.height;
    image.rgb.resize(pixels * 3);

    float lowest = 3.4e38f;
    float highest = -3.4e38f;
    double sum = 0.0;
    for (size_t i = 0; i < pixels; ++i) {
        const float count = std::max(1.0f, accumulator[i].w);
        const float channels[3] = {accumulator[i].x / count, accumulator[i].y / count,
                                   accumulator[i].z / count};
        bool bad = false;
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(channels[k])) bad = true;
            image.rgb[i * 3 + static_cast<size_t>(k)] = channels[k];
            lowest = std::min(lowest, channels[k]);
            highest = std::max(highest, channels[k]);
            sum += channels[k];
        }
        if (bad) ++stats.non_finite_pixels;
    }
    stats.min_radiance = lowest;
    stats.max_radiance = highest;
    stats.mean_radiance = sum / static_cast<double>(pixels * 3);

    context_.release(buffer_accumulator);
    context_.release(buffer_nodes);
    context_.release(buffer_vertices);
    context_.release(buffer_normals);
    context_.release(buffer_tri_materials);
    context_.release(buffer_order);
    context_.release(buffer_materials);
    context_.release(buffer_lights);
    context_.release(buffer_cdf);

    if (shared_host && vulkan_) {
        vulkan_->release_imported();
        free_aligned(host);
    }
    return image;
}

}
