#pragma once

#include <cstdint>

#if defined(_WIN32)
#define CL_CALL __stdcall
#else
#define CL_CALL
#endif

using cl_int = std::int32_t;
using cl_uint = std::uint32_t;
using cl_ulong = std::uint64_t;
using cl_bitfield = std::uint64_t;
using cl_bool = cl_uint;

using cl_platform_id = void*;
using cl_device_id = void*;
using cl_context = void*;
using cl_command_queue = void*;
using cl_mem = void*;
using cl_program = void*;
using cl_kernel = void*;
using cl_event = void*;

using cl_device_type = cl_bitfield;
using cl_mem_flags = cl_bitfield;
using cl_command_queue_properties = cl_bitfield;
using cl_context_properties = intptr_t;

enum : cl_int {
    CL_SUCCESS = 0,
    CL_DEVICE_NOT_FOUND = -1,
    CL_DEVICE_NOT_AVAILABLE = -2,
    CL_COMPILER_NOT_AVAILABLE = -3,
    CL_MEM_OBJECT_ALLOCATION_FAILURE = -4,
    CL_OUT_OF_RESOURCES = -5,
    CL_OUT_OF_HOST_MEMORY = -6,
    CL_BUILD_PROGRAM_FAILURE = -11,
    CL_INVALID_VALUE = -30,
    CL_INVALID_DEVICE = -33,
    CL_INVALID_CONTEXT = -34,
    CL_INVALID_COMMAND_QUEUE = -36,
    CL_INVALID_MEM_OBJECT = -38,
    CL_INVALID_BUILD_OPTIONS = -43,
    CL_INVALID_PROGRAM_EXECUTABLE = -45,
    CL_INVALID_KERNEL_NAME = -46,
    CL_INVALID_KERNEL = -48,
    CL_INVALID_ARG_INDEX = -49,
    CL_INVALID_ARG_VALUE = -50,
    CL_INVALID_ARG_SIZE = -51,
    CL_INVALID_KERNEL_ARGS = -52,
    CL_INVALID_WORK_GROUP_SIZE = -54,
    CL_INVALID_WORK_ITEM_SIZE = -55,
};

enum : cl_uint {
    CL_PLATFORM_VERSION = 0x0901,
    CL_PLATFORM_NAME = 0x0902,
    CL_DEVICE_TYPE = 0x1000,
    CL_DEVICE_MAX_COMPUTE_UNITS = 0x1002,
    CL_DEVICE_MAX_WORK_GROUP_SIZE = 0x1004,
    CL_DEVICE_MAX_MEM_ALLOC_SIZE = 0x1010,
    CL_DEVICE_GLOBAL_MEM_SIZE = 0x101F,
    CL_DEVICE_LOCAL_MEM_SIZE = 0x1023,
    CL_DEVICE_NAME = 0x102B,
    CL_DEVICE_VENDOR = 0x102C,
    CL_DRIVER_VERSION = 0x102D,
    CL_DEVICE_VERSION = 0x102F,
    CL_DEVICE_EXTENSIONS = 0x1030,
    CL_PROGRAM_BUILD_LOG = 0x1183,
    CL_KERNEL_WORK_GROUP_SIZE = 0x11B0,
    CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE = 0x11B3,
};

enum : cl_device_type {
    CL_DEVICE_TYPE_CPU = 1 << 1,
    CL_DEVICE_TYPE_GPU = 1 << 2,
    CL_DEVICE_TYPE_ACCELERATOR = 1 << 3,
    CL_DEVICE_TYPE_ALL = 0xFFFFFFFF,
};

enum : cl_mem_flags {
    CL_MEM_READ_WRITE = 1 << 0,
    CL_MEM_WRITE_ONLY = 1 << 1,
    CL_MEM_READ_ONLY = 1 << 2,
    CL_MEM_USE_HOST_PTR = 1 << 3,
    CL_MEM_ALLOC_HOST_PTR = 1 << 4,
    CL_MEM_COPY_HOST_PTR = 1 << 5,
};

struct CLApi {
    cl_int(CL_CALL* GetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
    cl_int(CL_CALL* GetPlatformInfo)(cl_platform_id, cl_uint, size_t, void*, size_t*);
    cl_int(CL_CALL* GetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
    cl_int(CL_CALL* GetDeviceInfo)(cl_device_id, cl_uint, size_t, void*, size_t*);
    cl_context(CL_CALL* CreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*,
                                       void*, void*, cl_int*);
    cl_command_queue(CL_CALL* CreateCommandQueue)(cl_context, cl_device_id,
                                                  cl_command_queue_properties, cl_int*);
    cl_program(CL_CALL* CreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
    cl_int(CL_CALL* BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
    cl_int(CL_CALL* GetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, size_t, void*, size_t*);
    cl_kernel(CL_CALL* CreateKernel)(cl_program, const char*, cl_int*);
    cl_int(CL_CALL* GetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_uint, size_t, void*, size_t*);
    cl_int(CL_CALL* SetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
    cl_mem(CL_CALL* CreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
    cl_int(CL_CALL* EnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t,
                                        const void*, cl_uint, const cl_event*, cl_event*);
    cl_int(CL_CALL* EnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t,
                                       void*, cl_uint, const cl_event*, cl_event*);
    cl_int(CL_CALL* EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*,
                                          const size_t*, const size_t*, cl_uint,
                                          const cl_event*, cl_event*);
    cl_int(CL_CALL* Finish)(cl_command_queue);
    cl_int(CL_CALL* ReleaseMemObject)(cl_mem);
    cl_int(CL_CALL* ReleaseKernel)(cl_kernel);
    cl_int(CL_CALL* ReleaseProgram)(cl_program);
    cl_int(CL_CALL* ReleaseCommandQueue)(cl_command_queue);
    cl_int(CL_CALL* ReleaseContext)(cl_context);
};

const CLApi& cl();
const char* cl_error_string(cl_int err);
