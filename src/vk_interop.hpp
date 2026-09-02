#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ember {

enum class InteropTier : int {
    Unavailable = -1,
    StagedCopy = 0,
    SharedHostMemory = 1,
    ExternalHandle = 2,
};

const char* tier_name(InteropTier tier);

struct VulkanCapabilities {
    bool loaded = false;
    std::string device_name;
    std::string api_version;
    std::string driver_id;
    bool external_memory_host = false;
    bool external_memory_handle = false;
    bool external_semaphore = false;
    bool timeline_semaphore = false;
    bool ray_query = false;
    bool acceleration_structure = false;
    std::uint64_t device_local_bytes = 0;
    std::size_t min_imported_host_pointer_alignment = 0;
    std::string unavailable_reason;
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    const VulkanCapabilities& capabilities() const { return capabilities_; }

    bool import_host_buffer(void* host_pointer, std::size_t bytes);
    bool run_resolve_pass(std::size_t bytes);
    void release_imported();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    VulkanCapabilities capabilities_;
};

struct HostAllocation {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
};

HostAllocation allocate_aligned(std::size_t bytes, std::size_t alignment);
void free_aligned(HostAllocation& allocation);

}
