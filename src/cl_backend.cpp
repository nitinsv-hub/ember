#include "cl_backend.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#if defined(_WIN32)
using LibHandle = HMODULE;
LibHandle open_icd() {
    for (const char* name : {"OpenCL.dll", "C:\\Windows\\System32\\OpenCL.dll"})
        if (LibHandle handle = ::LoadLibraryA(name)) return handle;
    return nullptr;
}
void* lookup(LibHandle handle, const char* name) {
    return reinterpret_cast<void*>(::GetProcAddress(handle, name));
}
const char* icd_hint() { return "install a GPU driver that ships OpenCL.dll"; }
#elif defined(__APPLE__)
using LibHandle = void*;
LibHandle open_icd() {
    for (const char* name : {"/System/Library/Frameworks/OpenCL.framework/OpenCL",
                             "/System/Library/Frameworks/OpenCL.framework/Versions/A/OpenCL",
                             "libOpenCL.so.1"})
        if (LibHandle handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL)) return handle;
    return nullptr;
}
void* lookup(LibHandle handle, const char* name) { return ::dlsym(handle, name); }
const char* icd_hint() { return "OpenCL.framework should be present on macOS 10.7 and later"; }
#else
using LibHandle = void*;
LibHandle open_icd() {
    for (const char* name : {"libOpenCL.so.1", "libOpenCL.so", "libOpenCL.so.1.0.0"})
        if (LibHandle handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL)) return handle;
    return nullptr;
}
void* lookup(LibHandle handle, const char* name) { return ::dlsym(handle, name); }
const char* icd_hint() { return "install ocl-icd-libopencl1 or a vendor runtime"; }
#endif

CLApi load_api() {
    LibHandle library = open_icd();
    if (!library)
        throw std::runtime_error(std::string("no OpenCL loader found: ") + icd_hint());

    CLApi api{};
    auto load = [&](auto& slot, const char* name) {
        void* address = lookup(library, name);
        if (!address) throw std::runtime_error(std::string("missing OpenCL entry point: ") + name);
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(address);
    };

    load(api.GetPlatformIDs, "clGetPlatformIDs");
    load(api.GetPlatformInfo, "clGetPlatformInfo");
    load(api.GetDeviceIDs, "clGetDeviceIDs");
    load(api.GetDeviceInfo, "clGetDeviceInfo");
    load(api.CreateContext, "clCreateContext");
    load(api.CreateCommandQueue, "clCreateCommandQueue");
    load(api.CreateProgramWithSource, "clCreateProgramWithSource");
    load(api.BuildProgram, "clBuildProgram");
    load(api.GetProgramBuildInfo, "clGetProgramBuildInfo");
    load(api.CreateKernel, "clCreateKernel");
    load(api.GetKernelWorkGroupInfo, "clGetKernelWorkGroupInfo");
    load(api.SetKernelArg, "clSetKernelArg");
    load(api.CreateBuffer, "clCreateBuffer");
    load(api.EnqueueWriteBuffer, "clEnqueueWriteBuffer");
    load(api.EnqueueReadBuffer, "clEnqueueReadBuffer");
    load(api.EnqueueNDRangeKernel, "clEnqueueNDRangeKernel");
    load(api.Finish, "clFinish");
    load(api.ReleaseMemObject, "clReleaseMemObject");
    load(api.ReleaseKernel, "clReleaseKernel");
    load(api.ReleaseProgram, "clReleaseProgram");
    load(api.ReleaseCommandQueue, "clReleaseCommandQueue");
    load(api.ReleaseContext, "clReleaseContext");
    return api;
}

std::string query_string(cl_int (CL_CALL* fn)(void*, cl_uint, size_t, void*, size_t*),
                         void* object, cl_uint param) {
    size_t bytes = 0;
    if (fn(object, param, 0, nullptr, &bytes) != CL_SUCCESS || bytes == 0) return {};
    std::string value(bytes, '\0');
    if (fn(object, param, bytes, value.data(), nullptr) != CL_SUCCESS) return {};
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) value.pop_back();
    return value;
}

template <typename T>
T query_number(cl_int (CL_CALL* fn)(void*, cl_uint, size_t, void*, size_t*),
               void* object, cl_uint param) {
    T value{};
    fn(object, param, sizeof(T), &value, nullptr);
    return value;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) words.push_back(word);
    std::sort(words.begin(), words.end());
    return words;
}

}

const CLApi& cl() {
    static const CLApi api = load_api();
    return api;
}

const char* cl_error_string(cl_int err) {
    switch (err) {
        case CL_SUCCESS: return "SUCCESS";
        case CL_DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
        case CL_DEVICE_NOT_AVAILABLE: return "DEVICE_NOT_AVAILABLE";
        case CL_COMPILER_NOT_AVAILABLE: return "COMPILER_NOT_AVAILABLE";
        case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "MEM_OBJECT_ALLOCATION_FAILURE";
        case CL_OUT_OF_RESOURCES: return "OUT_OF_RESOURCES";
        case CL_OUT_OF_HOST_MEMORY: return "OUT_OF_HOST_MEMORY";
        case CL_BUILD_PROGRAM_FAILURE: return "BUILD_PROGRAM_FAILURE";
        case CL_INVALID_VALUE: return "INVALID_VALUE";
        case CL_INVALID_DEVICE: return "INVALID_DEVICE";
        case CL_INVALID_CONTEXT: return "INVALID_CONTEXT";
        case CL_INVALID_COMMAND_QUEUE: return "INVALID_COMMAND_QUEUE";
        case CL_INVALID_MEM_OBJECT: return "INVALID_MEM_OBJECT";
        case CL_INVALID_BUILD_OPTIONS: return "INVALID_BUILD_OPTIONS";
        case CL_INVALID_PROGRAM_EXECUTABLE: return "INVALID_PROGRAM_EXECUTABLE";
        case CL_INVALID_KERNEL_NAME: return "INVALID_KERNEL_NAME";
        case CL_INVALID_KERNEL: return "INVALID_KERNEL";
        case CL_INVALID_ARG_INDEX: return "INVALID_ARG_INDEX";
        case CL_INVALID_ARG_VALUE: return "INVALID_ARG_VALUE";
        case CL_INVALID_ARG_SIZE: return "INVALID_ARG_SIZE";
        case CL_INVALID_KERNEL_ARGS: return "INVALID_KERNEL_ARGS";
        case CL_INVALID_WORK_GROUP_SIZE: return "INVALID_WORK_GROUP_SIZE";
        case CL_INVALID_WORK_ITEM_SIZE: return "INVALID_WORK_ITEM_SIZE";
        default: return "UNKNOWN";
    }
}

namespace ember {

void cl_check(cl_int err, const char* what) {
    if (err == CL_SUCCESS) return;
    std::ostringstream message;
    message << what << " failed: " << cl_error_string(err) << " (" << err << ")";
    throw std::runtime_error(message.str());
}

bool DeviceInfo::has(const std::string& extension) const {
    return std::binary_search(extensions.begin(), extensions.end(), extension);
}

std::string DeviceInfo::kind() const {
    if (is_gpu()) return "GPU";
    if (is_cpu()) return "CPU";
    return "other";
}

std::vector<DeviceInfo> enumerate_devices() {
    const CLApi& api = cl();
    std::vector<DeviceInfo> found;

    cl_uint platform_count = 0;
    if (api.GetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0)
        return found;

    std::vector<cl_platform_id> platforms(platform_count);
    api.GetPlatformIDs(platform_count, platforms.data(), nullptr);

    for (cl_platform_id platform : platforms) {
        cl_uint device_count = 0;
        if (api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &device_count) != CL_SUCCESS)
            continue;

        std::vector<cl_device_id> ids(device_count);
        api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, device_count, ids.data(), nullptr);

        for (cl_device_id id : ids) {
            DeviceInfo info;
            info.platform = platform;
            info.id = id;
            info.platform_name = query_string(api.GetPlatformInfo, platform, CL_PLATFORM_NAME);
            info.name = query_string(api.GetDeviceInfo, id, CL_DEVICE_NAME);
            info.vendor = query_string(api.GetDeviceInfo, id, CL_DEVICE_VENDOR);
            info.version = query_string(api.GetDeviceInfo, id, CL_DEVICE_VERSION);
            info.driver = query_string(api.GetDeviceInfo, id, CL_DRIVER_VERSION);
            info.extensions = split_words(query_string(api.GetDeviceInfo, id, CL_DEVICE_EXTENSIONS));
            info.type = query_number<cl_device_type>(api.GetDeviceInfo, id, CL_DEVICE_TYPE);
            info.compute_units = query_number<cl_uint>(api.GetDeviceInfo, id, CL_DEVICE_MAX_COMPUTE_UNITS);
            info.max_work_group = query_number<size_t>(api.GetDeviceInfo, id, CL_DEVICE_MAX_WORK_GROUP_SIZE);
            info.local_mem = query_number<cl_ulong>(api.GetDeviceInfo, id, CL_DEVICE_LOCAL_MEM_SIZE);
            info.global_mem = query_number<cl_ulong>(api.GetDeviceInfo, id, CL_DEVICE_GLOBAL_MEM_SIZE);
            info.max_alloc = query_number<cl_ulong>(api.GetDeviceInfo, id, CL_DEVICE_MAX_MEM_ALLOC_SIZE);
            found.push_back(std::move(info));
        }
    }
    return found;
}

const DeviceInfo& pick_device(const std::vector<DeviceInfo>& devices, int index) {
    if (devices.empty()) throw std::runtime_error("no OpenCL devices found");
    if (index >= 0) {
        if (static_cast<size_t>(index) >= devices.size())
            throw std::runtime_error("device index out of range, try --list");
        return devices[static_cast<size_t>(index)];
    }
    for (const DeviceInfo& device : devices)
        if (device.is_gpu()) return device;
    return devices.front();
}

Context::Context(const DeviceInfo& device) : device_(device) {
    const CLApi& api = cl();
    cl_int err = CL_SUCCESS;
    cl_device_id id = device_.id;

    ctx_ = api.CreateContext(nullptr, 1, &id, nullptr, nullptr, &err);
    cl_check(err, "clCreateContext");

    queue_ = api.CreateCommandQueue(ctx_, id, 0, &err);
    cl_check(err, "clCreateCommandQueue");
}

Context::~Context() {
    const CLApi& api = cl();
    if (queue_) api.ReleaseCommandQueue(queue_);
    if (ctx_) api.ReleaseContext(ctx_);
}

cl_program Context::build(const std::string& source, const std::string& options) {
    const CLApi& api = cl();
    cl_int err = CL_SUCCESS;

    const char* text = source.c_str();
    const size_t length = source.size();
    cl_program program = api.CreateProgramWithSource(ctx_, 1, &text, &length, &err);
    cl_check(err, "clCreateProgramWithSource");

    cl_device_id id = device_.id;
    const std::string attempts[] = {options, std::string()};
    cl_int status = CL_BUILD_PROGRAM_FAILURE;
    std::string log;

    for (const std::string& attempt : attempts) {
        status = api.BuildProgram(program, 1, &id, attempt.c_str(), nullptr, nullptr);

        size_t log_size = 0;
        api.GetProgramBuildInfo(program, id, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        log.assign(log_size, '\0');
        if (log_size)
            api.GetProgramBuildInfo(program, id, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        while (!log.empty() && (log.back() == '\0' || log.back() == '\n')) log.pop_back();

        if (status == CL_SUCCESS) {
            build_options_used_ = attempt.empty() ? "(none)" : attempt;
            break;
        }
        if (status != CL_INVALID_BUILD_OPTIONS && !attempt.empty()) break;
        if (attempt.empty()) break;
    }

    if (status != CL_SUCCESS) {
        api.ReleaseProgram(program);
        std::ostringstream message;
        message << "kernel build failed: " << cl_error_string(status) << "\n"
                << (log.empty() ? "driver returned no build log" : log);
        throw std::runtime_error(message.str());
    }
    if (!log.empty()) std::fprintf(stderr, "%s\n", log.c_str());
    return program;
}

cl_kernel Context::kernel(cl_program program, const char* name) {
    cl_int err = CL_SUCCESS;
    cl_kernel k = cl().CreateKernel(program, name, &err);
    cl_check(err, "clCreateKernel");
    return k;
}

size_t Context::kernel_max_work_group(cl_kernel k) const {
    size_t value = 0;
    cl().GetKernelWorkGroupInfo(k, device_.id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(value), &value, nullptr);
    return value;
}

size_t Context::kernel_preferred_multiple(cl_kernel k) const {
    size_t value = 0;
    cl().GetKernelWorkGroupInfo(k, device_.id, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
                                sizeof(value), &value, nullptr);
    return value;
}

cl_mem Context::create_buffer(size_t bytes, cl_mem_flags flags, const void* host_data) {
    cl_int err = CL_SUCCESS;
    void* pointer = nullptr;
    if (host_data) {
        flags |= CL_MEM_COPY_HOST_PTR;
        pointer = const_cast<void*>(host_data);
    }
    cl_mem buffer = cl().CreateBuffer(ctx_, flags, bytes, pointer, &err);
    cl_check(err, "clCreateBuffer");
    return buffer;
}

void Context::read(cl_mem buffer, void* destination, size_t bytes) {
    cl_check(cl().EnqueueReadBuffer(queue_, buffer, 1, 0, bytes, destination, 0, nullptr, nullptr),
             "clEnqueueReadBuffer");
}

void Context::write(cl_mem buffer, const void* source, size_t bytes) {
    cl_check(cl().EnqueueWriteBuffer(queue_, buffer, 1, 0, bytes, source, 0, nullptr, nullptr),
             "clEnqueueWriteBuffer");
}

void Context::release(cl_mem buffer) {
    if (buffer) cl().ReleaseMemObject(buffer);
}

void Context::set_arg(cl_kernel k, cl_uint index, size_t size, const void* value) {
    cl_check(cl().SetKernelArg(k, index, size, value), "clSetKernelArg");
}

void Context::run2d(cl_kernel k, size_t gx, size_t gy, size_t lx, size_t ly) {
    const size_t global[2] = {gx, gy};
    const size_t local[2] = {lx, ly};
    cl_check(cl().EnqueueNDRangeKernel(queue_, k, 2, nullptr, global, (lx && ly) ? local : nullptr,
                                       0, nullptr, nullptr),
             "clEnqueueNDRangeKernel");
}

void Context::finish() { cl_check(cl().Finish(queue_), "clFinish"); }

}
