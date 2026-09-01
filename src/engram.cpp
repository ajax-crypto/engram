#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "engram.h"

#include <cstdarg>
#include <tuple>

#ifndef ENGRAM_DISABLE_PMR
#include <optional>
#endif

#include <tuple>
#include <unordered_map>

#ifndef ENGRAM_FALLBACK_PAGESZ
#define ENGRAM_FALLBACK_PAGESZ 4096
#endif

#ifdef ENGRAM_ENABLE_VULKAN
#ifndef ENGRAM_VULKAN_HEADER
#include <vulkan/vulkan.h>
#else
#include ENGRAM_VULKAN_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_DX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

#ifdef ENGRAM_ENABLE_CUDA
#ifndef ENGRAM_CUDA_HEADER
#include <cuda_runtime.h>
#else
#include ENGRAM_CUDA_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_ROCM
#ifndef ENGRAM_ROCM_HEADER
#include <hip/hip_runtime.h>
#else
#include ENGRAM_ROCM_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_OPENCL
#ifndef ENGRAM_OPENCL_HEADER
#include <CL/opencl.h>
#else
#include ENGRAM_OPENCL_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_XDNA
#ifndef ENGRAM_XRT_DEVICE_HEADER
#include <experimental/xrt_device.h>
#else
#include ENGRAM_XRT_DEVICE_HEADER
#endif
#ifndef ENGRAM_XRT_BO_HEADER
#include <experimental/xrt_bo.h>
#else
#include ENGRAM_XRT_BO_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_DPDK
#ifndef ENGRAM_DPDK_MALLOC_HEADER
#include <rte_malloc.h>
#else
#include ENGRAM_DPDK_MALLOC_HEADER
#endif
#ifndef ENGRAM_DPDK_MEMZONE_HEADER
#include <rte_memzone.h>
#else
#include ENGRAM_DPDK_MEMZONE_HEADER
#endif
#include <string_view>
#endif

#ifdef ENGRAM_ENABLE_OP_TEE
#ifndef ENGRAM_OP_TEE_HEADER
#include <tee_internal_api.h>
#else
#include ENGRAM_OP_TEE_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_SYCL
#ifndef ENGRAM_SYCL_HEADER
#include <sycl/sycl.hpp>
#else
#include ENGRAM_SYCL_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO
#ifndef ENGRAM_LEVEL_ZERO_HEADER
#include <level_zero/ze_api.h>
#else
#include ENGRAM_LEVEL_ZERO_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_WEBGPU
#ifndef ENGRAM_WEBGPU_HEADER
#include <webgpu/webgpu.h>
#else
#include ENGRAM_WEBGPU_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_PMDK
#ifndef ENGRAM_PMDK_HEADER
#include <libpmem.h>
#else
#include ENGRAM_PMDK_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_RDMA
#ifndef ENGRAM_RDMA_HEADER
#include <infiniband/verbs.h>
#else
#include ENGRAM_RDMA_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_GPUDIRECT
#include <cuda_runtime.h>
#ifndef ENGRAM_GPUDIRECT_HEADER
#include <cufile.h>
#else
#include ENGRAM_GPUDIRECT_HEADER
#endif
#endif

#ifdef ENGRAM_ENABLE_DMABUF
#include <fcntl.h>
#include <sys/ioctl.h>
#ifndef ENGRAM_DMABUF_HEADER
#include <linux/dma-heap.h>
#else
#include ENGRAM_DMABUF_HEADER
#endif
#endif

// _mm_prefetch and __builtin_prefetch both require constant hint arguments, so the runtime
// rw / locality pair is dispatched to a call with literal hints here.
#ifdef _MSC_VER
#include <intrin.h> // Required for MSVC intrinsics
namespace engram::detail {
inline void prefetch_hint(const void* addr, int rw, int locality)
{
    (void)rw;
#if defined(_M_ARM) || defined(_M_ARM64)
    (void)locality;
    __prefetch(addr);
#else
    switch (locality)
    {
        case 3:  _mm_prefetch((const char*)addr, _MM_HINT_T0); break;
        case 2:  _mm_prefetch((const char*)addr, _MM_HINT_T1); break;
        case 1:  _mm_prefetch((const char*)addr, _MM_HINT_T2); break;
        default: _mm_prefetch((const char*)addr, _MM_HINT_NTA); break;
    }
#endif
}
}
#define PrefetchIntoCache(addr, rw, locality) \
    ::engram::detail::prefetch_hint((const void*)(addr), (int)(rw), (int)(locality))
#elif __GNUC__
namespace engram::detail {
inline void prefetch_hint(const void* addr, int rw, int locality)
{
    if (rw)
        switch (locality)
        {
            case 3:  __builtin_prefetch(addr, 1, 3); break;
            case 2:  __builtin_prefetch(addr, 1, 2); break;
            case 1:  __builtin_prefetch(addr, 1, 1); break;
            default: __builtin_prefetch(addr, 1, 0); break;
        }
    else
        switch (locality)
        {
            case 3:  __builtin_prefetch(addr, 0, 3); break;
            case 2:  __builtin_prefetch(addr, 0, 2); break;
            case 1:  __builtin_prefetch(addr, 0, 1); break;
            default: __builtin_prefetch(addr, 0, 0); break;
        }
}
}
#define PrefetchIntoCache(addr, rw, locality) \
    ::engram::detail::prefetch_hint((const void*)(addr), (int)(rw), (int)(locality))
#else
#warning "The PrefetchIntoCache macro is not defined for this compiler. Please define it appropriately."
#define PrefetchIntoCache(addr, rw, locality) ((void)(addr), (void)(rw), (void)(locality))
#endif

#ifdef ENGRAM_UNIX_ENV
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#elif _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <processthreadsapi.h>
#endif

#if __APPLE__
#ifdef ENGRAM_METAL_CPP
#ifdef ENGRAM_METAL_PRIVATE_IMPL
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#endif
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#else
#include <objc/runtime.h>
#include <objc/message.h>

// Define shorthand for the Objective-C message send function
#define objc_msgSend_id ((id (*)(id, SEL))objc_msgSend)
#define objc_msgSend_id_id ((id (*)(id, SEL, id))objc_msgSend)
#define objc_msgSend_void ((void (*)(id, SEL))objc_msgSend)

// Metal Enums (Standard values for Metal framework)
typedef unsigned long NSUInteger;
typedef enum { MTLStorageModeShared = 0, MTLStorageModePrivate = 2 } MTLStorageMode;
typedef enum { MTLCPUCacheModeDefaultCache = 0 } MTLCPUCacheMode;
typedef enum { MTLResourceStorageModeShared = 0 << 4 } MTLResourceOptions;
#endif
#endif

namespace engram {

// ---------------------------------------------------------------------------
// PIMPL storage: all arena state lives here. The header only forward-declares
// this type, so no allocation details leak to consumers.
// ---------------------------------------------------------------------------
struct impl_data
{
    std::byte*  m_ptr = nullptr;
	std::size_t m_offset = 0;
    std::size_t m_size = 0;
#ifndef ENGRAM_DISABLE_TRACKING
    std::size_t m_count = 0, m_total = 0;
#endif
	memory_source m_type = memory_source::heap;
    arena_error m_error = arena_error::no_error;
    std::size_t m_alignment = alignof(std::max_align_t);

#ifndef ENGRAM_DISABLE_PMR
    std::optional<std::pmr::monotonic_buffer_resource> m_pmr = std::nullopt;
#endif

#ifdef ENGRAM_EASY_POP
    std::array<std::pair<std::byte*, std::size_t>, ENGRAM_MAX_ARRAY_STACKSZ> m_array_sizes;
    std::size_t m_array_stacksz = 0;
#endif

    // One entry per pending arena::save(); restore() rewinds to the newest one.
    struct save_point
    {
        std::size_t offset;
#ifndef ENGRAM_DISABLE_TRACKING
        std::size_t count;
#endif
#ifdef ENGRAM_EASY_POP
        std::size_t array_stacksz;
#endif
    };
    std::array<save_point, ENGRAM_MAX_SAVE_STACKSZ> m_save_stack{};
    std::size_t m_save_stacksz = 0;

	void* m_extra = nullptr;
    bool m_use_sys_free = false;
    bool m_clear_on_free = true;
    bool m_is_managed = false;  
};

arena::arena()
{
    m_impl = new impl_data();
}

#ifdef ENGRAM_ENABLE_DX12
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
#endif

// ---------------------------------------------------------------------------
// Vendor backend hooks. All of them operate on impl_data& (the parameter is
// named `arena` so the bodies read naturally), keeping vendor types out of the
// public header entirely.
// ---------------------------------------------------------------------------
namespace vendor {

#if __APPLE__

static void free_metal(impl_data& arena);

#ifdef ENGRAM_METAL_CPP
static void allocate_metal(impl_data& arena, MTL::Device* device, int32_t flags);
#else
static void allocate_metal(impl_data& arena, id device, int32_t flags);
#endif

#endif

#ifdef ENGRAM_ENABLE_VULKAN
struct vk_mem_tracker
{
	VkDevice device;
	VkBuffer buffer;
	VkDeviceMemory memory;
};
inline static std::unordered_map<std::byte*, vk_mem_tracker> vk_mem_info_map;

static void free_vulkan(impl_data& arena);
static void allocate_vulkan(impl_data& arena, VkDevice& device, VkPhysicalDevice& physicalDevice, const VkAllocationCallbacks* allocCbs, 
    VkDeviceSize offset, VkMemoryMapFlags vkflags, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_DX12
inline static std::unordered_map<std::byte*, ComPtr<ID3D12Resource>> dx12_mem_info_map;

static void free_dx12(impl_data& arena);
static void allocate_dx12(impl_data& arena, ComPtr<ID3D12Device> device, D3D12_RESOURCE_FLAGS descflags, D3D12_RESOURCE_STATES resflags, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_CUDA
static void free_cuda(impl_data& arena);
static void allocate_cuda(impl_data& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_ROCM
static void free_rocm(impl_data& arena);
static void allocate_rocm(impl_data& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_OPENCL
inline static std::unordered_map<std::byte*, cl_context> opencl_mem_info_map;

static void free_opencl(impl_data& arena);
static void allocate_opencl(impl_data& arena, cl_context context, cl_svm_mem_flags clflags, cl_uint alignment, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_XDNA
inline static std::unordered_map<std::byte*, xrtBufferHandle> xdna_buffer_mapping;
	
static void free_xdna(impl_data& arena);
static void allocate_xdna(xrtDeviceHandle device, xrtBufferFlags xoflags, xrtMemoryGroup group, impl_data& arena, int32_t flags);
static xrtBufferHandle get_xrt_buffer_handle(impl_data& arena);
#endif

#ifdef ENGRAM_ENABLE_DPDK
inline static std::unordered_map<std::byte*, const rte_memzone*> dpdk_mem_zone_mapping;

static void free_dpdk(impl_data& arena);
static void allocate_dpdk(std::string_view name, int align, int socketId, unsigned int dpdkFlags, bool useVirtAddr, impl_data& arena, int32_t flags);
static const rte_memzone* get_rte_mem_zone(impl_data& arena);
#endif

#ifdef ENGRAM_ENABLE_OP_TEE

static void free_op_tee(impl_data& arena);
static void allocate_op_tee(impl_data& arena, uint32_t hint, int32_t flags);

#endif

#ifdef ENGRAM_ENABLE_SYCL
inline static std::unordered_map<std::byte*, sycl::queue> sycl_mem_info_map;

static void free_sycl(impl_data& arena);
static void allocate_sycl(impl_data& arena, sycl::queue& queue, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO
inline static std::unordered_map<std::byte*, ze_context_handle_t> level_zero_mem_info_map;

static void free_level_zero(impl_data& arena);
static void allocate_level_zero(impl_data& arena, ze_context_handle_t context, ze_device_handle_t device, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_WEBGPU
inline static std::unordered_map<std::byte*, WGPUBuffer> webgpu_mem_info_map;

static void free_webgpu(impl_data& arena);
static void allocate_webgpu(impl_data& arena, WGPUDevice device, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_PMDK
inline static std::unordered_map<std::byte*, std::pair<std::size_t, bool>> pmdk_map_info;   // { mapped_len, is_pmem }

static void free_pmdk(impl_data& arena);
static void allocate_pmdk(impl_data& arena, const char* path, int pmdk_flags, mode_t mode, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_RDMA
inline static std::unordered_map<std::byte*, ibv_mr*> rdma_mr_map;

static void free_rdma(impl_data& arena);
static void allocate_rdma(impl_data& arena, void* buffer, ibv_pd* pd, int access, int32_t flags);
static ibv_mr* get_rdma_mr(impl_data& arena);
#endif

#ifdef ENGRAM_ENABLE_GPUDIRECT
static void free_gpudirect(impl_data& arena);
static void allocate_gpudirect(impl_data& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_DMABUF
inline static std::unordered_map<std::byte*, int> dmabuf_fd_map;   // ptr -> dma-buf fd

static void free_dmabuf(impl_data& arena);
static void allocate_dmabuf(impl_data& arena, int deviceFd, int32_t flags);
#endif

} // namespace vendor

// ---------------------------------------------------------------------------
// Platform primitives.
// ---------------------------------------------------------------------------
#ifdef ENGRAM_UNIX_ENV

static std::size_t get_total_stack_space()
{
	pthread_attr_t attr;
    void *stack_base;
    std::size_t stack_size;

    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_base, &stack_size);
    pthread_attr_destroy(&attr);
	
	return stack_size;
}

static std::size_t get_page_size()
{
    auto pagesz = sysconf(_SC_PAGESIZE);
    return pagesz == -1 ? ENGRAM_FALLBACK_PAGESZ : pagesz;
}

static std::pair<std::byte*, bool> heap_allocate_impl(std::size_t size, int32_t flags, std::size_t alignment, int fd = -1)
{
    auto shared = (flags & (engram::flags::shared | engram::flags::unified)) != 0;
    auto contiguous = (flags & engram::flags::true_contiguous) != 0;

	if (shared || contiguous)
	{
        auto mapfd = 
#ifdef __linux__
		    (flags & engram::flags::unified) ? fd : -1;
#else
            -1;
#endif
        int mapflags =
#ifdef __APPLE__
            MAP_ANON | 
#else
            MAP_ANONYMOUS | 
#endif
#if defined(BSD)
            (contiguous ? MAP_ALIGNED_SUPER : 0) | 
#elif __linux__
            (contiguous ? MAP_HUGETLB : 0) | 
#else
            0 |
#endif
            (shared ? MAP_SHARED : MAP_PRIVATE);
		void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, mapflags, 
#ifdef __APPLE__
            contiguous ? VM_FLAGS_SUPERPAGE_SIZE_2MB : -1, 
#else
            mapfd,
#endif
            0);
		if (addr == MAP_FAILED) return { nullptr, false };
        if ((flags & engram::flags::pin_to_physical) != 0)
            mlock(addr, size);
		return { (std::byte*)addr, true };
	}
	else
    {
        auto isStdLib = false;
        void* memory = nullptr;

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
		    memory = ::operator new[](size, std::align_val_t{alignment});
		    isStdLib = true;
            if ((flags & engram::flags::pin_to_physical) != 0)
                mlock(memory, size);

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        }
        catch (const std::bad_alloc&) {
            memory = nullptr;
        }
#endif
		return { (std::byte*)memory, !isStdLib };
    }
}
#elif _WIN32

static std::size_t get_total_stack_space()
{
	ULONG_PTR low_limit, high_limit;
    GetCurrentThreadStackLimits(&low_limit, &high_limit);
    auto pagesz = high_limit - low_limit;
    return pagesz > 0 ? pagesz : ENGRAM_FALLBACK_PAGESZ;
}

static std::size_t get_page_size()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

static void* allocate_contiguous(std::size_t size)
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return nullptr;
    }

    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
        CloseHandle(hToken);
        return nullptr;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // AdjustTokenPrivileges returns TRUE even if some privileges aren't adjusted. 
    // Always check GetLastError() afterward.
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL) || 
        GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        CloseHandle(hToken);
        return nullptr;
    }

    CloseHandle(hToken);

    MEM_EXTENDED_PARAMETER extendedParam = {};
    extendedParam.Type = MemExtendedParameterAttributeFlags;
    extendedParam.ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;
    void* pBuffer = VirtualAlloc2(
        GetCurrentProcess(),                                // Process handle
        NULL,                                               // Base address (let OS choose)
        size,                                               // Size of allocation
        MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,         // Allocation flags
        PAGE_READWRITE,                                     // Page protection
        &extendedParam,                                     // Pointer to extended parameters
        1                                                   // Parameter count
    );

    return pBuffer;
}

static std::pair<std::byte*, bool> heap_allocate_impl(std::size_t size, int32_t flags, std::size_t alignment, int fd = -1)
{
    (void)fd;
	void* memory = nullptr;
	auto isStdLib = false;
	if (flags & engram::flags::true_contiguous) 
		memory = allocate_contiguous(size);
	else if (flags & engram::flags::shared)
		memory = VirtualAlloc2(
			GetCurrentProcess(),
			NULL,
			size,
			MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
			PAGE_NOACCESS,
			NULL, 0
		);
	else
	{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
		    memory = ::operator new[](size, std::align_val_t{alignment});
		    isStdLib = true;

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        }
        catch (const std::bad_alloc&) {
            memory = nullptr;
        }
#endif
	}

    if (flags & engram::flags::pin_to_physical)
        VirtualLock(memory, size);
	
	return { (std::byte*)memory, !isStdLib };
}
#endif

static void heap_allocate(impl_data& arena, int32_t flags, std::size_t alignment = alignof(std::max_align_t), int fd = -1)
{
	std::tie(arena.m_ptr, arena.m_use_sys_free) = heap_allocate_impl(arena.m_size, flags, alignment, fd);
    arena.m_alignment = alignment;
    if (arena.m_ptr == nullptr)
        arena.m_error = arena_error::alloc_failed;
    else
        arena.m_type = memory_source::heap;
}

static void heap_free(impl_data& arena)
{
    if (arena.m_ptr)
    {
        if (arena.m_clear_on_free)
            memset(arena.m_ptr, 0, arena.m_size);

        if (arena.m_use_sys_free)
#ifdef _WIN32
            VirtualFree((void*)arena.m_ptr, 0, MEM_RELEASE);
#elif ENGRAM_UNIX_ENV
            munmap((void*)arena.m_ptr, arena.m_size);
#endif
        else
            ::operator delete[](arena.m_ptr, std::align_val_t{arena.m_alignment});
        arena.m_ptr = nullptr;
    }
}

// ---------------------------------------------------------------------------
// General-purpose (backend-agnostic) prefetch helpers.
// ---------------------------------------------------------------------------
void warm_cache(std::byte* ptr, std::size_t size, cache_locality locality, int32_t ioflags)
{
    if (!ptr)
        return;

    auto rw = (ioflags & flags::write) ? 1 : 0;
    auto loc = static_cast<int>(locality);
    if (size == 0)
        size = 1;

    for (std::size_t off = 0; off < size; off += 64)
        PrefetchIntoCache((const void*)(ptr + off), rw, loc);
}

bool prefetch(std::byte* ptr, std::size_t size)
{
    auto ok = false;
    
    if (ptr && size > 0)
    {
#ifdef ENGRAM_UNIX_ENV
#ifdef __linux__
        ok = madvise(ptr, size, MADV_WILLNEED) == 0;
#else
        auto pagesz = get_page_size();
        for (volatile auto* p = ptr; p < ptr + size; p += pagesz)
        {
            auto junk = *p;
            (void)junk;
        }

        ok = true;
#endif
#elif _WIN32
        WIN32_MEMORY_RANGE_ENTRY entry;
        entry.VirtualAddress = ptr;
        entry.NumberOfBytes = size;
        ok = PrefetchVirtualMemory(GetCurrentProcess(), 1, &entry, 0) == TRUE;
#endif
    }

    return ok;
}

// ---------------------------------------------------------------------------
// arena factories.
// ---------------------------------------------------------------------------
arena arena::create(memory_source type, std::size_t size, int32_t flags, std::size_t alignment, int fd)
{
    if (fd != -1)
        flags |= engram::flags::unified;

    arena result;
    auto& d = *result.m_impl;
    d.m_type = type;
    d.m_size = size;
    d.m_clear_on_free = !(flags & engram::flags::no_clear);

    if ((flags & engram::flags::page_aligned) && (type != memory_source::custom))
    {
        auto pagesz = get_page_size();
        if (pagesz > alignment)
            alignment = pagesz;
        size = ((size / pagesz) + 1) * pagesz;
    }

    // Stack arenas come from ENGRAM_STACK_ARENA; external and custom from
    // adopt / create_custom.
    if (type == memory_source::heap)
    {
        heap_allocate(d, flags, alignment, fd);

        if (d.m_ptr != nullptr) 
        {
            if (flags & engram::flags::commit)
                memset(d.m_ptr, 0, size);
        }
        else d.m_error = arena_error::alloc_failed;
    }
    else
    {
        assert(false && "engram::arena::create only builds heap arenas");
        d.m_error = arena_error::alloc_failed;
    }

    return result;
}

#ifdef __linux__
arena arena::heap(std::size_t size, std::string_view name, bool trueContiguous, std::size_t alignment, int fd)
{
    auto result = create(memory_source::heap, size, trueContiguous ? engram::flags::true_contiguous | engram::flags::page_aligned : 
        engram::flags::page_aligned, alignment, fd);
    if (result.m_impl->m_ptr)
    {
        prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, result.m_impl->m_ptr, size, name.data());
    }
    return result;
}

arena arena::heapfile(std::size_t size, std::string_view name, std::size_t alignment)
{
    auto fd = memfd_create(name.data(), MFD_CLOEXEC);
    if (fd != -1) 
    {
        if (ftruncate(fd, size) != -1) 
            return create(memory_source::heap, size, engram::flags::page_aligned, alignment, fd);
        else
            close(fd);
    }

    return arena{};
}
#endif

arena arena::create_custom_va(std::size_t size, custom type, int32_t flags, std::size_t provided, ...)
{
    arena result;
    auto& d = *result.m_impl;
    d.m_type = memory_source::custom;
    d.m_size = size;

    va_list args;
    va_start(args, provided);

    switch (type)
    {
#if __APPLE__
        case custom::Metal:
        {
#ifdef ENGRAM_METAL_CPP
            auto device = va_arg(args, MTL::Device*);
#else
            auto device = va_arg(args, id);
#endif
            vendor::allocate_metal(d, device, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_VULKAN
        case custom::Vulkan:
        {
            auto device = va_arg(args, VkDevice);
            auto physicalDevice = va_arg(args, VkPhysicalDevice);
            auto allocCbs = provided > 2 ? va_arg(args, const VkAllocationCallbacks*) : nullptr;
            auto offset = provided > 3 ? va_arg(args, VkDeviceSize) : VkDeviceSize{0};
            auto vkflags = provided > 4 ? va_arg(args, VkMemoryMapFlags) : VkMemoryMapFlags{0};
            vendor::allocate_vulkan(d, device, physicalDevice, allocCbs, offset, vkflags, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_DX12
        case custom::DX12:
        {
            auto device = va_arg(args, ID3D12Device*);
            auto descflags = provided > 1 ? (D3D12_RESOURCE_FLAGS)va_arg(args, int) : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            auto resflags = provided > 2 ? (D3D12_RESOURCE_STATES)va_arg(args, int) : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            vendor::allocate_dx12(d, device, descflags, resflags, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_CUDA
        case custom::CUDA:
            vendor::allocate_cuda(d, flags);
            break;
#endif
#ifdef ENGRAM_ENABLE_ROCM
        case custom::ROCm:
            vendor::allocate_rocm(d, flags);
            break;
#endif
#ifdef ENGRAM_ENABLE_OPENCL
        case custom::OpenCL:
        {
            auto context = va_arg(args, cl_context);
            auto clflags = va_arg(args, cl_svm_mem_flags);
            auto alignment = va_arg(args, cl_uint);
            vendor::allocate_opencl(d, context, clflags, alignment, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_XDNA
        case custom::XDNA:
        {
            auto device = va_arg(args, xrtDeviceHandle);
            auto xoflags = va_arg(args, xrtBufferFlags);
            auto group = va_arg(args, xrtMemoryGroup);
            vendor::allocate_xdna(device, xoflags, group, d, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_DPDK
        case custom::DPDK:
        {
            auto name = va_arg(args, const char*);
            auto align = va_arg(args, int);
            auto socketId = provided > 2 ? va_arg(args, int) : SOCKET_ID_ANY;
            auto dpdkFlags = provided > 3 ? va_arg(args, unsigned int) : (unsigned int)RTE_MEMZONE_ZEROED;
            auto useVirtAddr = provided > 4 ? (va_arg(args, int) != 0) : true;
            vendor::allocate_dpdk(name, align, socketId, dpdkFlags, useVirtAddr, d, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_OP_TEE
        case custom::OpTee:
        {
            auto hint = provided > 0 ? va_arg(args, uint32_t) : (uint32_t)TEE_MALLOC_FILL_ZERO;
            vendor::allocate_op_tee(d, hint, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_SYCL
        case custom::SYCL:
        {
            auto queue = va_arg(args, sycl::queue*);
            vendor::allocate_sycl(d, *queue, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_LEVEL_ZERO
        case custom::LevelZero:
        {
            auto context = va_arg(args, ze_context_handle_t);
            auto device = va_arg(args, ze_device_handle_t);
            vendor::allocate_level_zero(d, context, device, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_WEBGPU
        case custom::WebGPU:
        {
            auto device = va_arg(args, WGPUDevice);
            vendor::allocate_webgpu(d, device, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_PMDK
        case custom::PMDK:
        {
            auto path = va_arg(args, const char*);
            auto pmdk_flags = provided > 1 ? va_arg(args, int) : PMEM_FILE_CREATE;
            auto mode = provided > 2 ? va_arg(args, mode_t) : (mode_t)0666;
            vendor::allocate_pmdk(d, path, pmdk_flags, mode, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_RDMA
        case custom::RDMA:
        {
            auto buffer = va_arg(args, void*);
            auto pd = va_arg(args, ibv_pd*);
            auto access = provided > 2 ? va_arg(args, int) : IBV_ACCESS_LOCAL_WRITE;
            vendor::allocate_rdma(d, buffer, pd, access, flags);
            break;
        }
#endif
#ifdef ENGRAM_ENABLE_GPUDIRECT
        case custom::GPUDirect:
            vendor::allocate_gpudirect(d, flags);
            break;
#endif
#ifdef ENGRAM_ENABLE_DMABUF
        case custom::DmaBuf:
        {
            auto deviceFd = provided > 0 ? va_arg(args, int) : -1;
            vendor::allocate_dmabuf(d, deviceFd, flags);
            break;
        }
#endif
        default: break;
    }

    va_end(args);
    return result;
}

arena arena::make_external(std::byte* storage, std::size_t size, int32_t flags)
{
    arena result;
    auto& d = *result.m_impl;

    // Align the adopted base up to the max alignment so bump offsets (multiples
    // of that quantum) yield correctly aligned addresses.
    constexpr std::size_t A = alignof(std::max_align_t);
    auto misalign = (std::size_t)(reinterpret_cast<std::uintptr_t>(storage) & (A - 1));
    auto adjust = misalign ? (A - misalign) : 0;
    if (adjust >= size)
    {
        storage = nullptr;
        size = 0;
    }
    else
    {
        storage += adjust;
        size -= adjust;
    }

    d.m_ptr = storage;
    d.m_type = memory_source::external;
    d.m_size = size;
    d.m_clear_on_free = !(flags & engram::flags::no_clear);
    if (storage && (flags & engram::flags::commit))
        memset(storage, 0, size);
    return result;
}

bool stack_fits(std::size_t size)
{
    return size < get_total_stack_space();
}

arena arena::wrap_stack(void* storage, std::size_t size, int32_t flags)
{
    if (!storage)
    {
        arena result;
        auto& d = *result.m_impl;
        d.m_type = memory_source::stack;
        d.m_error = arena_error::stack_overflow;
        return result;
    }

    auto result = make_external((std::byte*)storage, size, flags);
    result.m_impl->m_type = memory_source::stack;
    return result;
}

// ---------------------------------------------------------------------------
// PIMPL bookkeeping helpers used by the header-side templates.
// ---------------------------------------------------------------------------
std::byte* arena::base_ptr() const
{
    return m_impl->m_ptr;
}

// Round a request up to the maximum fundamental alignment. Because the base is
// always max-aligned and every reservation advances the offset by a multiple of
// this quantum, each returned address is suitably aligned for any standard type
// and pop remains exact without tracking per-allocation padding.
static constexpr std::size_t align_up_max(std::size_t n)
{
    constexpr std::size_t A = alignof(std::max_align_t);
    return (n + (A - 1)) & ~(A - 1);
}

std::byte* arena::reserve(std::size_t bytes, bool countable, bool track)
{
    assert(m_impl && "engram: cannot allocate from a moved-from arena");
    auto& d = *m_impl;
    auto rounded = align_up_max(bytes);
    if (!d.m_ptr || rounded > d.m_size - d.m_offset)
    {
        d.m_error = arena_error::alloc_failed;
        return nullptr;
    }

    auto slot = d.m_ptr + d.m_offset;
    d.m_offset += rounded;
#ifndef ENGRAM_DISABLE_TRACKING
    if (countable)
    {
        d.m_count++;
        d.m_total++;
    }
#else
    (void)countable;
#endif
#ifdef ENGRAM_EASY_POP
    if (track)
        d.m_array_sizes[d.m_array_stacksz++] = { slot, bytes };
#else
    (void)track;
#endif
    return slot;
}

std::byte* arena::unreserve(std::size_t bytes, bool countable)
{
    auto& d = *m_impl;
    auto rounded = align_up_max(bytes);
    assert(rounded <= d.m_offset);

    d.m_offset -= rounded;
#ifndef ENGRAM_DISABLE_TRACKING
    if (countable)
        --d.m_count;
#else
    (void)countable;
#endif
    return d.m_ptr + d.m_offset;
}

#ifdef ENGRAM_EASY_POP
std::pair<std::byte*, std::size_t> arena::unreserve_tracked(bool countable)
{
    auto& d = *m_impl;
    auto bytes = d.m_array_sizes[d.m_array_stacksz - 1].second;
    assert(align_up_max(bytes) <= d.m_offset);

    d.m_offset -= align_up_max(bytes);
#ifndef ENGRAM_DISABLE_TRACKING
    if (countable)
        --d.m_count;
#else
    (void)countable;
#endif
    --d.m_array_stacksz;
    return { d.m_ptr + d.m_offset, bytes };
}
#endif

// ---------------------------------------------------------------------------
// Introspection accessors. A moved-from arena has no impl, and must still read
// as an empty arena rather than dereferencing a null pointer.
// ---------------------------------------------------------------------------
bool arena::is_valid() const noexcept { return m_impl && m_impl->m_ptr != nullptr; }
bool arena::empty() const noexcept { return !m_impl || m_impl->m_offset == 0; }
std::size_t arena::used() const noexcept { return m_impl ? m_impl->m_offset : 0; }
std::size_t arena::capacity() const noexcept { return m_impl ? m_impl->m_size : 0; }
std::size_t arena::remaining() const noexcept { return m_impl ? m_impl->m_size - m_impl->m_offset : 0; }
#ifndef ENGRAM_DISABLE_TRACKING
std::size_t arena::count() const noexcept { return m_impl ? m_impl->m_count : 0; }
std::size_t arena::total() const noexcept { return m_impl ? m_impl->m_total : 0; }
#else
std::size_t arena::count() const noexcept { return 0; }
std::size_t arena::total() const noexcept { return 0; }
#endif
memory_source arena::source() const noexcept { return m_impl ? m_impl->m_type : memory_source::external; }
arena_error arena::error() const noexcept { return m_impl ? m_impl->m_error : arena_error::no_error; }

std::span<std::byte> arena::data() const noexcept
{
    if (!m_impl || !m_impl->m_ptr)
        return {};
    return { m_impl->m_ptr, m_impl->m_size };
}

arena arena::partition(std::size_t start, std::size_t size, int32_t flags)
{
    auto& d = *m_impl;
    assert((start + size) <= d.m_size);
    return make_external(d.m_ptr + start, size, (flags & engram::flags::no_clear) ? 0 : engram::flags::commit);
}

arena::~arena()
{
    if (!m_impl)
        return;

    auto& d = *m_impl;
    switch (d.m_type)
    {
        case memory_source::stack: if (d.m_ptr && d.m_clear_on_free) { memset(d.m_ptr, 0, d.m_size); } break;
        case memory_source::heap: heap_free(d); break;
        case memory_source::external: break;
        case memory_source::custom: if (d.m_extra) ((void(*)(impl_data&))d.m_extra)(d); break;
    }

    delete m_impl;
    m_impl = nullptr;
}

#ifndef ENGRAM_DISABLE_PMR
std::pmr::monotonic_buffer_resource& arena::get_pmr_resource(std::pmr::memory_resource* upstream)
{
    auto& d = *m_impl;
    if (!d.m_pmr.has_value())
        d.m_pmr.emplace(d.m_ptr + d.m_offset, d.m_size - d.m_offset, upstream);
    return d.m_pmr.value();
}

std::pmr::monotonic_buffer_resource& arena::get_pmr_resource(std::size_t start, std::size_t size, 
    std::pmr::memory_resource* upstream)
{
    auto& d = *m_impl;
    assert((start + size) <= d.m_size);
    if (!d.m_pmr.has_value())
        d.m_pmr.emplace(d.m_ptr + start, size, upstream);
    return d.m_pmr.value();
}
#endif

void arena::reset() noexcept
{
    auto& d = *m_impl;
    d.m_offset = 0;
#ifndef ENGRAM_DISABLE_TRACKING
    d.m_count = 0;
#endif
#ifdef ENGRAM_EASY_POP
    d.m_array_stacksz = 0;
#endif
    d.m_save_stacksz = 0;
#ifndef ENGRAM_DISABLE_PMR
    d.m_pmr.reset();
#endif
}

bool arena::save() noexcept
{
    auto& d = *m_impl;
    if (d.m_save_stacksz >= d.m_save_stack.size())
        return false;

    auto& sp = d.m_save_stack[d.m_save_stacksz++];
    sp.offset = d.m_offset;
#ifndef ENGRAM_DISABLE_TRACKING
    sp.count = d.m_count;
#endif
#ifdef ENGRAM_EASY_POP
    sp.array_stacksz = d.m_array_stacksz;
#endif
    return true;
}

bool arena::restore() noexcept
{
    auto& d = *m_impl;
    if (d.m_save_stacksz == 0)
        return false;

    const auto& sp = d.m_save_stack[--d.m_save_stacksz];
    // Device storage may not be addressable from the host, so never memset it here.
    if (d.m_ptr && d.m_clear_on_free && (d.m_type != memory_source::custom) && (d.m_offset > sp.offset))
        memset(d.m_ptr + sp.offset, 0, d.m_offset - sp.offset);

    d.m_offset = sp.offset;
#ifndef ENGRAM_DISABLE_TRACKING
    d.m_count = sp.count;
#endif
#ifdef ENGRAM_EASY_POP
    d.m_array_stacksz = sp.array_stacksz;
#endif
    return true;
}

std::size_t arena::save_depth() const noexcept { return m_impl->m_save_stacksz; }

void arena::unpin()
{
    auto& d = *m_impl;
    if (d.m_ptr && (d.m_type == memory_source::heap))
    {
#ifdef __linux__
        munlock(d.m_ptr, d.m_size);
#elif _WIN32
        VirtualUnlock(d.m_ptr, d.m_size);
#endif
    }
}

bool arena::sync_va(std::size_t provided, ...)
{
    auto& d = *m_impl;
    va_list args;
    bool ok = false;
    (void)d;
    (void)provided;
#ifdef ENGRAM_ENABLE_XDNA
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_xdna))
    {
        va_start(args, provided);
        bool host_to_device = va_arg(args, int) != 0;
        va_end(args);

        auto it = vendor::xdna_buffer_mapping.find(d.m_ptr);
        if (it != vendor::xdna_buffer_mapping.end())
            ok = xrtBOSync(it->second, host_to_device ? XCL_BO_SYNC_BO_TO_DEVICE : XCL_BO_SYNC_BO_FROM_DEVICE, 
                d.m_size, 0) == 0;
    }
#endif
#ifdef ENGRAM_ENABLE_CUDA
    if (d.m_ptr && d.m_is_managed && (d.m_extra == (void*)&vendor::free_cuda))
        ok = cudaDeviceSynchronize() == cudaSuccess;
#endif
#ifdef ENGRAM_ENABLE_ROCM
    if (d.m_ptr && d.m_is_managed && (d.m_extra == (void*)&vendor::free_rocm))
        ok = hipDeviceSynchronize() == hipSuccess;
#endif
#ifdef ENGRAM_ENABLE_SYCL
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_sycl))
    {
        auto it = vendor::sycl_mem_info_map.find(d.m_ptr);
        if (it != vendor::sycl_mem_info_map.end())
        {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try { it->second.wait_and_throw(); ok = true; }
            catch (...) { ok = false; }
#else
            it->second.wait();
            ok = true;
#endif
        }
    }
#endif
#ifdef ENGRAM_ENABLE_OPENCL
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_opencl))
    {
        va_start(args, provided);
        cl_command_queue queue = va_arg(args, cl_command_queue);
        va_end(args);

        if (queue)
            ok = clFinish(queue) == CL_SUCCESS;
    }
#endif
#ifdef ENGRAM_ENABLE_PMDK
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_pmdk))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        auto it = vendor::pmdk_map_info.find(d.m_ptr);
        if (it != vendor::pmdk_map_info.end())
        {
            if (it->second.second)   // is_pmem: flush + drain
            {
                pmem_persist(d.m_ptr + start, end - start);
                ok = true;
            }
            else                     // not real pmem: fall back to msync
                ok = pmem_msync(d.m_ptr + start, end - start) == 0;
        }
    }
#endif
#ifdef ENGRAM_ENABLE_GPUDIRECT
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_gpudirect))
        ok = cudaDeviceSynchronize() == cudaSuccess;
#endif
    (void)args;
    return ok;
}

bool arena::prefetch_va(std::size_t provided, ...)
{
    auto& d = *m_impl;
    va_list args;
    bool ok = false;
    (void)provided;

    if (d.m_ptr && d.m_type == memory_source::heap)
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t size = va_arg(args, std::size_t);
        va_end(args);

        assert(start + size <= d.m_size);
        ok = engram::prefetch(d.m_ptr + start, size);
    }
#ifdef ENGRAM_ENABLE_CUDA
    if (d.m_ptr && d.m_is_managed && (d.m_extra == (void*)&vendor::free_cuda))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        int device = va_arg(args, int);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        ok = cudaMemPrefetchAsync(d.m_ptr + start, end - start, device) == cudaSuccess;
    }
#endif
#ifdef ENGRAM_ENABLE_ROCM
    if (d.m_ptr && d.m_is_managed && (d.m_extra == (void*)&vendor::free_rocm))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        int device = va_arg(args, int);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        ok = hipMemPrefetchAsync(d.m_ptr + start, end - start, device) == hipSuccess;
    }
#endif
#ifdef ENGRAM_ENABLE_SYCL
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_sycl))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        auto it = vendor::sycl_mem_info_map.find(d.m_ptr);
        if (it != vendor::sycl_mem_info_map.end())
        {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try { it->second.prefetch(d.m_ptr + start, end - start); ok = true; }
            catch (...) { ok = false; }
#else
            it->second.prefetch(d.m_ptr + start, end - start);
            ok = true;
#endif
        }
    }
#endif
#ifdef ENGRAM_ENABLE_OPENCL
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_opencl))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        cl_command_queue queue = va_arg(args, cl_command_queue);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        if (queue)
        {
            const void* ptrs[1] = { d.m_ptr + start };
            std::size_t sizes[1] = { end - start };
            ok = clEnqueueSVMMigrateMem(queue, 1, ptrs, sizes, 0, 0, nullptr, nullptr) == CL_SUCCESS;
        }
    }
#endif
#ifdef ENGRAM_ENABLE_RDMA
    if (d.m_ptr && (d.m_type == memory_source::custom) && (d.m_extra == (void*)&vendor::free_rdma))
    {
        va_start(args, provided);
        std::size_t start = va_arg(args, std::size_t);
        std::size_t end = va_arg(args, std::size_t);
        va_end(args);

        assert(start <= end && end <= d.m_size);
        auto it = vendor::rdma_mr_map.find(d.m_ptr);
        if (it != vendor::rdma_mr_map.end())
        {
            ibv_sge sg{};
            sg.addr = (uintptr_t)(d.m_ptr + start);
            sg.length = (uint32_t)(end - start);
            sg.lkey = it->second->lkey;
            ok = ibv_advise_mr(it->second->pd, IBV_ADVISE_MR_ADVICE_PREFETCH, 
                IBV_ADVISE_MR_FLAG_FLUSH, &sg, 1) == 0;
        }
    }
#endif
    (void)args;
    return ok;
}

void arena::warm_cache(cache_locality locality, int32_t ioflags, std::size_t start, std::size_t size)
{
    auto& d = *m_impl;
    if (d.m_ptr && (d.m_type != memory_source::custom))
    {
        size = (size == 0) ? d.m_size - start : size;
        assert(start + size <= d.m_size);

        for (auto idx = start; idx < start + size; idx++)
            PrefetchIntoCache(d.m_ptr + idx, (ioflags & flags::write) ? 1 : 0, 
                static_cast<int>(locality));
    }
}

static void handle_heap_fallback(impl_data& arena, int32_t flags)
{
    if ((arena.m_ptr == nullptr) && (flags & engram::flags::heap_fallback))
		heap_allocate(arena, flags);
    else
    {
		arena.m_ptr = nullptr;
        arena.m_error = arena_error::alloc_failed;
    }
}

// ---------------------------------------------------------------------------
// Vendor backend implementations.
// ---------------------------------------------------------------------------
namespace vendor {

#if __APPLE__

#ifdef ENGRAM_METAL_CPP
static inline std::unordered_map<std::byte*, MTL::Buffer*> metal_mem_info_map;

void free_metal(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = metal_mem_info_map.find(arena.m_ptr);
            if (it != metal_mem_info_map.end())
            {
                it->second->release();
                metal_mem_info_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_metal(impl_data& arena, MTL::Device* device, int32_t flags)
{
    auto buffer = device->newBuffer(arena.m_size, MTL::ResourceStorageModeShared);
    if (buffer)
    {
        arena.m_ptr = (std::byte*)buffer->contents();
        arena.m_use_sys_free = true;
        arena.m_extra = &free_metal;
        arena.m_type = memory_source::custom;
        metal_mem_info_map.emplace(arena.m_ptr, buffer);

        if (flags & engram::flags::commit)
            std::memset(arena.m_ptr, 0, arena.m_size);
    }
    else
    {
        handle_heap_fallback(arena, flags);
    }
}

#else

static inline std::unordered_map<std::byte*, std::pair<id, id>> metal_mem_info_map;

void free_metal(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = metal_mem_info_map.find(arena.m_ptr);
            if (it != metal_mem_info_map.end())
            {
                objc_msgSend_void(it->second.second, sel_registerName("release"));
                objc_msgSend_void(it->second.first, sel_registerName("release"));
                metal_mem_info_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_metal(impl_data& arena, id device, int32_t flags)
{
    Class MTLHeapDescriptorClass = objc_lookUpClass("MTLHeapDescriptor");
    id heapDescriptor = ((id (*)(Class, SEL))objc_msgSend)(MTLHeapDescriptorClass, sel_registerName("alloc"));
    heapDescriptor = objc_msgSend_id(heapDescriptor, sel_registerName("init"));

    NSUInteger heapSize = arena.m_size;
    ((void (*)(id, SEL, NSUInteger))objc_msgSend)(heapDescriptor, sel_registerName("setSize:"), heapSize);
    ((void (*)(id, SEL, NSUInteger))objc_msgSend)(heapDescriptor, sel_registerName("setStorageMode:"), MTLStorageModeShared);

    SEL newHeapSel = sel_registerName("newHeapWithDescriptor:");
    id heap = ((id (*)(id, SEL, id))objc_msgSend)(device, newHeapSel, heapDescriptor);
    objc_msgSend_void(heapDescriptor, sel_registerName("release"));

    if (heap) 
    {
        NSUInteger bufferSize = arena.m_size;
        MTLResourceOptions options = MTLResourceStorageModeShared;
        
        SEL newBufferSel = sel_registerName("newBufferWithLength:options:");
        id buffer = ((id (*)(id, SEL, NSUInteger, NSUInteger))objc_msgSend)(heap, newBufferSel, bufferSize, options);
        void* (*MTLBuffer_contents)(id, SEL) = (void* (*)(id, SEL))objc_msgSend;
        void* cpu_ptr = MTLBuffer_contents(buffer, sel_registerName("contents"));

        if (cpu_ptr) 
        {
            arena.m_ptr = (std::byte*)cpu_ptr;
            arena.m_use_sys_free = true;
            arena.m_extra = &free_metal;
            arena.m_type = memory_source::custom;
            metal_mem_info_map.emplace(arena.m_ptr, std::make_pair(heap, buffer));

            if (flags & engram::flags::commit)
                std::memset(arena.m_ptr, 0, arena.m_size);
        } 
    }
    
    if (!arena.m_ptr) 
        handle_heap_fallback(arena, flags);
}

#endif

#endif

#ifdef ENGRAM_ENABLE_VULKAN

static std::optional<uint32_t> vk_find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) 
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    return std::nullopt;
}

void free_vulkan(impl_data& arena)
{
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
		{
			auto it = vk_mem_info_map.find(arena.m_ptr);
			
			if (it != vk_mem_info_map.end())
			{
				auto& [device, buffer, memory] = it->second;
				vkUnmapMemory(device, memory);
				vkFreeMemory(device, memory, nullptr);
				vkDestroyBuffer(device, buffer, nullptr);
				vk_mem_info_map.erase(it);
			}
		}
		else
			heap_free(arena);
}

void allocate_vulkan(impl_data& arena, VkDevice& device, VkPhysicalDevice& physicalDevice, const VkAllocationCallbacks* allocCbs, 
    VkDeviceSize offset, VkMemoryMapFlags vkflags, int32_t flags)
{
	VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = arena.m_size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = (flags & engram::flags::shared) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
	
	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) == VK_SUCCESS)
	{
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
		auto memTypeIndex = vk_find_memory_type(memRequirements.memoryTypeBits, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, physicalDevice);
			
		if (memTypeIndex.has_value())
		{
			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = arena.m_size;
			allocInfo.memoryTypeIndex = memTypeIndex.value();

			VkDeviceMemory memory;
            void* out = nullptr;
			
			if ((vkAllocateMemory(device, &allocInfo, allocCbs, &memory) == VK_SUCCESS) &&
			    (vkBindBufferMemory(device, buffer, memory, offset) == VK_SUCCESS) &&
				(vkMapMemory(device, memory, offset, VK_WHOLE_SIZE, vkflags, &out) == VK_SUCCESS))
			{
				arena.m_ptr = (std::byte*)out;
                vk_mem_info_map.emplace(std::piecewise_construct, std::forward_as_tuple(arena.m_ptr), 
					std::forward_as_tuple(device, buffer, memory));
					
				if (flags & engram::flags::commit)
					std::memset(arena.m_ptr, 0, arena.m_size);
					
				arena.m_use_sys_free = true;
                arena.m_extra = &free_vulkan;
                arena.m_type = memory_source::custom;
			}
		}
	}
	
	handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_DX12

void free_dx12(impl_data& arena)
{
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
		{
			auto it = dx12_mem_info_map.find(arena.m_ptr);
			
			if (it != dx12_mem_info_map.end())
			{
				it->second->Unmap(0, nullptr);
				dx12_mem_info_map.erase(it);
			}
		}
		else
			heap_free(arena);
}

void allocate_dx12(impl_data& arena, ComPtr<ID3D12Device> device, D3D12_RESOURCE_FLAGS descflags, 
    D3D12_RESOURCE_STATES resflags, int32_t flags)
{
	D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD; 

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = arena.m_size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = descflags; 

    ComPtr<ID3D12Resource> computeBuffer;
    if (device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			resflags,
			nullptr,
			IID_PPV_ARGS(&computeBuffer)
		) == S_OK)
	{
		// PERSISTENT MAPPING: Map the memory segment EXACTLY ONCE here
		void* pPersistentlyMappedData = nullptr;
		D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read on the CPU *right now*
		
		if (computeBuffer->Map(0, &readRange, &pPersistentlyMappedData) == S_OK)
		{
			arena.m_ptr = (std::byte*)pPersistentlyMappedData;
			dx12_mem_info_map.emplace(arena.m_ptr, computeBuffer);

            if (flags & engram::flags::commit)
                std::memset(arena.m_ptr, 0, arena.m_size);

            arena.m_use_sys_free = true;
            arena.m_extra = &free_dx12;
            arena.m_type = memory_source::custom;
		}
	}
	
	handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_CUDA

void free_cuda(impl_data& arena) 
{ 
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
			cudaFree(arena.m_ptr);
		else
			heap_free(arena);
}

void allocate_cuda(impl_data& arena, int32_t flags)
{
	auto err = (flags & engram::flags::unified)
		? cudaMallocManaged((void**)&arena.m_ptr, arena.m_size)
		: cudaMalloc((void**)&arena.m_ptr, arena.m_size);
	if (err == cudaSuccess)
	{
		arena.m_extra = &free_cuda;
		arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
        arena.m_is_managed = (flags & engram::flags::unified) != 0;

        if (flags & engram::flags::commit)
            cudaMemset(arena.m_ptr, 0, arena.m_size);
	}
	else 
        arena.m_ptr = nullptr;
	
    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_ROCM

void free_rocm(impl_data& arena) 
{ 
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
			hipFree(arena.m_ptr);
		else
			heap_free(arena);
}

void allocate_rocm(impl_data& arena, int32_t flags)
{
	auto err = (flags & engram::flags::unified)
		? hipMallocManaged((void**)&arena.m_ptr, arena.m_size)
		: hipMalloc((void**)&arena.m_ptr, arena.m_size);
	if (err == hipSuccess)
	{
		arena.m_extra = &free_rocm;
		arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
        arena.m_is_managed = (flags & engram::flags::unified) != 0;

        if (flags & engram::flags::commit)
            hipMemset(arena.m_ptr, 0, arena.m_size);
	}
	else 
        arena.m_ptr = nullptr;
	
    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_OPENCL

void free_opencl(impl_data& arena)
{
    if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
        {
            auto it = opencl_mem_info_map.find(arena.m_ptr);
            if (it != opencl_mem_info_map.end())   
            {
                clSVMFree(it->second, (void*)arena.m_ptr);
                opencl_mem_info_map.erase(it);
            }
        }
		else
			heap_free(arena);
}

void allocate_opencl(impl_data& arena, cl_context context, cl_svm_mem_flags clflags, cl_uint alignment, int32_t flags)
{
    arena.m_ptr = (std::byte*)clSVMAlloc(context, clflags, arena.m_size, alignment);
    if (arena.m_ptr)
    {
        opencl_mem_info_map.emplace(arena.m_ptr, context);
        arena.m_type = memory_source::custom;
        arena.m_extra = &free_opencl;
    }
    
    handle_heap_fallback(arena, flags);
}
#endif

#ifdef ENGRAM_ENABLE_XDNA
	
void free_xdna(impl_data& arena) 
{ 
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
		{
			auto it = xdna_buffer_mapping.find(arena.m_ptr); 
			xrtBOFree(it->second); 
			xdna_buffer_mapping.erase(it);
		}
		else
			heap_free(arena);
}

void allocate_xdna(xrtDeviceHandle device, xrtBufferFlags xoflags, xrtMemoryGroup group, impl_data& arena, int32_t flags)
{
	assert(device != NULL);
	auto handle = xrtBOAlloc(device, arena.m_size, xoflags, group);
	
	if ((handle == NULL) && !(flags & engram::flags::heap_fallback))
		arena.m_ptr = nullptr;
	else if (handle != NULL)
	{
		arena.m_ptr = (std::byte*)xrtBOMap(handle);
		
		if (arena.m_ptr != nullptr)
		{
			xdna_buffer_mapping.emplace(arena.m_ptr, handle);
			arena.m_extra = &free_xdna;
            arena.m_type = memory_source::custom;
		}
		else
		{
			xrtBOFree(handle);
			arena.m_ptr = nullptr;
		}
	}
	
    handle_heap_fallback(arena, flags);
}

xrtBufferHandle get_xrt_buffer_handle(impl_data& arena)
{
    if (arena.m_type == memory_source::custom)
    {
        auto it = xdna_buffer_mapping.find(arena.m_ptr);
        if (it != xdna_buffer_mapping.end())
            return it->second;
    }
    
    return NULL;
}
	
#endif

#ifdef ENGRAM_ENABLE_DPDK

void free_dpdk(impl_data& arena)
{
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
		{
			auto it = dpdk_mem_zone_mapping.find(arena.m_ptr);
			if (it == dpdk_mem_zone_mapping.end())
				rte_free(arena.m_ptr);
			else
			{
				rte_memzone_free(it->second);
				dpdk_mem_zone_mapping.erase(it);
			}
		}
		else
			heap_free(arena);
}

void allocate_dpdk(std::string_view name, int align, int socketId, unsigned int dpdkFlags, bool useVirtAddr, impl_data& arena, int32_t flags)
{
	if (flags & engram::flags::true_contiguous)
	{
		auto memzone = rte_memzone_reserve(name.data(), arena.m_size, socketId, dpdkFlags);
		arena.m_ptr = useVirtAddr ? (std::byte*)memzone->addr : (std::byte*)memzone->iova;
		dpdk_mem_zone_mapping.emplace(arena.m_ptr, memzone);
	}
	else
		arena.m_ptr = (std::byte*)rte_malloc(name.data(), arena.m_size, align);
		
	if (arena.m_ptr)
	{
		arena.m_extra = &free_dpdk;
		arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
	}
	else 
        arena.m_ptr = nullptr;
	
    handle_heap_fallback(arena, flags);
}

const rte_memzone* get_rte_mem_zone(impl_data& arena)
{
    auto it = dpdk_mem_zone_mapping.find(arena.m_ptr);
    return it != dpdk_mem_zone_mapping.end() ? it->second : nullptr;
}

#endif

#ifdef ENGRAM_ENABLE_OP_TEE

void free_op_tee(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
            TEE_Free(arena.m_ptr);
        else
            heap_free(arena);
}

void allocate_op_tee(impl_data& arena, uint32_t hint, int32_t flags)
{
    auto ptr = TEE_Malloc(arena.m_size, hint);
    if (ptr)
    {
        arena.m_ptr = (std::byte*)ptr;
        arena.m_extra = &free_op_tee;
        arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
    }
    else
        arena.m_ptr = nullptr;
}

#endif

#ifdef ENGRAM_ENABLE_SYCL

void free_sycl(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = sycl_mem_info_map.find(arena.m_ptr);
            if (it != sycl_mem_info_map.end())
            {
                sycl::free(arena.m_ptr, it->second);
                sycl_mem_info_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_sycl(impl_data& arena, sycl::queue& queue, int32_t flags)
{
    arena.m_ptr = (std::byte*)sycl::malloc_shared(arena.m_size, queue);
    if (arena.m_ptr)
    {
        arena.m_extra = &free_sycl;
        arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
        sycl_mem_info_map.emplace(arena.m_ptr, queue);

        if (flags & engram::flags::commit)
            std::memset(arena.m_ptr, 0, arena.m_size);
    }

    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO

void free_level_zero(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = level_zero_mem_info_map.find(arena.m_ptr);
            if (it != level_zero_mem_info_map.end())
            {
                zeMemFree(it->second, arena.m_ptr);
                level_zero_mem_info_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_level_zero(impl_data& arena, ze_context_handle_t context, ze_device_handle_t device, int32_t flags)
{
    ze_device_mem_alloc_desc_t deviceDesc = {};
    deviceDesc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;

    ze_host_mem_alloc_desc_t hostDesc = {};
    hostDesc.stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC;

    void* ptr = nullptr;
    if (zeMemAllocShared(context, &deviceDesc, &hostDesc, arena.m_size, arena.m_alignment, device, &ptr) == ZE_RESULT_SUCCESS)
    {
        arena.m_ptr = (std::byte*)ptr;
        arena.m_extra = &free_level_zero;
        arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
        level_zero_mem_info_map.emplace(arena.m_ptr, context);

        if (flags & engram::flags::commit)
            std::memset(arena.m_ptr, 0, arena.m_size);
    }

    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_WEBGPU

void free_webgpu(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = webgpu_mem_info_map.find(arena.m_ptr);
            if (it != webgpu_mem_info_map.end())
            {
                wgpuBufferUnmap(it->second);
                wgpuBufferRelease(it->second);
                webgpu_mem_info_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_webgpu(impl_data& arena, WGPUDevice device, int32_t flags)
{
    WGPUBufferDescriptor desc = {};
    desc.size = arena.m_size;
    desc.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
    desc.mappedAtCreation = true;

    auto buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (buffer)
    {
        arena.m_ptr = (std::byte*)wgpuBufferGetMappedRange(buffer, 0, arena.m_size);
        if (arena.m_ptr)
        {
            arena.m_extra = &free_webgpu;
            arena.m_use_sys_free = true;
            arena.m_type = memory_source::custom;
            webgpu_mem_info_map.emplace(arena.m_ptr, buffer);

            if (flags & engram::flags::commit)
                std::memset(arena.m_ptr, 0, arena.m_size);
        }
        else
            wgpuBufferRelease(buffer);
    }

    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_PMDK

void free_pmdk(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = pmdk_map_info.find(arena.m_ptr);
            if (it != pmdk_map_info.end())
            {
                pmem_unmap(arena.m_ptr, it->second.first);
                pmdk_map_info.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_pmdk(impl_data& arena, const char* path, int pmdk_flags, mode_t mode, int32_t flags)
{
    std::size_t mapped_len = 0;
    int is_pmem = 0;
    void* p = pmem_map_file(path, arena.m_size, pmdk_flags, mode, &mapped_len, &is_pmem);
    if (p)
    {
        arena.m_ptr = (std::byte*)p;
        arena.m_extra = &free_pmdk;
        arena.m_use_sys_free = true;
        arena.m_type = memory_source::custom;
        pmdk_map_info.emplace(arena.m_ptr, std::make_pair(mapped_len, is_pmem != 0));

        if (flags & engram::flags::commit)
        {
            if (is_pmem)
                pmem_memset_persist(arena.m_ptr, 0, arena.m_size);
            else
                std::memset(arena.m_ptr, 0, arena.m_size);
        }
    }

    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_RDMA

void free_rdma(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = rdma_mr_map.find(arena.m_ptr);
            if (it != rdma_mr_map.end())
            {
                ibv_dereg_mr(it->second);
                rdma_mr_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_rdma(impl_data& arena, void* buffer, ibv_pd* pd, int access, int32_t flags)
{
    if (buffer)
    {
        auto mr = ibv_reg_mr(pd, buffer, arena.m_size, access);
        if (mr)
        {
            arena.m_ptr = (std::byte*)buffer;
            arena.m_extra = &free_rdma;
            arena.m_type = memory_source::custom;
            rdma_mr_map.emplace(arena.m_ptr, mr);

            if (flags & engram::flags::commit)
                std::memset(arena.m_ptr, 0, arena.m_size);
        }
    }

    handle_heap_fallback(arena, flags);
}

ibv_mr* get_rdma_mr(impl_data& arena)
{
    if (arena.m_type == memory_source::custom)
    {
        auto it = rdma_mr_map.find(arena.m_ptr);
        if (it != rdma_mr_map.end())
            return it->second;
    }

    return nullptr;
}

#endif

#ifdef ENGRAM_ENABLE_GPUDIRECT

void free_gpudirect(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            cuFileBufDeregister(arena.m_ptr);
            cudaFree(arena.m_ptr);
        }
        else
            heap_free(arena);
}

void allocate_gpudirect(impl_data& arena, int32_t flags)
{
    if (cudaMalloc((void**)&arena.m_ptr, arena.m_size) == cudaSuccess)
    {
        if (cuFileBufRegister(arena.m_ptr, arena.m_size, 0).err == CU_FILE_SUCCESS)
        {
            arena.m_extra = &free_gpudirect;
            arena.m_use_sys_free = true;
            arena.m_type = memory_source::custom;

            if (flags & engram::flags::commit)
                cudaMemset(arena.m_ptr, 0, arena.m_size);
        }
        else
        {
            cudaFree(arena.m_ptr);
            arena.m_ptr = nullptr;
        }
    }
    else
        arena.m_ptr = nullptr;

    handle_heap_fallback(arena, flags);
}

#endif

#ifdef ENGRAM_ENABLE_DMABUF

void free_dmabuf(impl_data& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
        {
            auto it = dmabuf_fd_map.find(arena.m_ptr);
            if (it != dmabuf_fd_map.end())
            {
                munmap(arena.m_ptr, arena.m_size);
                close(it->second);
                dmabuf_fd_map.erase(it);
            }
        }
        else
            heap_free(arena);
}

void allocate_dmabuf(impl_data& arena, int deviceFd, int32_t flags)
{
    // A negative device fd means "use the default system dma-buf heap".
    bool ownsHeap = deviceFd < 0;
    int heapFd = ownsHeap ? open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC) : deviceFd;
    if (heapFd < 0)
    {
        arena.m_ptr = nullptr;
        return;
    }

    dma_heap_allocation_data alloc = {};
    alloc.len = arena.m_size;
    alloc.fd_flags = O_RDWR | O_CLOEXEC;

    int rc = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &alloc);
    if (ownsHeap)
        close(heapFd);
    if (rc != 0)
    {
        arena.m_ptr = nullptr;
        return;
    }

    void* p = mmap(nullptr, arena.m_size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc.fd, 0);
    if (p == MAP_FAILED)
    {
        close(alloc.fd);
        arena.m_ptr = nullptr;
        return;
    }

    arena.m_ptr = (std::byte*)p;
    arena.m_extra = &free_dmabuf;
    arena.m_use_sys_free = true;
    arena.m_type = memory_source::custom;
    dmabuf_fd_map.emplace(arena.m_ptr, alloc.fd);

    if (flags & engram::flags::commit)
        std::memset(arena.m_ptr, 0, arena.m_size);
}

#endif

} // namespace vendor

} // namespace engram
