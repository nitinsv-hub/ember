#include "cl_backend.hpp"
#include "image.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "vk_interop.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ember;

namespace {

struct Options {
    fs::path input;
    int device = -1;
    int width = 0;
    int height = 0;
    int samples = 0;
    int bounces = 0;
    float exposure = -1.0f;
    float clamp_value = -1.0f;
    bool list = false;
    bool no_vulkan = false;
    bool write_hdr = false;
    fs::path compare_a, compare_b;
};

void print_usage() {
    std::printf(
        "ember - OpenCL path tracer with Vulkan interop\n"
        "\n"
        "  ember <file>              render one scene next to the input as <name>_output.png\n"
        "  ember <directory>         render every scene into <directory>/output/<name>.png\n"
        "\n"
        "  --list                    show OpenCL and Vulkan devices\n"
        "  --device N                OpenCL device index\n"
        "  --width N  --height N     override resolution\n"
        "  --samples N               override samples per pixel\n"
        "  --bounces N               override maximum path length\n"
        "  --exposure F              override tonemap exposure\n"
        "  --clamp F                 clamp per-sample radiance, suppresses fireflies\n"
        "  --hdr                     also write a .pfm alongside the png\n"
        "  --no-vulkan               skip Vulkan entirely, stay on staged copies\n"
        "  --compare A.pfm B.pfm     diff two hdr renders\n"
        "\n"
        "Accepted inputs: .obj meshes and .scene description files.\n");
}

int to_int(const char* text, const char* flag) {
    try {
        return std::stoi(text);
    } catch (...) {
        throw std::runtime_error(std::string("expected a number after ") + flag);
    }
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) throw std::runtime_error(std::string("missing value after ") + flag);
            return argv[++i];
        };

        if (argument == "--list") options.list = true;
        else if (argument == "--hdr") options.write_hdr = true;
        else if (argument == "--no-vulkan") options.no_vulkan = true;
        else if (argument == "--device") options.device = to_int(value("--device"), "--device");
        else if (argument == "--width") options.width = to_int(value("--width"), "--width");
        else if (argument == "--height") options.height = to_int(value("--height"), "--height");
        else if (argument == "--samples") options.samples = to_int(value("--samples"), "--samples");
        else if (argument == "--bounces") options.bounces = to_int(value("--bounces"), "--bounces");
        else if (argument == "--exposure") options.exposure = std::stof(value("--exposure"));
        else if (argument == "--clamp") options.clamp_value = std::stof(value("--clamp"));
        else if (argument == "--compare") {
            options.compare_a = value("--compare");
            options.compare_b = value("--compare");
        } else if (argument == "-h" || argument == "--help") {
            print_usage();
            std::exit(0);
        } else if (argument.rfind("--", 0) == 0) {
            throw std::runtime_error("unknown option: " + argument);
        } else if (options.input.empty()) {
            options.input = argument;
        } else {
            throw std::runtime_error("more than one input path given");
        }
    }
    return options;
}

void list_devices(bool probe_vulkan) {
    const auto devices = enumerate_devices();
    if (devices.empty()) {
        std::printf("no OpenCL devices found\n");
    }
    for (size_t i = 0; i < devices.size(); ++i) {
        const DeviceInfo& device = devices[i];
        std::printf("opencl [%zu] %s %s\n", i, device.kind().c_str(), device.name.c_str());
        std::printf("            %s, %s, driver %s\n", device.platform_name.c_str(),
                    device.version.c_str(), device.driver.c_str());
        std::printf("            %u units, %.1f GB global, %.1f GB max alloc, max workgroup %zu\n",
                    device.compute_units,
                    static_cast<double>(device.global_mem) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(device.max_alloc) / (1024.0 * 1024.0 * 1024.0),
                    device.max_work_group);

        std::string interop;
        for (const std::string& extension : device.extensions) {
            if (extension.find("external") == std::string::npos &&
                extension.find("semaphore") == std::string::npos)
                continue;
            if (!interop.empty()) interop += ", ";
            interop += extension;
        }
        std::printf("            interop: %s\n", interop.empty() ? "none" : interop.c_str());
    }

    if (!probe_vulkan) return;

    VulkanContext vulkan;
    const VulkanCapabilities& caps = vulkan.capabilities();
    std::printf("\n");
    if (!caps.loaded) {
        std::printf("vulkan      unavailable (%s)\n", caps.unavailable_reason.c_str());
        return;
    }
    std::printf("vulkan      %s, api %s", caps.device_name.c_str(), caps.api_version.c_str());
    if (!caps.driver_id.empty()) std::printf(", %s", caps.driver_id.c_str());
    std::printf("\n");
    std::printf("            external memory host  %s\n", caps.external_memory_host ? "yes" : "no");
    std::printf("            external memory handle %s\n", caps.external_memory_handle ? "yes" : "no");
    std::printf("            external semaphore    %s\n", caps.external_semaphore ? "yes" : "no");
    std::printf("            timeline semaphore    %s\n", caps.timeline_semaphore ? "yes" : "no");
    std::printf("            ray query             %s\n", caps.ray_query ? "yes" : "no");
    std::printf("            acceleration structure %s\n", caps.acceleration_structure ? "yes" : "no");
    if (caps.min_imported_host_pointer_alignment)
        std::printf("            host pointer alignment %zu bytes\n",
                    caps.min_imported_host_pointer_alignment);
}

int compare_images(const fs::path& a, const fs::path& b) {
    const ImageF left = read_pfm(a.string());
    const ImageF right = read_pfm(b.string());
    if (left.width != right.width || left.height != right.height) {
        std::printf("size mismatch: %dx%d against %dx%d\n", left.width, left.height, right.width,
                    right.height);
        return 1;
    }

    double largest = 0.0;
    double squared = 0.0;
    bool identical = true;
    for (size_t i = 0; i < left.rgb.size(); ++i) {
        const double difference = static_cast<double>(left.rgb[i]) - static_cast<double>(right.rgb[i]);
        if (left.rgb[i] != right.rgb[i]) identical = false;
        largest = std::max(largest, std::abs(difference));
        squared += difference * difference;
    }
    std::printf("max difference %.6g\n", largest);
    std::printf("rmse           %.6g\n", std::sqrt(squared / static_cast<double>(left.rgb.size())));
    std::printf("identical      %s\n", identical ? "yes" : "no");
    return identical ? 0 : 2;
}

fs::path find_kernel() {
    std::vector<fs::path> roots = {fs::current_path()};
    fs::path walk = fs::current_path();
    for (int i = 0; i < 5 && walk.has_parent_path() && walk.parent_path() != walk; ++i) {
        walk = walk.parent_path();
        roots.push_back(walk);
    }
    for (const fs::path& root : roots) {
        const fs::path candidate = root / "kernels" / "trace.cl";
        if (fs::exists(candidate)) return candidate;
    }
    throw std::runtime_error("cannot find kernels/trace.cl relative to the working directory");
}

void apply_overrides(Scene& scene, const Options& options) {
    if (options.width > 0) scene.settings.width = options.width;
    if (options.height > 0) scene.settings.height = options.height;
    if (options.samples > 0) scene.settings.samples = options.samples;
    if (options.bounces > 0) scene.settings.bounces = options.bounces;
    if (options.exposure >= 0.0f) scene.settings.exposure = options.exposure;
    if (options.clamp_value >= 0.0f) scene.settings.radiance_clamp = options.clamp_value;
    scene.settings.batch = std::max(1, std::min(scene.settings.batch, scene.settings.samples));
}

fs::path output_path_for(const fs::path& input, const fs::path& root, bool directory_mode) {
    if (!directory_mode) {
        fs::path result = input;
        result.replace_filename(input.stem().string() + "_output.png");
        return result;
    }
    const fs::path folder = root / "output";
    fs::create_directories(folder);
    return folder / (input.stem().string() + ".png");
}

int run(const Options& options) {
    if (options.input.empty()) {
        print_usage();
        return 1;
    }
    if (!fs::exists(options.input))
        throw std::runtime_error("no such file or directory: " + options.input.string());

    const bool directory_mode = fs::is_directory(options.input);
    const auto inputs = collect_inputs(options.input);
    if (inputs.empty())
        throw std::runtime_error("no .obj or .scene files found in " + options.input.string());

    const auto devices = enumerate_devices();
    const DeviceInfo& device = pick_device(devices, options.device);
    Renderer renderer(device, find_kernel(), !options.no_vulkan);

    std::printf("opencl   %s (%s)\n", device.name.c_str(), device.version.c_str());
    const VulkanCapabilities& caps = renderer.vulkan();
    if (caps.loaded)
        std::printf("vulkan   %s (api %s)\n", caps.device_name.c_str(), caps.api_version.c_str());
    else if (!options.no_vulkan)
        std::printf("vulkan   unavailable (%s)\n", caps.unavailable_reason.c_str());
    std::printf("interop  tier %s\n", tier_name(renderer.tier()));
    std::printf("inputs   %zu\n\n", inputs.size());

    int failures = 0;
    for (const fs::path& input : inputs) {
        try {
            Scene scene = load_scene(input);
            apply_overrides(scene, options);

            RenderStats stats;
            const ImageF image = renderer.render(scene, stats);

            const fs::path output = output_path_for(input, options.input, directory_mode);
            write_png(output.string(), tonemap_aces(image, scene.settings.exposure), image.width,
                      image.height);
            if (options.write_hdr) {
                fs::path hdr = output;
                hdr.replace_extension(".pfm");
                write_pfm(hdr.string(), image);
            }

            const double rays_per_second =
                static_cast<double>(image.width) * image.height * scene.settings.samples /
                std::max(1e-6, stats.trace_seconds);

            std::printf("%s\n", input.filename().string().c_str());
            std::printf("  %zu triangles, %zu bvh nodes, depth %d, built in %.0f ms\n",
                        stats.triangles, stats.bvh_nodes, stats.bvh_depth, stats.bvh_seconds * 1000.0);
            std::printf("  %d emitters, sun %s, sky %.2f %.2f %.2f\n", stats.emitters,
                        scene.sun.enabled ? "on" : "off", scene.sky.horizon.x, scene.sky.horizon.y,
                        scene.sky.horizon.z);
            std::printf("  %dx%d at %d spp, %d bounces\n", image.width, image.height,
                        scene.settings.samples, scene.settings.bounces);
            std::printf("  traced in %.2f s, %.1f Mpaths/s\n", stats.trace_seconds,
                        rays_per_second / 1e6);
            std::printf("  radiance min %.3f mean %.3f max %.1f\n", stats.min_radiance,
                        stats.mean_radiance, stats.max_radiance);
            if (stats.vulkan_resolve) std::printf("  vulkan resolve pass on shared memory ok\n");
            if (stats.non_finite_pixels)
                std::printf("  warning: %zu pixels contain non-finite radiance\n",
                            stats.non_finite_pixels);
            if (stats.max_radiance <= 0.0f)
                std::printf("  warning: image is entirely black\n");
            std::printf("  wrote %s\n\n", output.string().c_str());
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s: %s\n\n", input.filename().string().c_str(), error.what());
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

}

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);
        if (options.list) {
            list_devices(!options.no_vulkan);
            return 0;
        }
        if (!options.compare_a.empty()) return compare_images(options.compare_a, options.compare_b);
        return run(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
