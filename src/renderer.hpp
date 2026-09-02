#pragma once

#include "cl_backend.hpp"
#include "image.hpp"
#include "scene.hpp"
#include "vk_interop.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace ember {

struct RenderStats {
    double bvh_seconds = 0.0;
    double trace_seconds = 0.0;
    size_t triangles = 0;
    size_t bvh_nodes = 0;
    int bvh_depth = 0;
    int emitters = 0;
    InteropTier tier = InteropTier::Unavailable;
    bool vulkan_resolve = false;
    size_t non_finite_pixels = 0;
    float min_radiance = 0.0f;
    float max_radiance = 0.0f;
    double mean_radiance = 0.0;
};

class Renderer {
public:
    Renderer(const DeviceInfo& device, const std::filesystem::path& kernel_path, bool enable_vulkan);
    ~Renderer();

    ImageF render(const Scene& scene, RenderStats& stats);

    const VulkanCapabilities& vulkan() const;
    InteropTier tier() const { return tier_; }
    const DeviceInfo& device() const { return context_.device(); }

private:
    Context context_;
    cl_program program_ = nullptr;
    cl_kernel kernel_ = nullptr;
    std::unique_ptr<VulkanContext> vulkan_;
    InteropTier tier_ = InteropTier::StagedCopy;
    size_t local_x_ = 8;
    size_t local_y_ = 8;
};

}
