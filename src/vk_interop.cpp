#include "vk_interop.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <malloc.h>
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ember {
namespace {

using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkQueue = void*;
using VkBuffer = std::uint64_t;
using VkDeviceMemory = std::uint64_t;
using VkCommandPool = std::uint64_t;
using VkCommandBuffer = void*;
using VkFence = std::uint64_t;
using VkFlags = std::uint32_t;
using VkDeviceSize = std::uint64_t;

constexpr std::uint32_t VK_SUCCESS_CODE = 0;
constexpr std::uint32_t VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
constexpr std::uint32_t VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
constexpr std::uint32_t VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2;
constexpr std::uint32_t VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3;
constexpr std::uint32_t VK_STRUCTURE_TYPE_SUBMIT_INFO = 4;
constexpr std::uint32_t VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5;
constexpr std::uint32_t VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8;
constexpr std::uint32_t VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12;
constexpr std::uint32_t VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39;
constexpr std::uint32_t VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40;
constexpr std::uint32_t VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42;
constexpr std::uint32_t VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT = 1000178000;
constexpr std::uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 = 1000059001;
constexpr std::uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT = 1000178002;
constexpr std::uint32_t VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES = 1000196000;

constexpr std::uint32_t VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT = 0x00000080;
constexpr std::uint32_t VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001;
constexpr std::uint32_t VK_BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002;
constexpr std::uint32_t VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020;
constexpr std::uint32_t VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x00000001;
constexpr std::uint32_t VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x00000002;
constexpr std::uint32_t VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x00000004;
constexpr std::uint32_t VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0;
constexpr std::uint32_t VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x00000001;
constexpr std::uint32_t VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 0x00000002;
constexpr std::uint32_t VK_QUEUE_COMPUTE_BIT = 0x00000002;
constexpr std::uint32_t VK_QUEUE_TRANSFER_BIT = 0x00000004;
constexpr std::uint32_t VK_SHARING_MODE_EXCLUSIVE = 0;
constexpr std::uint64_t VK_WHOLE_SIZE = ~0ull;

struct VkExtensionProperties {
    char extensionName[256];
    std::uint32_t specVersion;
};

struct VkApplicationInfo {
    std::uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    std::uint32_t applicationVersion;
    const char* pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

struct VkPhysicalDeviceLimitsStub {
    std::uint64_t opaque[63];
};

struct VkPhysicalDeviceSparsePropertiesStub {
    std::uint32_t opaque[5];
};

struct VkPhysicalDeviceProperties {
    std::uint32_t apiVersion;
    std::uint32_t driverVersion;
    std::uint32_t vendorID;
    std::uint32_t deviceID;
    std::uint32_t deviceType;
    char deviceName[256];
    std::uint8_t pipelineCacheUUID[16];
    VkPhysicalDeviceLimitsStub limits;
    VkPhysicalDeviceSparsePropertiesStub sparseProperties;
};

static_assert(sizeof(VkPhysicalDeviceProperties) == 824);

struct VkPhysicalDeviceProperties2 {
    std::uint32_t sType;
    void* pNext;
    VkPhysicalDeviceProperties properties;
};

static_assert(sizeof(VkPhysicalDeviceProperties2) == 840);

struct VkPhysicalDeviceExternalMemoryHostPropertiesEXT {
    std::uint32_t sType;
    void* pNext;
    VkDeviceSize minImportedHostPointerAlignment;
};

struct VkPhysicalDeviceDriverProperties {
    std::uint32_t sType;
    void* pNext;
    std::uint32_t driverID;
    char driverName[256];
    char driverInfo[256];
    std::uint32_t conformanceVersion;
};

struct VkMemoryType {
    std::uint32_t propertyFlags;
    std::uint32_t heapIndex;
};

struct VkMemoryHeap {
    VkDeviceSize size;
    std::uint32_t flags;
};

struct VkPhysicalDeviceMemoryProperties {
    std::uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    std::uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};

struct VkQueueFamilyProperties {
    std::uint32_t queueFlags;
    std::uint32_t queueCount;
    std::uint32_t timestampValidBits;
    std::uint32_t minImageTransferGranularity[3];
};

struct VkDeviceQueueCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    std::uint32_t queueFamilyIndex;
    std::uint32_t queueCount;
    const float* pQueuePriorities;
};

struct VkDeviceCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    std::uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
};

struct VkBufferCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    VkDeviceSize size;
    std::uint32_t usage;
    std::uint32_t sharingMode;
    std::uint32_t queueFamilyIndexCount;
    const std::uint32_t* pQueueFamilyIndices;
};

struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    std::uint32_t memoryTypeBits;
};

struct VkMemoryAllocateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkDeviceSize allocationSize;
    std::uint32_t memoryTypeIndex;
};

struct VkImportMemoryHostPointerInfoEXT {
    std::uint32_t sType;
    const void* pNext;
    std::uint32_t handleType;
    void* pHostPointer;
};

struct VkCommandPoolCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    std::uint32_t queueFamilyIndex;
};

struct VkCommandBufferAllocateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkCommandPool commandPool;
    std::uint32_t level;
    std::uint32_t commandBufferCount;
};

struct VkCommandBufferBeginInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const void* pInheritanceInfo;
};

struct VkSubmitInfo {
    std::uint32_t sType;
    const void* pNext;
    std::uint32_t waitSemaphoreCount;
    const void* pWaitSemaphores;
    const std::uint32_t* pWaitDstStageMask;
    std::uint32_t commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    std::uint32_t signalSemaphoreCount;
    const void* pSignalSemaphores;
};

struct VkFenceCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    VkFlags flags;
};

struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
};

#if defined(_WIN32)
#define VKAPI __stdcall
#else
#define VKAPI
#endif

struct VulkanApi {
    void*(VKAPI* GetInstanceProcAddr)(VkInstance, const char*) = nullptr;
    std::uint32_t(VKAPI* CreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*) = nullptr;
    void(VKAPI* DestroyInstance)(VkInstance, const void*) = nullptr;
    std::uint32_t(VKAPI* EnumeratePhysicalDevices)(VkInstance, std::uint32_t*, VkPhysicalDevice*) = nullptr;
    void(VKAPI* GetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*) = nullptr;
    void(VKAPI* GetPhysicalDeviceProperties2)(VkPhysicalDevice, VkPhysicalDeviceProperties2*) = nullptr;
    void(VKAPI* GetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*) = nullptr;
    void(VKAPI* GetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, std::uint32_t*, VkQueueFamilyProperties*) = nullptr;
    std::uint32_t(VKAPI* EnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char*, std::uint32_t*, VkExtensionProperties*) = nullptr;
    std::uint32_t(VKAPI* CreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*) = nullptr;
    void(VKAPI* DestroyDevice)(VkDevice, const void*) = nullptr;
    void(VKAPI* GetDeviceQueue)(VkDevice, std::uint32_t, std::uint32_t, VkQueue*) = nullptr;
    std::uint32_t(VKAPI* CreateBuffer)(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*) = nullptr;
    void(VKAPI* DestroyBuffer)(VkDevice, VkBuffer, const void*) = nullptr;
    void(VKAPI* GetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements*) = nullptr;
    std::uint32_t(VKAPI* AllocateMemory)(VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*) = nullptr;
    void(VKAPI* FreeMemory)(VkDevice, VkDeviceMemory, const void*) = nullptr;
    std::uint32_t(VKAPI* BindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) = nullptr;
    std::uint32_t(VKAPI* CreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*) = nullptr;
    void(VKAPI* DestroyCommandPool)(VkDevice, VkCommandPool, const void*) = nullptr;
    std::uint32_t(VKAPI* AllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*) = nullptr;
    std::uint32_t(VKAPI* BeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*) = nullptr;
    std::uint32_t(VKAPI* EndCommandBuffer)(VkCommandBuffer) = nullptr;
    void(VKAPI* CmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, std::uint32_t, const VkBufferCopy*) = nullptr;
    std::uint32_t(VKAPI* CreateFence)(VkDevice, const VkFenceCreateInfo*, const void*, VkFence*) = nullptr;
    void(VKAPI* DestroyFence)(VkDevice, VkFence, const void*) = nullptr;
    std::uint32_t(VKAPI* QueueSubmit)(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) = nullptr;
    std::uint32_t(VKAPI* WaitForFences)(VkDevice, std::uint32_t, const VkFence*, std::uint32_t, std::uint64_t) = nullptr;
    std::uint32_t(VKAPI* QueueWaitIdle)(VkQueue) = nullptr;
};

#if defined(_WIN32)
using LibHandle = HMODULE;
LibHandle open_vulkan() { return ::LoadLibraryA("vulkan-1.dll"); }
void* lookup(LibHandle h, const char* name) { return reinterpret_cast<void*>(::GetProcAddress(h, name)); }
#elif defined(__APPLE__)
using LibHandle = void*;
LibHandle open_vulkan() {
    for (const char* n : {"libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib"}) {
        if (LibHandle h = ::dlopen(n, RTLD_LAZY | RTLD_LOCAL)) return h;
    }
    return nullptr;
}
void* lookup(LibHandle h, const char* name) { return ::dlsym(h, name); }
#else
using LibHandle = void*;
LibHandle open_vulkan() {
    for (const char* n : {"libvulkan.so.1", "libvulkan.so"}) {
        if (LibHandle h = ::dlopen(n, RTLD_LAZY | RTLD_LOCAL)) return h;
    }
    return nullptr;
}
void* lookup(LibHandle h, const char* name) { return ::dlsym(h, name); }
#endif

bool has_extension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list)
        if (std::strncmp(e.extensionName, name, sizeof(e.extensionName)) == 0) return true;
    return false;
}

std::string version_string(std::uint32_t packed) {
    return std::to_string(packed >> 22) + "." + std::to_string((packed >> 12) & 0x3FF) + "." +
           std::to_string(packed & 0xFFF);
}

}

struct VulkanContext::Impl {
    LibHandle library = nullptr;
    VulkanApi api;
    VkInstance instance = nullptr;
    VkPhysicalDevice physical = nullptr;
    VkDevice device = nullptr;
    VkQueue queue = nullptr;
    std::uint32_t queue_family = 0;
    VkCommandPool pool = 0;
    VkPhysicalDeviceMemoryProperties memory{};

    VkBuffer imported_buffer = 0;
    VkDeviceMemory imported_memory = 0;
    VkBuffer device_buffer = 0;
    VkDeviceMemory device_memory = 0;
    std::size_t imported_bytes = 0;

    int find_memory_type(std::uint32_t type_bits, std::uint32_t required) const {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) == 0) continue;
            if ((memory.memoryTypes[i].propertyFlags & required) == required) return static_cast<int>(i);
        }
        return -1;
    }
};

const char* tier_name(InteropTier tier) {
    switch (tier) {
        case InteropTier::ExternalHandle: return "2 (external handle, zero copy)";
        case InteropTier::SharedHostMemory: return "1 (shared host allocation, zero copy)";
        case InteropTier::StagedCopy: return "0 (staged copy)";
        default: return "unavailable";
    }
}

HostAllocation allocate_aligned(std::size_t bytes, std::size_t alignment) {
    HostAllocation allocation;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    const std::size_t rounded = ((bytes + alignment - 1) / alignment) * alignment;
#if defined(_WIN32)
    allocation.pointer = _aligned_malloc(rounded, alignment);
#else
    if (::posix_memalign(&allocation.pointer, alignment, rounded) != 0) allocation.pointer = nullptr;
#endif
    if (allocation.pointer) {
        allocation.bytes = rounded;
        allocation.alignment = alignment;
        std::memset(allocation.pointer, 0, rounded);
    }
    return allocation;
}

void free_aligned(HostAllocation& allocation) {
    if (!allocation.pointer) return;
#if defined(_WIN32)
    _aligned_free(allocation.pointer);
#else
    std::free(allocation.pointer);
#endif
    allocation = HostAllocation{};
}

VulkanContext::VulkanContext() {
    impl_ = new Impl();

    impl_->library = open_vulkan();
    if (!impl_->library) {
        capabilities_.unavailable_reason = "no Vulkan loader found";
        return;
    }

    auto& api = impl_->api;
    api.GetInstanceProcAddr = reinterpret_cast<decltype(api.GetInstanceProcAddr)>(
        lookup(impl_->library, "vkGetInstanceProcAddr"));
    if (!api.GetInstanceProcAddr) {
        capabilities_.unavailable_reason = "vkGetInstanceProcAddr missing";
        return;
    }

    auto global = [&](const char* name) { return api.GetInstanceProcAddr(nullptr, name); };
    api.CreateInstance = reinterpret_cast<decltype(api.CreateInstance)>(global("vkCreateInstance"));
    if (!api.CreateInstance) {
        capabilities_.unavailable_reason = "vkCreateInstance missing";
        return;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ember";
    app.pEngineName = "ember";
    app.apiVersion = (1u << 22) | (1u << 12);

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;

    if (api.CreateInstance(&info, nullptr, &impl_->instance) != VK_SUCCESS_CODE) {
        capabilities_.unavailable_reason = "vkCreateInstance failed";
        return;
    }

    auto load = [&](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(
            api.GetInstanceProcAddr(impl_->instance, name));
    };
    load(api.DestroyInstance, "vkDestroyInstance");
    load(api.EnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    load(api.GetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    load(api.GetPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");
    load(api.GetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    load(api.GetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    load(api.EnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
    load(api.CreateDevice, "vkCreateDevice");
    load(api.DestroyDevice, "vkDestroyDevice");
    load(api.GetDeviceQueue, "vkGetDeviceQueue");
    load(api.CreateBuffer, "vkCreateBuffer");
    load(api.DestroyBuffer, "vkDestroyBuffer");
    load(api.GetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    load(api.AllocateMemory, "vkAllocateMemory");
    load(api.FreeMemory, "vkFreeMemory");
    load(api.BindBufferMemory, "vkBindBufferMemory");
    load(api.CreateCommandPool, "vkCreateCommandPool");
    load(api.DestroyCommandPool, "vkDestroyCommandPool");
    load(api.AllocateCommandBuffers, "vkAllocateCommandBuffers");
    load(api.BeginCommandBuffer, "vkBeginCommandBuffer");
    load(api.EndCommandBuffer, "vkEndCommandBuffer");
    load(api.CmdCopyBuffer, "vkCmdCopyBuffer");
    load(api.CreateFence, "vkCreateFence");
    load(api.DestroyFence, "vkDestroyFence");
    load(api.QueueSubmit, "vkQueueSubmit");
    load(api.WaitForFences, "vkWaitForFences");
    load(api.QueueWaitIdle, "vkQueueWaitIdle");

    std::uint32_t count = 0;
    if (api.EnumeratePhysicalDevices(impl_->instance, &count, nullptr) != VK_SUCCESS_CODE || count == 0) {
        capabilities_.unavailable_reason = "no Vulkan physical devices";
        return;
    }
    std::vector<VkPhysicalDevice> devices(count);
    api.EnumeratePhysicalDevices(impl_->instance, &count, devices.data());

    impl_->physical = devices[0];
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props{};
        api.GetPhysicalDeviceProperties(candidate, &props);
        if (props.deviceType == 2) {
            impl_->physical = candidate;
            break;
        }
    }

    VkPhysicalDeviceProperties props{};
    api.GetPhysicalDeviceProperties(impl_->physical, &props);
    capabilities_.device_name = props.deviceName;
    capabilities_.api_version = version_string(props.apiVersion);

    std::uint32_t ext_count = 0;
    api.EnumerateDeviceExtensionProperties(impl_->physical, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> extensions(ext_count);
    if (ext_count)
        api.EnumerateDeviceExtensionProperties(impl_->physical, nullptr, &ext_count, extensions.data());

    capabilities_.external_memory_host = has_extension(extensions, "VK_EXT_external_memory_host");
    capabilities_.timeline_semaphore = has_extension(extensions, "VK_KHR_timeline_semaphore") ||
                                       props.apiVersion >= ((1u << 22) | (2u << 12));
    capabilities_.ray_query = has_extension(extensions, "VK_KHR_ray_query");
    capabilities_.acceleration_structure = has_extension(extensions, "VK_KHR_acceleration_structure");
#if defined(_WIN32)
    capabilities_.external_memory_handle = has_extension(extensions, "VK_KHR_external_memory_win32");
    capabilities_.external_semaphore = has_extension(extensions, "VK_KHR_external_semaphore_win32");
#else
    capabilities_.external_memory_handle = has_extension(extensions, "VK_KHR_external_memory_fd");
    capabilities_.external_semaphore = has_extension(extensions, "VK_KHR_external_semaphore_fd");
#endif

    if (api.GetPhysicalDeviceProperties2) {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT host{};
        host.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
        VkPhysicalDeviceDriverProperties driver{};
        driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        driver.pNext = &host;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver;
        api.GetPhysicalDeviceProperties2(impl_->physical, &props2);
        capabilities_.min_imported_host_pointer_alignment =
            static_cast<std::size_t>(host.minImportedHostPointerAlignment);
        capabilities_.driver_id = driver.driverName;
    }

    api.GetPhysicalDeviceMemoryProperties(impl_->physical, &impl_->memory);
    for (std::uint32_t i = 0; i < impl_->memory.memoryHeapCount; ++i)
        if (impl_->memory.memoryHeaps[i].flags & 1u)
            capabilities_.device_local_bytes =
                std::max<std::uint64_t>(capabilities_.device_local_bytes, impl_->memory.memoryHeaps[i].size);

    std::uint32_t family_count = 0;
    api.GetPhysicalDeviceQueueFamilyProperties(impl_->physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    api.GetPhysicalDeviceQueueFamilyProperties(impl_->physical, &family_count, families.data());

    bool found_family = false;
    for (std::uint32_t i = 0; i < family_count; ++i) {
        if (families[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) {
            impl_->queue_family = i;
            found_family = true;
            break;
        }
    }
    if (!found_family) {
        capabilities_.unavailable_reason = "no compute or transfer queue family";
        return;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = impl_->queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    std::vector<const char*> enabled;
    if (capabilities_.external_memory_host) {
        enabled.push_back("VK_KHR_external_memory");
        enabled.push_back("VK_EXT_external_memory_host");
    }

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    device_info.ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data();

    if (api.CreateDevice(impl_->physical, &device_info, nullptr, &impl_->device) != VK_SUCCESS_CODE) {
        capabilities_.unavailable_reason = "vkCreateDevice failed";
        return;
    }

    api.GetDeviceQueue(impl_->device, impl_->queue_family, 0, &impl_->queue);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = impl_->queue_family;
    if (api.CreateCommandPool(impl_->device, &pool_info, nullptr, &impl_->pool) != VK_SUCCESS_CODE) {
        capabilities_.unavailable_reason = "vkCreateCommandPool failed";
        return;
    }

    capabilities_.loaded = true;
}

VulkanContext::~VulkanContext() {
    if (!impl_) return;
    release_imported();
    auto& api = impl_->api;
    if (impl_->device) {
        if (impl_->pool) api.DestroyCommandPool(impl_->device, impl_->pool, nullptr);
        api.DestroyDevice(impl_->device, nullptr);
    }
    if (impl_->instance && api.DestroyInstance) api.DestroyInstance(impl_->instance, nullptr);
    delete impl_;
    impl_ = nullptr;
}

bool VulkanContext::import_host_buffer(void* host_pointer, std::size_t bytes) {
    if (!capabilities_.loaded || !capabilities_.external_memory_host) return false;
    release_imported();

    auto& api = impl_->api;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = bytes;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (api.CreateBuffer(impl_->device, &buffer_info, nullptr, &impl_->imported_buffer) != VK_SUCCESS_CODE)
        return false;

    VkMemoryRequirements requirements{};
    api.GetBufferMemoryRequirements(impl_->device, impl_->imported_buffer, &requirements);

    const int type_index = impl_->find_memory_type(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type_index < 0) {
        release_imported();
        return false;
    }

    VkImportMemoryHostPointerInfoEXT import{};
    import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    import.pHostPointer = host_pointer;

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.pNext = &import;
    allocate.allocationSize = bytes;
    allocate.memoryTypeIndex = static_cast<std::uint32_t>(type_index);

    if (api.AllocateMemory(impl_->device, &allocate, nullptr, &impl_->imported_memory) != VK_SUCCESS_CODE) {
        release_imported();
        return false;
    }
    if (api.BindBufferMemory(impl_->device, impl_->imported_buffer, impl_->imported_memory, 0) !=
        VK_SUCCESS_CODE) {
        release_imported();
        return false;
    }

    VkBufferCreateInfo device_info = buffer_info;
    if (api.CreateBuffer(impl_->device, &device_info, nullptr, &impl_->device_buffer) != VK_SUCCESS_CODE) {
        release_imported();
        return false;
    }
    VkMemoryRequirements device_requirements{};
    api.GetBufferMemoryRequirements(impl_->device, impl_->device_buffer, &device_requirements);
    int device_type_index =
        impl_->find_memory_type(device_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (device_type_index < 0)
        device_type_index = impl_->find_memory_type(device_requirements.memoryTypeBits, 0);
    if (device_type_index < 0) {
        release_imported();
        return false;
    }

    VkMemoryAllocateInfo device_allocate{};
    device_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    device_allocate.allocationSize = device_requirements.size;
    device_allocate.memoryTypeIndex = static_cast<std::uint32_t>(device_type_index);
    if (api.AllocateMemory(impl_->device, &device_allocate, nullptr, &impl_->device_memory) !=
        VK_SUCCESS_CODE) {
        release_imported();
        return false;
    }
    if (api.BindBufferMemory(impl_->device, impl_->device_buffer, impl_->device_memory, 0) !=
        VK_SUCCESS_CODE) {
        release_imported();
        return false;
    }

    impl_->imported_bytes = bytes;
    return true;
}

bool VulkanContext::run_resolve_pass(std::size_t bytes) {
    if (!impl_->imported_buffer || !impl_->device_buffer) return false;
    auto& api = impl_->api;

    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = impl_->pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;

    VkCommandBuffer command = nullptr;
    if (api.AllocateCommandBuffers(impl_->device, &allocate, &command) != VK_SUCCESS_CODE) return false;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (api.BeginCommandBuffer(command, &begin) != VK_SUCCESS_CODE) return false;

    VkBufferCopy region{};
    region.size = bytes;
    api.CmdCopyBuffer(command, impl_->imported_buffer, impl_->device_buffer, 1, &region);
    api.CmdCopyBuffer(command, impl_->device_buffer, impl_->imported_buffer, 1, &region);

    if (api.EndCommandBuffer(command) != VK_SUCCESS_CODE) return false;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = 0;
    if (api.CreateFence(impl_->device, &fence_info, nullptr, &fence) != VK_SUCCESS_CODE) return false;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;

    bool ok = api.QueueSubmit(impl_->queue, 1, &submit, fence) == VK_SUCCESS_CODE;
    if (ok) ok = api.WaitForFences(impl_->device, 1, &fence, 1, 5000000000ull) == VK_SUCCESS_CODE;
    api.DestroyFence(impl_->device, fence, nullptr);
    return ok;
}

void VulkanContext::release_imported() {
    if (!impl_ || !impl_->device) return;
    auto& api = impl_->api;
    if (impl_->queue && api.QueueWaitIdle) api.QueueWaitIdle(impl_->queue);
    if (impl_->device_buffer) {
        api.DestroyBuffer(impl_->device, impl_->device_buffer, nullptr);
        impl_->device_buffer = 0;
    }
    if (impl_->device_memory) {
        api.FreeMemory(impl_->device, impl_->device_memory, nullptr);
        impl_->device_memory = 0;
    }
    if (impl_->imported_buffer) {
        api.DestroyBuffer(impl_->device, impl_->imported_buffer, nullptr);
        impl_->imported_buffer = 0;
    }
    if (impl_->imported_memory) {
        api.FreeMemory(impl_->device, impl_->imported_memory, nullptr);
        impl_->imported_memory = 0;
    }
    impl_->imported_bytes = 0;
}

}
