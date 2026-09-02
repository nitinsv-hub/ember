#pragma once

#include "cl_min.hpp"
#include "vecmath.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ember {

struct DeviceInfo {
    cl_platform_id platform = nullptr;
    cl_device_id id = nullptr;

    std::string name, vendor, version, driver, platform_name;
    std::vector<std::string> extensions;

    cl_device_type type = 0;
    cl_uint compute_units = 0;
    size_t max_work_group = 0;
    cl_ulong local_mem = 0, global_mem = 0, max_alloc = 0;

    bool is_gpu() const { return (type & CL_DEVICE_TYPE_GPU) != 0; }
    bool is_cpu() const { return (type & CL_DEVICE_TYPE_CPU) != 0; }
    bool has(const std::string& extension) const;
    std::string kind() const;
};

std::vector<DeviceInfo> enumerate_devices();
const DeviceInfo& pick_device(const std::vector<DeviceInfo>& devices, int index);

class Context {
public:
    explicit Context(const DeviceInfo& device);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    cl_program build(const std::string& source, const std::string& options);
    cl_kernel kernel(cl_program program, const char* name);

    size_t kernel_max_work_group(cl_kernel k) const;
    size_t kernel_preferred_multiple(cl_kernel k) const;

    cl_mem create_buffer(size_t bytes, cl_mem_flags flags, const void* host_data = nullptr);
    void read(cl_mem buffer, void* destination, size_t bytes);
    void write(cl_mem buffer, const void* source, size_t bytes);
    void release(cl_mem buffer);

    void set_arg(cl_kernel k, cl_uint index, size_t size, const void* value);

    template <typename T>
    void set_arg(cl_kernel k, cl_uint index, const T& value) {
        set_arg(k, index, sizeof(T), &value);
    }

    void run2d(cl_kernel k, size_t gx, size_t gy, size_t lx, size_t ly);
    void finish();

    const DeviceInfo& device() const { return device_; }
    const std::string& build_options_used() const { return build_options_used_; }

private:
    DeviceInfo device_;
    cl_context ctx_ = nullptr;
    cl_command_queue queue_ = nullptr;
    std::string build_options_used_;
};

void cl_check(cl_int err, const char* what);

}
