#pragma once
#ifndef ENGRAM_H_INCLUDED
#define ENGRAM_H_INCLUDED

/**
 * @file engram.h
 * @brief engram — a move-only bump-allocation `arena` over stack, heap, external,
 *        and GPU / accelerator device memory (single-header build).
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

// A conforming freestanding implementation reports __STDC_HOSTED__ as 0.
#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0) && !defined(ENGRAM_ENABLE_FREESTANDING)
#define ENGRAM_ENABLE_FREESTANDING
#endif

#ifdef ENGRAM_ENABLE_FREESTANDING
// No OS, no heap, no vendor SDKs: only stack and adopted arenas remain.
#undef ENGRAM_ALL
#undef ENGRAM_ENABLE_VULKAN
#undef ENGRAM_ENABLE_DX12
#undef ENGRAM_ENABLE_CUDA
#undef ENGRAM_ENABLE_ROCM
#undef ENGRAM_ENABLE_OPENCL
#undef ENGRAM_ENABLE_SYCL
#undef ENGRAM_ENABLE_LEVEL_ZERO
#undef ENGRAM_ENABLE_WEBGPU
#undef ENGRAM_ENABLE_XDNA
#undef ENGRAM_ENABLE_DPDK
#undef ENGRAM_ENABLE_OP_TEE
#undef ENGRAM_ENABLE_PMDK
#undef ENGRAM_ENABLE_RDMA
#undef ENGRAM_ENABLE_GPUDIRECT
#undef ENGRAM_ENABLE_DMABUF
#undef ENGRAM_METAL_CPP
#undef ENGRAM_METAL_PRIVATE_IMPL

// monotonic_buffer_resource is a hosted facility, so PMR goes with the heap.
#ifndef ENGRAM_DISABLE_PMR
#define ENGRAM_DISABLE_PMR
#endif

/** @brief Stack budget a freestanding stack arena is checked against (nothing to query). */
#ifndef ENGRAM_FREESTANDING_STACKSZ
#define ENGRAM_FREESTANDING_STACKSZ (64 * 1024)
#endif
#endif

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <span>
#include <string_view>
#include <new>
#include <cstdarg>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifndef ENGRAM_FALLBACK_PAGESZ
#define ENGRAM_FALLBACK_PAGESZ 4096
#endif

#ifndef ENGRAM_DISABLE_PMR
#include <optional>
#include <memory_resource>
#endif

#include <array>

#ifdef ENGRAM_EASY_POP
#ifndef ENGRAM_MAX_ARRAY_STACKSZ
#define ENGRAM_MAX_ARRAY_STACKSZ 64
#endif
#endif

/** @brief Depth of the fixed save-point stack used by @ref engram::arena::save. */
#ifndef ENGRAM_MAX_SAVE_STACKSZ
#define ENGRAM_MAX_SAVE_STACKSZ 32
#endif

#ifndef ENGRAM_ENABLE_FREESTANDING
#ifdef _WIN32
#define ENGRAM_ENABLE_DX12
#elif defined(__unix__) || defined(__UNIX__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/param.h>
#define ENGRAM_UNIX_ENV 1
#endif
#ifdef __APPLE__
#define ENGRAM_ENABLE_METAL 1
#endif
#endif

#ifdef ENGRAM_ALL
#if defined(_WIN32) || defined(__linux__)
#define ENGRAM_ENABLE_VULKAN
#define ENGRAM_ENABLE_CUDA
#define ENGRAM_ENABLE_ROCM
#define ENGRAM_ENABLE_XDNA
#define ENGRAM_ENABLE_OPENCL
#define ENGRAM_ENABLE_SYCL
#define ENGRAM_ENABLE_LEVEL_ZERO
#define ENGRAM_ENABLE_WEBGPU
#define ENGRAM_ENABLE_PMDK
#endif
#ifdef __linux__
#define ENGRAM_ENABLE_DPDK
#define ENGRAM_ENABLE_RDMA
#define ENGRAM_ENABLE_GPUDIRECT
#define ENGRAM_ENABLE_DMABUF
#endif
#endif

#ifdef ENGRAM_ENABLE_VULKAN
#ifndef ENGRAM_VULKAN_HEADER
#include <vulkan/vulkan.h>
#else
#include ENGRAM_VULKAN_HEADER
#endif
#include <unordered_map>
#endif

#ifdef ENGRAM_ENABLE_DX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <unordered_map>
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
#include <unordered_map>
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
#include <unordered_map>
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
#include <unordered_map>
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
#include <unordered_map>
#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO
#ifndef ENGRAM_LEVEL_ZERO_HEADER
#include <level_zero/ze_api.h>
#else
#include ENGRAM_LEVEL_ZERO_HEADER
#endif
#include <unordered_map>
#endif

#ifdef ENGRAM_ENABLE_WEBGPU
#ifndef ENGRAM_WEBGPU_HEADER
#include <webgpu/webgpu.h>
#else
#include ENGRAM_WEBGPU_HEADER
#endif
#include <unordered_map>
#endif

#ifdef ENGRAM_ENABLE_PMDK
#ifndef ENGRAM_PMDK_HEADER
#include <libpmem.h>
#else
#include ENGRAM_PMDK_HEADER
#endif
#include <unordered_map>
#endif

#ifdef ENGRAM_ENABLE_RDMA
#ifndef ENGRAM_RDMA_HEADER
#include <infiniband/verbs.h>
#else
#include ENGRAM_RDMA_HEADER
#endif
#include <unordered_map>
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

#ifdef _MSC_VER
#include <malloc.h>
#define ENGRAM_STACK_ALLOC(sz) _alloca(sz)
#elif defined(__GNUC__)
#define ENGRAM_STACK_ALLOC(sz) __builtin_alloca(sz)
#else
#warning "ENGRAM_STACK_ALLOC is not defined for this compiler; ENGRAM_STACK_ARENA will not work."
#define ENGRAM_STACK_ALLOC(sz) nullptr
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

#ifndef ENGRAM_ENABLE_FREESTANDING
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
#endif

#ifdef ENGRAM_ENABLE_METAL
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

class arena;

/** @brief Where an arena's memory comes from. */
enum class memory_source
{
    stack,          ///< Stack memory (`alloca`), with optional heap fallback.
    heap,           ///< Heap memory (aligned / paged / contiguous / shared).
    external,       ///< A caller-owned buffer the arena does not free.
    custom = 1000   ///< A vendor / device backend (see @ref custom).
};

/** @brief Error state recorded on an arena after a failed creation. */
enum class arena_error
{
    no_error,        ///< No error.
    stack_overflow,  ///< Requested stack size exceeded the thread stack.
    alloc_failed,    ///< The backend allocation failed.
    custom = 1000    ///< Backend-specific error.
};

/** @brief Target cache level for @ref arena::warm_cache and @ref warm_cache. */
enum class cache_locality
{
    Discard,   ///< Non-temporal / streaming (do not retain in cache).
    L3,        ///< Keep in L3.
    L2,        ///< Keep in L2.
    L1         ///< Keep in L1 (highest temporal locality).
};

/** @brief Vendor backend selector for @ref arena::create_custom. */
enum class custom { CUDA, ROCm, Vulkan, DX12, OpenCL, SYCL, LevelZero, WebGPU, DPDK, RDMA, GPUDirect, XDNA, PMDK, Metal, OpTee, DmaBuf };

/** @brief Bit flags controlling allocation behaviour and cache-warm intent. */
namespace flags 
{ 
    constexpr int32_t none = 0;              ///< No flags.
    constexpr int32_t heap_fallback = 1;     ///< Fall back to a heap allocation when the backend allocation fails.
    constexpr int32_t true_contiguous = 2;   ///< Request physically-contiguous / huge pages.
    constexpr int32_t page_aligned = 4;      ///< Round size up to and align on the page size.
    constexpr int32_t commit = 8;            ///< Zero-initialise the storage on creation.
    constexpr int32_t shared = 16;           ///< Map shareable memory.
    constexpr int32_t no_clear = 32;         ///< Do not zero the storage when the arena is freed.
    constexpr int32_t pin_to_physical = 64;  ///< Lock pages into physical RAM (mlock / VirtualLock).
    constexpr int32_t unified = 128;         ///< Use unified / managed memory (device backends).

    constexpr int32_t read = 1;              ///< Cache-warm read intent (@ref warm_cache ioflags).
    constexpr int32_t write = 2;             ///< Cache-warm write intent (@ref warm_cache ioflags).
}

#ifdef ENGRAM_ENABLE_DX12
using Microsoft::WRL::ComPtr;
#endif

namespace vendor {

#ifdef ENGRAM_ENABLE_METAL

inline void free_metal(arena& arena);

#ifdef ENGRAM_METAL_CPP
inline void allocate_metal(arena& arena, MTL::Device* device, int32_t flags);
#else
inline void allocate_metal(arena& arena, id device, int32_t flags);
#endif

#endif

#ifdef ENGRAM_ENABLE_VULKAN
struct vk_mem_tracker
{
	VkDevice& device;
	VkBuffer& buffer;
	VkDeviceMemory& memory;
};
inline std::unordered_map<std::byte*, vk_mem_tracker> vk_mem_info_map;

inline void free_vulkan(arena& arena);
inline void allocate_vulkan(arena& arena, VkDevice& device);
#endif

#ifdef ENGRAM_ENABLE_DX12
inline std::unordered_map<std::byte*, ComPtr<ID3D12Resource>> dx12_mem_info_map;

inline void free_dx12(arena& arena);
inline void allocate_dx12(arena& arena, ComPtr<ID3D12Device> device, D3D12_RESOURCE_FLAGS descflags, D3D12_RESOURCE_STATES resflags , int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_CUDA
inline void free_cuda(arena& arena);
inline void allocate_cuda(arena& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_ROCM
inline void free_rocm(arena& arena);
inline void allocate_rocm(arena& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_OPENCL
inline std::unordered_map<std::byte*, cl_context> opencl_mem_info_map;

inline void free_opencl(arena& arena);
inline void allocate_opencl(arena& arena, cl_context context, cl_svm_mem_flags clflags, cl_uint alignment, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_XDNA
inline std::unordered_map<std::byte*, xrtBufferHandle> xdna_buffer_mapping;
	
inline void free_xdna(arena& arena);
inline void allocate_xdna(xrtDeviceHandle device, xrtBufferFlags xoflags, xrtMemoryGroup group, arena& arena, int32_t flags);
inline xrtBufferHandle get_xrt_buffer_handle(arena& arena);
#endif

#ifdef ENGRAM_ENABLE_DPDK
inline std::unordered_map<std::byte*, const rte_memzone*> dpdk_mem_zone_mapping;

inline void free_dpdk(arena& arena);
inline void allocate_dpdk(std::string_view name, int align, int socketId, unsigned int dpdkFlags, bool useVirtAddr, arena& arena, int32_t flags);
inline const rte_memzone* get_rte_mem_zone(arena& arena);
#endif

#ifdef ENGRAM_ENABLE_OP_TEE

inline void free_op_tee(arena& arena);
inline void allocate_op_tee(arena& arena, uint32_t hint, int32_t flags);

#endif

#ifdef ENGRAM_ENABLE_SYCL
inline std::unordered_map<std::byte*, sycl::queue> sycl_mem_info_map;

inline void free_sycl(arena& arena);
inline void allocate_sycl(arena& arena, sycl::queue& queue, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO
inline std::unordered_map<std::byte*, ze_context_handle_t> level_zero_mem_info_map;

inline void free_level_zero(arena& arena);
inline void allocate_level_zero(arena& arena, ze_context_handle_t context, ze_device_handle_t device, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_WEBGPU
inline std::unordered_map<std::byte*, WGPUBuffer> webgpu_mem_info_map;

inline void free_webgpu(arena& arena);
inline void allocate_webgpu(arena& arena, WGPUDevice device, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_PMDK
inline std::unordered_map<std::byte*, std::pair<std::size_t, bool>> pmdk_map_info;   // { mapped_len, is_pmem }

inline void free_pmdk(arena& arena);
inline void allocate_pmdk(arena& arena, const char* path, int pmdk_flags, mode_t mode, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_RDMA
inline std::unordered_map<std::byte*, ibv_mr*> rdma_mr_map;

inline void free_rdma(arena& arena);
inline void allocate_rdma(arena& arena, void* buffer, ibv_pd* pd, int access, int32_t flags);
inline ibv_mr* get_rdma_mr(arena& arena);
#endif

#ifdef ENGRAM_ENABLE_GPUDIRECT
inline void free_gpudirect(arena& arena);
inline void allocate_gpudirect(arena& arena, int32_t flags);
#endif

#ifdef ENGRAM_ENABLE_DMABUF
inline std::unordered_map<std::byte*, int> dmabuf_fd_map;   // ptr -> dma-buf fd

inline void free_dmabuf(arena& arena);
inline void allocate_dmabuf(arena& arena, int deviceFd, int32_t flags);
#endif

} // namespace vendor

// Platform primitives and the heap backend. `heap_allocate` / `heap_free` need a complete
// `arena`, so the whole block is defined after the class and only declared here.
inline std::size_t get_total_stack_space();
#ifndef ENGRAM_ENABLE_FREESTANDING
inline std::size_t get_page_size();
inline void heap_allocate(arena& arena, int32_t flags, std::size_t alignment = alignof(std::max_align_t), int fd = -1);
inline void heap_free(arena& arena);
#endif

/**
 * @brief Check a request against the calling thread's total stack size.
 * @return `true` if @p size bytes can plausibly be taken from the stack.
 */
inline bool stack_fits(std::size_t size)
{
    return size < get_total_stack_space();
}

#if !defined(ENGRAM_ENABLE_FREESTANDING) || defined(ENGRAM_ENABLE_FSEXTRA)
/**
 * @brief Emit CPU prefetch hints for `sizeof(T)`-aligned data at @p ptr.
 * @tparam T       Pointee type.
 * @param ptr      Address to warm.
 * @param locality Target cache level.
 * @param ioflags  `flags::read` or `flags::write` access-intent hint.
 */
template <typename T>
void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
{
    PrefetchIntoCache(ptr, (ioflags & flags::write) ? 1 : 0, static_cast<int>(locality));
}
#endif

#ifndef ENGRAM_ENABLE_FREESTANDING
/**
 * @brief Page a host range into RAM (`madvise(MADV_WILLNEED)` / `PrefetchVirtualMemory`).
 * @tparam T   Pointee type.
 * @param ptr  Start of the range.
 * @param size Number of elements.
 * @return `true` if the OS accepted the prefetch request.
 */
template <typename T>
bool prefetch(T* ptr, std::size_t size)
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
#endif

/**
 * @brief Move-only bump-pointer allocator that owns one block of memory.
 *
 * @details Create an arena for a target (stack, heap, an external buffer, or a
 * device backend) with one of the static factories, then `push` objects, arrays
 * and strings into it and `pop` them in LIFO order. All storage is reclaimed when
 * the arena is destroyed. Arenas are non-copyable but movable.
 */
class arena
{
private:
    arena() {}

    template <typename T, typename... ArgsT>
	static bool initialize(std::byte* ptr, ArgsT&&... args)
	{
#if defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
        try {
#endif
            if constexpr (std::is_constructible_v<T>)
                new ((void*)ptr) T{ std::forward<ArgsT>(args)... };
            else if constexpr (sizeof...(args) == 0 && !std::is_scalar_v<T> && std::is_default_constructible_v<T>)
                new ((void*)ptr) T{};
#if defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
        }
        catch (...) {
            return false;
        }
#endif

        return true;
	}

    template <typename T>
    static void release(std::byte* ptr)
    {
        if constexpr (!std::is_scalar_v<T> && !std::is_reference_v<T> && std::is_destructible_v<T>)
			((T*)(ptr))->~T();
    }

    // Round a request up to the maximum fundamental alignment; keeps every bump
    // offset a multiple of that quantum so each returned address is aligned for
    // any standard type and pop stays exact without tracking padding.
    static constexpr std::size_t align_up_max(std::size_t n)
    {
        constexpr std::size_t A = alignof(std::max_align_t);
        return (n + (A - 1)) & ~(A - 1);
    }

    std::byte* reserve(std::size_t bytes, bool countable, bool track)
    {
        auto rounded = align_up_max(bytes);
        if (!m_ptr || rounded > m_size - m_offset)
        {
            m_error = arena_error::alloc_failed;
            return nullptr;
        }

        auto slot = m_ptr + m_offset;
        m_offset += rounded;
#ifndef ENGRAM_DISABLE_TRACKING
        if (countable)
        {
            m_count++;
            m_total++;
        }
#else
        (void)countable;
#endif
#ifdef ENGRAM_EASY_POP
        if (track)
            m_array_sizes[m_array_stacksz++] = { slot, bytes };
#else
        (void)track;
#endif
        return slot;
    }

    std::byte* unreserve(std::size_t bytes, bool countable)
    {
        auto rounded = align_up_max(bytes);
        assert(rounded <= m_offset);

        m_offset -= rounded;
#ifndef ENGRAM_DISABLE_TRACKING
        if (countable)
            --m_count;
#else
        (void)countable;
#endif
        return m_ptr + m_offset;
    }

#ifdef ENGRAM_EASY_POP
    std::pair<std::byte*, std::size_t> unreserve_tracked(bool countable)
    {
        auto bytes = m_array_sizes[m_array_stacksz - 1].second;
        assert(align_up_max(bytes) <= m_offset);

        m_offset -= align_up_max(bytes);
#ifndef ENGRAM_DISABLE_TRACKING
        if (countable)
            --m_count;
#else
        (void)countable;
#endif
        --m_array_stacksz;
        return { m_ptr + m_offset, bytes };
    }
#endif

public:

	// The following data members hold the arena's internal state. They are public
	// in the header-only build but are implementation details, not stable API.
	std::byte*  m_ptr = nullptr;
	std::size_t m_offset = 0;
    std::size_t m_size = 0;
#ifndef ENGRAM_DISABLE_TRACKING
    std::size_t m_count = 0, m_total = 0;
#endif
	memory_source m_type;
    arena_error m_error = arena_error::no_error;
    std::size_t m_alignment = alignof(std::max_align_t);

#ifndef ENGRAM_DISABLE_PMR
    std::optional<std::pmr::monotonic_buffer_resource> m_pmr = std::nullopt;
#endif

#ifdef ENGRAM_EASY_POP
    std::array<std::pair<std::byte*, std::size_t>, ENGRAM_MAX_ARRAY_STACKSZ> m_array_sizes;
    std::size_t m_array_stacksz = 0;
#endif

    // One entry per pending save(); restore() rewinds to the newest one.
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
	
	/**
	 * @brief Create an arena backed by @p type.
	 * @param type      Memory source (heap / external / custom). @ref memory_source::stack
	 *                  is not creatable here — use @ref ENGRAM_STACK_ARENA.
	 * @param size      Requested capacity in bytes.
	 * @param flags     Bitwise-OR of @ref flags values.
	 * @param alignment Minimum alignment of the storage.
	 * @param fd        Optional file descriptor for file-backed / unified mappings.
	 * @return The new arena (check @ref is_valid).
	 */
	[[nodiscard]] static arena create(memory_source type, std::size_t size, int32_t flags = 0, 
        std::size_t alignment = alignof(std::max_align_t), int fd = -1)
	{
        if (fd != -1)
            flags |= engram::flags::unified;

		arena result;
        result.m_type = type;
        result.m_size = size;
        result.m_clear_on_free = !(flags & engram::flags::no_clear);

#ifndef ENGRAM_ENABLE_FREESTANDING
        if ((flags & engram::flags::page_aligned) && (type != memory_source::custom))
        {
            auto pagesz = get_page_size();
            if (pagesz > alignment)
                alignment = pagesz;
            size = ((size / pagesz) + 1) * pagesz;
        }
#else
        (void)alignment;
        (void)fd;
#endif
		
		// Stack arenas come from ENGRAM_STACK_ARENA; external and custom from
		// adopt / create_custom.
#ifndef ENGRAM_ENABLE_FREESTANDING
		if (type == memory_source::heap)
		{
			heap_allocate(result, flags, alignment, fd);

			if (result.m_ptr != nullptr) 
			{
				if (flags & engram::flags::commit)
					memset(result.m_ptr, 0, size);
			}
			else result.m_error = arena_error::alloc_failed;
		}
		else
#endif
		{
			assert(false && "engram::arena::create only builds heap arenas");
			result.m_error = arena_error::alloc_failed;
		}
		
		return result;
	}

    /**
     * @brief Adopt stack storage the caller has already reserved.
     *
     * @details Do not call this directly — use @ref ENGRAM_STACK_ARENA. `alloca` memory
     * is released when the function that requested it returns, so the reservation has to
     * happen in the frame that will use the arena.
     * @param storage Stack address to bind, or `nullptr` to record @ref arena_error::stack_overflow.
     * @param size    Size of the reservation in bytes.
     * @param flags   @ref flags (e.g. `commit`, `no_clear`).
     */
    [[nodiscard]] static arena wrap_stack(void* storage, std::size_t size, int32_t flags = 0)
    {
        if (!storage)
        {
            arena result;
            result.m_type = memory_source::stack;
            result.m_error = arena_error::stack_overflow;
            return result;
        }

        auto result = adopt((std::byte*)storage, size, flags);
        result.m_type = memory_source::stack;
        return result;
    }

#ifndef ENGRAM_ENABLE_FREESTANDING
    /**
     * @brief Create a heap arena (page-aligned; optionally physically contiguous).
     * @param size           Capacity in bytes.
     * @param trueContiguous Request contiguous / huge-page memory.
     * @param alignment      Minimum alignment.
     * @param fd             Optional file descriptor for a file-backed mapping.
     */
    [[nodiscard]] static arena heap(std::size_t size, bool trueContiguous = false, std::size_t alignment = alignof(std::max_align_t), int fd = -1)
    {
        return create(memory_source::heap, size, trueContiguous ? engram::flags::true_contiguous | engram::flags::page_aligned : engram::flags::page_aligned, alignment, fd);
    }

#ifdef __linux__
    /**
     * @brief Create a heap arena (page-aligned; optionally physically contiguous).
     * @param size           Capacity in bytes.
     * @param name           Named virtual memory area, Check /proc/%d/maps.
     * @param trueContiguous Request contiguous / huge-page memory.
     * @param alignment      Minimum alignment.
     * @param fd             Optional file descriptor for a file-backed mapping.
     */
    [[nodiscard]] static arena heap(std::size_t size, std::string_view name, bool trueContiguous = false, std::size_t alignment = alignof(std::max_align_t), int fd = -1);
    {
        auto result = create(memory_source::heap, size, trueContiguous ? engram::flags::true_contiguous | engram::flags::page_aligned : 
        engram::flags::page_aligned, alignment, fd);
        if (result.m_impl->m_ptr)
        {
            prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, result.m_impl->m_ptr, size, name.data());
        }
        return result;
    }

    /** @brief Create a heap arena backed by an anonymous `memfd` named @p name (Linux only). */
    [[nodiscard]] static arena heapfile(std::size_t size, std::string_view name, std::size_t alignment = alignof(std::max_align_t))
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
#endif

    /**
     * @brief Adopt a caller-owned buffer; the arena uses but does not free it.
     * @tparam T      Element type of the storage.
     * @param storage Pointer to the buffer.
     * @param size    Size of the buffer in bytes.
     * @param flags   @ref flags (e.g. `commit`, `no_clear`).
     */
    template <typename T>
    [[nodiscard]] static arena adopt(T* storage, std::size_t size, int32_t flags = 0)
    {
        assert(storage != nullptr);

        arena result;

        // Align the adopted base up to the max alignment so bump offsets yield
        // correctly aligned addresses.
        constexpr std::size_t A = alignof(std::max_align_t);
        auto raw = (std::byte*)storage;
        auto misalign = (std::size_t)(reinterpret_cast<std::uintptr_t>(raw) & (A - 1));
        auto adjust = misalign ? (A - misalign) : 0;
        if (adjust >= size)
        {
            raw = nullptr;
            size = 0;
        }
        else
        {
            raw += adjust;
            size -= adjust;
        }

        result.m_ptr = raw;
        result.m_type = memory_source::external;
        result.m_size = size;
        result.m_clear_on_free = !(flags & engram::flags::no_clear);
        if (raw && (flags & engram::flags::commit))
            memset(raw, 0, size);
        return result;
    }

    /** @brief Adopt a caller-owned C array (size deduced from the array bound). */
    template <typename T, std::size_t size>
    [[nodiscard]] static arena adopt(T (&storage)[size], int32_t flags = 0)
    {
        static_assert(size > 0);
        return adopt(storage, size, flags);
    }

#ifndef ENGRAM_ENABLE_FREESTANDING
    /**
     * @brief Create a custom arena by invoking @p func to populate it.
     * @param size   Capacity in bytes.
     * @param func   Callable invoked as `func(arena&, params...)` to allocate the storage.
     * @param params Extra arguments forwarded to @p func.
     */
    [[nodiscard]] static arena create_custom(std::size_t size, auto&& func, auto&&... params)
    {
        arena result;
        result.m_type = memory_source::custom;
        result.m_size = size;

        func(result, std::forward<decltype(params)>(params)...);

        return result;
    }

    /**
     * @brief Create an arena from a vendor / device backend selected by @p type.
     *
     * @details The backend-specific parameters follow @p flags as variadic arguments
     * and are extracted before dispatching to the matching allocator:
     *   - CUDA / ROCm / GPUDirect : `create_custom(size, type, flags)`
     *   - Vulkan   : `(..., VkDevice, VkPhysicalDevice, const VkAllocationCallbacks*, VkDeviceSize offset, VkMemoryMapFlags)`
     *   - DX12     : `(..., ID3D12Device*, D3D12_RESOURCE_FLAGS, D3D12_RESOURCE_STATES)`
     *   - OpenCL   : `(..., cl_context, cl_svm_mem_flags, cl_uint alignment)`
     *   - SYCL     : `(..., sycl::queue* queue)`
     *   - LevelZero: `(..., ze_context_handle_t, ze_device_handle_t)`
     *   - WebGPU   : `(..., WGPUDevice)`
     *   - XDNA     : `(..., xrtDeviceHandle, xrtBufferFlags, xrtMemoryGroup)`
     *   - DPDK     : `(..., const char* name, int align, int socketId, unsigned int dpdkFlags, int useVirtAddr)`
     *   - PMDK     : `(..., const char* path, int pmdk_flags, mode_t mode)`
     *   - RDMA     : `(..., void* buffer, ibv_pd*, int access)`
     *   - OpTee    : `(..., uint32_t hint)`
     *   - Metal    : `(..., MTL::Device* | id device)`
     *   - DmaBuf   : `(..., int deviceFd)` (Linux; deviceFd < 0 opens "/dev/dma_heap/system")
     *
     * @param size  Capacity in bytes.
     * @param type  Backend selector.
     * @param flags @ref flags value(s).
     */
    [[nodiscard]] static arena create_custom(std::size_t size, custom type, int32_t flags, ...)
    {
        arena result;
        result.m_type = memory_source::custom;
        result.m_size = size;

        va_list args;
        va_start(args, flags);

        switch (type)
        {
#ifdef ENGRAM_ENABLE_METAL
            case custom::Metal:
            {
#ifdef ENGRAM_METAL_CPP
                auto device = va_arg(args, MTL::Device*);
#else
                auto device = va_arg(args, id);
#endif
                vendor::allocate_metal(result, device, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_VULKAN
            case custom::Vulkan:
            {
                auto device = va_arg(args, VkDevice);
                auto physicalDevice = va_arg(args, VkPhysicalDevice);
                auto allocCbs = va_arg(args, const VkAllocationCallbacks*);
                auto offset = va_arg(args, VkDeviceSize);
                auto vkflags = va_arg(args, VkMemoryMapFlags);
                vendor::allocate_vulkan(result, device, physicalDevice, allocCbs, offset, vkflags, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_DX12
            case custom::DX12:
            {
                auto device = va_arg(args, ID3D12Device*);
                auto descflags = (D3D12_RESOURCE_FLAGS)va_arg(args, int);
                auto resflags = (D3D12_RESOURCE_STATES)va_arg(args, int);
                vendor::allocate_dx12(result, device, descflags, resflags, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_CUDA
            case custom::CUDA:
                vendor::allocate_cuda(result, flags);
                break;
#endif
#ifdef ENGRAM_ENABLE_ROCM
            case custom::ROCm:
                vendor::allocate_rocm(result, flags);
                break;
#endif
#ifdef ENGRAM_ENABLE_OPENCL
            case custom::OpenCL:
            {
                auto context = va_arg(args, cl_context);
                auto clflags = va_arg(args, cl_svm_mem_flags);
                auto alignment = va_arg(args, cl_uint);
                vendor::allocate_opencl(result, context, clflags, alignment, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_XDNA
            case custom::XDNA:
            {
                auto device = va_arg(args, xrtDeviceHandle);
                auto xoflags = va_arg(args, xrtBufferFlags);
                auto group = va_arg(args, xrtMemoryGroup);
                vendor::allocate_xdna(device, xoflags, group, result, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_DPDK
            case custom::DPDK:
            {
                auto name = va_arg(args, const char*);
                auto align = va_arg(args, int);
                auto socketId = va_arg(args, int);
                auto dpdkFlags = va_arg(args, unsigned int);
                auto useVirtAddr = va_arg(args, int) != 0;
                vendor::allocate_dpdk(name, align, socketId, dpdkFlags, useVirtAddr, result, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_OP_TEE
            case custom::OpTee:
            {
                auto hint = va_arg(args, uint32_t);
                vendor::allocate_op_tee(result, hint, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_SYCL
            case custom::SYCL:
            {
                auto queue = va_arg(args, sycl::queue*);
                vendor::allocate_sycl(result, *queue, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_LEVEL_ZERO
            case custom::LevelZero:
            {
                auto context = va_arg(args, ze_context_handle_t);
                auto device = va_arg(args, ze_device_handle_t);
                vendor::allocate_level_zero(result, context, device, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_WEBGPU
            case custom::WebGPU:
            {
                auto device = va_arg(args, WGPUDevice);
                vendor::allocate_webgpu(result, device, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_PMDK
            case custom::PMDK:
            {
                auto path = va_arg(args, const char*);
                auto pmdk_flags = va_arg(args, int);
                auto mode = va_arg(args, mode_t);
                vendor::allocate_pmdk(result, path, pmdk_flags, mode, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_RDMA
            case custom::RDMA:
            {
                auto buffer = va_arg(args, void*);
                auto pd = va_arg(args, ibv_pd*);
                auto access = va_arg(args, int);
                vendor::allocate_rdma(result, buffer, pd, access, flags);
                break;
            }
#endif
#ifdef ENGRAM_ENABLE_GPUDIRECT
            case custom::GPUDirect:
                vendor::allocate_gpudirect(result, flags);
                break;
#endif
#ifdef ENGRAM_ENABLE_DMABUF
            case custom::DmaBuf:
            {
                auto deviceFd = va_arg(args, int);
                vendor::allocate_dmabuf(result, deviceFd, flags);
                break;
            }
#endif
            default: break;
        }

        va_end(args);
        return result;
    }
#endif

#ifdef ENGRAM_ENABLE_METAL
    /** @brief Create an arena over a shared Metal buffer (Apple). @param device `MTL::Device*`. */
    [[nodiscard]] static arena create_metal(std::size_t size, MTL::Device* device, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_metal, device, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_VULKAN
    /** @brief Create an arena over host-visible Vulkan device memory. */
    [[nodiscard]] static arena create_vulkan(std::size_t size, VkDevice& device, VkPhysicalDevice& physicalDevice, const VkAllocationCallbacks* allocCbs = nullptr, 
        VkDeviceSize offset = 0, VkMemoryMapFlags vkflags = 0, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_vulkan, device, physicalDevice, allocCbs, offset, vkflags, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_DX12
    /** @brief Create an arena over a persistently-mapped DirectX 12 upload buffer (Windows). */
    [[nodiscard]] static arena create_dx12(std::size_t size, ComPtr<ID3D12Device> device, D3D12_RESOURCE_FLAGS descflags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATES resflags = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_dx12, device, descflags, resflags, flags);
    }
#endif
	
#ifdef ENGRAM_ENABLE_CUDA
	/** @brief Create an arena over CUDA memory (`cudaMalloc`, or managed with `flags::unified`). */
	[[nodiscard]] static arena create_cuda(std::size_t size, int32_t flags = 0)
	{
		return create_custom(size, &vendor::allocate_cuda, flags);
	}
#endif
	
#ifdef ENGRAM_ENABLE_ROCM
	/** @brief Create an arena over ROCm/HIP memory (`hipMalloc`, or managed with `flags::unified`). */
	[[nodiscard]] static arena create_rocm(std::size_t size, int32_t flags = 0)
	{
		return create_custom(size, &vendor::allocate_rocm, flags);
	}
#endif

#ifdef ENGRAM_ENABLE_OPENCL
    /** @brief Create an arena over an OpenCL SVM allocation. */
    [[nodiscard]] static arena create_opencl(std::size_t size, cl_context context, cl_svm_mem_flags clflags, cl_uint alignment, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_opencl, context, clflags, alignment, flags);
    }
#endif
	
#ifdef ENGRAM_ENABLE_XDNA
	/** @brief Create an arena over an AMD XDNA (XRT) buffer object. */
	[[nodiscard]] static arena create_xdna(std::size_t size, xrtDeviceHandle device, xrtBufferFlags xoflags, 
        xrtMemoryGroup group, int32_t flags = 0)
	{
		return create_custom(size, &vendor::allocate_xdna, device, xoflags, group, flags);
	}
#endif

#ifdef ENGRAM_ENABLE_DPDK
	/** @brief Create an arena over a DPDK memory zone or `rte_malloc` allocation. */
	[[nodiscard]] static arena create_dpdk(std::size_t size, std::string_view name, int align, int socketId = SOCKET_ID_ANY, 
        unsigned int dpdkFlags = RTE_MEMZONE_ZEROED, bool useVirtAddr = true, int32_t flags = 0)
	{
		return create_custom(size, &vendor::allocate_dpdk, name, align, socketId, dpdkFlags, useVirtAddr, flags);
	}
#endif

#ifdef ENGRAM_ENABLE_OP_TEE
    /** @brief Create an arena in OP-TEE secure-world memory (`TEE_Malloc`). */
    [[nodiscard]] static arena create_op_tee(std::size_t size, uint32_t hint = TEE_MALLOC_FILL_ZERO, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_op_tee, hint, flags);
    }

    /** @brief Alias of @ref create_op_tee for ARM TrustZone terminology. */
    [[nodiscard]] static arena create_arm_trustzone(std::size_t size, uint32_t hint = TEE_MALLOC_FILL_ZERO, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_op_tee, hint, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_SYCL
    /** @brief Create an arena over a SYCL shared USM allocation. */
    [[nodiscard]] static arena create_sycl(std::size_t size, sycl::queue& queue, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_sycl, queue, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_LEVEL_ZERO
    /** @brief Create an arena over a oneAPI Level Zero shared USM allocation. */
    [[nodiscard]] static arena create_level_zero(std::size_t size, ze_context_handle_t context, ze_device_handle_t device, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_level_zero, context, device, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_WEBGPU
    /** @brief Create an arena over a mapped WebGPU buffer. */
    [[nodiscard]] static arena create_webgpu(std::size_t size, WGPUDevice device, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_webgpu, device, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_PMDK
    /** @brief Create an arena over a PMDK persistent-memory file (`pmem_map_file`). */
    [[nodiscard]] static arena create_pmdk(std::size_t size, const char* path, int pmdk_flags = PMEM_FILE_CREATE, 
        mode_t mode = 0666, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_pmdk, path, pmdk_flags, mode, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_RDMA
    /** @brief Register a caller-owned buffer with an RDMA device (`ibv_reg_mr`). */
    [[nodiscard]] static arena create_rdma(std::size_t size, void* buffer, ibv_pd* pd, int access = IBV_ACCESS_LOCAL_WRITE, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_rdma, buffer, pd, access, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_GPUDIRECT
    /** @brief Create a GPU arena registered for GPUDirect Storage (`cuFileBufRegister`). */
    [[nodiscard]] static arena create_gpudirect(std::size_t size, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_gpudirect, flags);
    }
#endif

#ifdef ENGRAM_ENABLE_DMABUF
    /**
     * @brief Create an arena over a Linux dma-buf heap allocation (`DMA_HEAP_IOCTL_ALLOC` + `mmap`).
     * @param deviceFd Open dma-buf heap fd; when negative, `/dev/dma_heap/system` is opened instead.
     */
    [[nodiscard]] static arena create_dmabuf(std::size_t size, int deviceFd = -1, int32_t flags = 0)
    {
        return create_custom(size, &vendor::allocate_dmabuf, deviceFd, flags);
    }
#endif

	arena(const arena&) = delete;            ///< Arenas are non-copyable.
	arena& operator=(const arena&) = delete; ///< Arenas are non-copyable.

    /** @brief Move constructor: transfers ownership and leaves @p other empty. */
    arena(arena&& other) noexcept
    {
        assert(m_ptr == nullptr);
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_offset, other.m_offset);
        std::swap(m_size, other.m_size);
        std::swap(m_error, other.m_error);
        m_type = other.m_type;
        m_extra = other.m_extra;
#ifndef ENGRAM_DISABLE_TRACKING
        m_total = other.m_total;
        m_count = other.m_count;
#endif
        m_use_sys_free = other.m_use_sys_free;
        m_clear_on_free = other.m_clear_on_free;
        m_is_managed = other.m_is_managed;
        m_alignment = other.m_alignment;
#ifdef ENGRAM_EASY_POP
        m_array_sizes = std::move(other.m_array_sizes);
        m_array_stacksz = other.m_array_stacksz;
#endif
        m_save_stack = other.m_save_stack;
        m_save_stacksz = other.m_save_stacksz;
#ifndef ENGRAM_DISABLE_PMR
        m_pmr.reset();
        other.m_pmr.reset();
#endif
    }

    /** @brief Move assignment: transfers ownership and leaves @p other empty. */
    arena& operator=(arena&& other) noexcept
    { 
        assert(m_ptr == nullptr);
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_offset, other.m_offset);
        std::swap(m_size, other.m_size);
        std::swap(m_error, other.m_error);
        m_type = other.m_type;
        m_extra = other.m_extra;
#ifndef ENGRAM_DISABLE_TRACKING
        m_total = other.m_total;
        m_count = other.m_count;
#endif
        m_use_sys_free = other.m_use_sys_free;
        m_clear_on_free = other.m_clear_on_free;
        m_is_managed = other.m_is_managed;
        m_alignment = other.m_alignment;
#ifdef ENGRAM_EASY_POP
        m_array_sizes = std::move(other.m_array_sizes);
        m_array_stacksz = other.m_array_stacksz;
#endif
        m_save_stack = other.m_save_stack;
        m_save_stacksz = other.m_save_stacksz;
#ifndef ENGRAM_DISABLE_PMR
        m_pmr.reset();
        other.m_pmr.reset();
#endif
        return *this;
    }
	
	/** @brief Destroy the arena, reclaiming its storage (runs the backend free hook for device arenas). */
	~arena()
	{
		switch (m_type)
		{
			case memory_source::stack: if (m_ptr && m_clear_on_free) { memset(m_ptr, 0, m_size); } break;
#ifndef ENGRAM_ENABLE_FREESTANDING
			case memory_source::heap: heap_free(*this); break;
#endif
			case memory_source::external: break;
			case memory_source::custom: if (m_extra) ((void(*)(arena&))m_extra)(*this); break;
		}
		
		m_ptr = nullptr;
		m_offset = 0;
#ifndef ENGRAM_DISABLE_TRACKING
		m_count = m_total = 0;
#endif
#ifndef ENGRAM_DISABLE_PMR
        m_pmr = std::nullopt;
#endif
	}

#ifndef ENGRAM_DISABLE_PMR
    /**
     * @brief View the arena's storage as a `std::pmr::monotonic_buffer_resource`.
     * @param upstream Upstream resource used once the arena is exhausted.
     */
    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::pmr::memory_resource* upstream = 
        std::pmr::null_memory_resource())
    {
        if (!m_pmr.has_value())
            m_pmr.emplace(m_ptr + m_offset, m_size - m_offset, upstream);
        return m_pmr.value();
    }

    /**
     * @brief View a sub-range `[start, start+size)` of the arena as a PMR resource.
     * @param start    Byte offset into the arena.
     * @param size     Size of the sub-range in bytes.
     * @param upstream Upstream resource used once the sub-range is exhausted.
     */
    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::size_t start, std::size_t size, 
        std::pmr::memory_resource* upstream = std::pmr::null_memory_resource())
    {
        assert((start + size) <= m_size);
        if (!m_pmr.has_value())
            m_pmr.emplace(m_ptr + start, size, upstream);
        return m_pmr.value();
    }
#endif
	
	/**
	 * @brief Construct a `T` in the arena and return a reference to it.
	 * @tparam T     Type to construct; if `T` is `arena`, a nested arena is created via @ref create.
	 * @tparam ArgsT Constructor (or @ref create) argument types.
	 */
	template <typename T, typename... ArgsT>
	[[nodiscard]] T& push(ArgsT&&... args)
	{
		auto slot = reserve(sizeof(T), true, false);
        if constexpr (!std::is_same_v<T, arena>)
        {
#if !defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
            try {
#endif
		        initialize<T, ArgsT...>(slot, std::forward<ArgsT>(args)...);
                return *reinterpret_cast<T*>(slot);
#if !defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
            } catch (...) {
                unreserve(sizeof(T), true);
                throw;
            }
#endif
        }
        else
            return *new (slot) arena{ create(std::forward<ArgsT>(args)...) };
	}
	
	/**
	 * @brief Reserve a compile-time-sized array of `T` and return it as a `std::span`.
	 * @tparam size Element count (compile-time).
	 * @tparam T    Element type; each element is constructed from @p args when any are given.
	 */
	template <std::size_t size, typename T, typename... ArgsT>
	[[nodiscard]] std::span<T> push_array(ArgsT&&... args)
	{
		static_assert(size > 0);

		auto valptr = reinterpret_cast<T*>(reserve(sizeof(T) * size, true, false));
		if (!valptr)
			return {};
		if constexpr (sizeof...(args) > 0)
		{
			auto ptr = valptr;
			for (std::size_t idx = 0; idx < size; ++idx, ++ptr)
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#if defined(ENGRAM_MASK_EXCEPTIONS)
				if (!initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...))
                {
                    // The truncated array is still live, so it stays counted.
                    unreserve(sizeof(T) * (size - idx), false);
                    return { valptr, idx + 1u };
                }
#else
                try {
                    initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
                } catch (...) {
                    unreserve(sizeof(T) * size, true);
                    throw;
                }
#endif
#else
                initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
#endif
		}
		return { valptr, (std::size_t)size };
	}

    /** @brief Copy a compile-time-sized character array into the arena (NUL-terminated). */
    template <std::size_t size, typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(CharT (&str)[size])
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, false));
        if (!valptr)
            return {};
        Traits::copy(valptr, str, size);
        Traits::assign(valptr[size], CharT{});
        return { valptr, size };
    }

    /** @brief Reserve a `size`-char string filled with @p fillchar (NUL-terminated). */
    template <std::size_t size, typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(CharT fillchar = CharT{})
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, false));
        if (!valptr)
            return {};
        Traits::assign(valptr, size, fillchar);
        Traits::assign(valptr[size], CharT{});
        return { valptr, size };
    }

    /**
     * @brief Reserve a runtime-sized array of `T` and return it as a `std::span`.
     * @param size Element count.
     */
    template <typename T, typename... ArgsT>
	[[nodiscard]] std::span<T> push_array(std::size_t size, ArgsT&&... args)
	{
		assert(size > 0);

		auto valptr = reinterpret_cast<T*>(reserve(sizeof(T) * size, true, true));
		if (!valptr)
			return {};
		if constexpr (sizeof...(args) > 0)
		{
			auto ptr = valptr;
			for (std::size_t idx = 0; idx < size; ++idx, ++ptr)
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#if defined(ENGRAM_MASK_EXCEPTIONS)
				if (!initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...))
                {
                    // The truncated array is still live, so it stays counted.
                    unreserve(sizeof(T) * (size - idx), false);
                    return { valptr, idx + 1u };
                }
#else
                try {
                    initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
                } catch (...) {
                    unreserve(sizeof(T) * size, true);
                    throw;
                }
#endif
#else
                initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
#endif
		}
		return { valptr, (std::size_t)size };
	}

    /** @brief Copy a string view into the arena (NUL-terminated); returns a view of the copy. */
    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(std::basic_string_view<CharT, Traits> str)
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((str.size() + 1) * sizeof(CharT), true, true));
        if (!valptr)
            return {};
        Traits::copy(valptr, str.data(), str.size());
        Traits::assign(valptr[str.size()], CharT{});
        return { valptr, str.size() };
    }

    /** @brief Reserve a runtime-sized string filled with @p fillchar (NUL-terminated). */
    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(std::size_t size, CharT fillchar = CharT{})
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, true));
        if (!valptr)
            return {};
        Traits::assign(valptr, size, fillchar);
        Traits::assign(valptr[size], CharT{});
        return { valptr, size };
    }
	
	/** @brief Destroy and pop the most recently pushed `T`. */
	template <typename T>
	void pop() 
	{ 
		release<T>(unreserve(sizeof(T), true));
	}

    /** @brief Destroy and pop a runtime-sized `T` array (@p size elements). */
    template <typename T>
	void pop_array(std::size_t size) 
    {
		auto valptr = unreserve(sizeof(T) * size, true);
		if constexpr (!std::is_scalar_v<T> && std::is_destructible_v<T>)
			for (std::size_t idx = 0; idx < size; ++idx)
				release<T>(valptr + (idx * sizeof(T)));
    }
	
	/** @brief Destroy and pop a compile-time-sized `T` array. */
	template <std::size_t size, typename T>
	void pop_array() 
	{ 
		pop_array<T>(size);
	}

    /** @brief Destroy and pop the array described by @p sp. */
    template <typename T>
	void pop_array(std::span<T> sp) 
    {
        pop_array<T>(sp.size());
    }

    /** @brief Pop a string previously pushed with @ref push_string. */
    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    void pop_string(std::basic_string_view<CharT, Traits> str)
    {
        unreserve((str.size() + 1) * sizeof(CharT), true);
    }

#ifdef ENGRAM_EASY_POP

    /** @brief Pop the most recently pushed array without re-specifying its size (requires `ENGRAM_EASY_POP`). */
    template <typename T>
	void pop_array() 
	{ 
		auto [valptr, bytes] = unreserve_tracked(true);
		if constexpr (!std::is_scalar_v<T> && std::is_destructible_v<T>)
			for (std::size_t idx = 0; idx < bytes / sizeof(T); ++idx)
				release<T>(valptr + (idx * sizeof(T)));
	}

    /** @brief Pop the most recently pushed string without re-specifying its size (requires `ENGRAM_EASY_POP`). */
    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    void pop_string()
    {
        unreserve_tracked(true);
    }

#endif

    /**
     * @brief Reclaim all storage in O(1), resetting the arena to empty.
     * @details Rewinds the arena and clears the live allocation count without
     * running any destructors, so use it for trivially-destructible data or
     * after you have already popped whatever needs cleanup.
     */
    void reset() noexcept
    {
        m_offset = 0;
#ifndef ENGRAM_DISABLE_TRACKING
        m_count = 0;
#endif
#ifdef ENGRAM_EASY_POP
        m_array_stacksz = 0;
#endif
        m_save_stacksz = 0;
#ifndef ENGRAM_DISABLE_PMR
        m_pmr.reset();
#endif
    }

    /**
     * @brief Push the current head onto the save stack (an implicit sub-arena marker).
     * @details Save points nest LIFO and are held in a fixed array of
     * `ENGRAM_MAX_SAVE_STACKSZ` (default 32) entries; define that macro before
     * including engram.h to change the depth.
     * @return `false` if the save stack is full.
     */
    bool save() noexcept
    {
        if (m_save_stacksz >= m_save_stack.size())
            return false;

        auto& sp = m_save_stack[m_save_stacksz++];
        sp.offset = m_offset;
#ifndef ENGRAM_DISABLE_TRACKING
        sp.count = m_count;
#endif
#ifdef ENGRAM_EASY_POP
        sp.array_stacksz = m_array_stacksz;
#endif
        return true;
    }

    /**
     * @brief Rewind to the most recent @ref save point, discarding everything pushed since.
     * @details Zeroes `[saved offset, current offset)` unless the arena was created
     * with `flags::no_clear` (device / `memory_source::custom` arenas are never
     * host-cleared), then restores the head and the allocation bookkeeping to their
     * saved values. Like @ref reset it runs no destructors.
     * @return `false` if no save point is pending.
     */
    bool restore() noexcept
    {
        if (m_save_stacksz == 0)
            return false;

        const auto& sp = m_save_stack[--m_save_stacksz];
        // Device storage may not be addressable from the host, so never memset it here.
        if (m_ptr && m_clear_on_free && (m_type != memory_source::custom) && (m_offset > sp.offset))
            memset(m_ptr + sp.offset, 0, m_offset - sp.offset);

        m_offset = sp.offset;
#ifndef ENGRAM_DISABLE_TRACKING
        m_count = sp.count;
#endif
#ifdef ENGRAM_EASY_POP
        m_array_stacksz = sp.array_stacksz;
#endif
        return true;
    }

    /** @return Number of save points currently pending. */
    std::size_t save_depth() const noexcept { return m_save_stacksz; }

#ifndef ENGRAM_ENABLE_FREESTANDING
    /** @brief Release pages pinned via `flags::pin_to_physical` (munlock / VirtualUnlock). */
    void unpin()
    {
        if (m_ptr && (m_type == memory_source::heap))
        {
#ifdef __linux__
            munlock(m_ptr, m_size);
#elif _WIN32
            VirtualUnlock(m_ptr, m_size);
#endif
        }
    }

    /**
     * @brief Synchronize a device-backed arena. Extra arguments are backend-specific:
     *   - XDNA      : `sync(bool host_to_device)`
     *   - CUDA/ROCm : `sync()` (device synchronize; managed memory)
     *   - SYCL      : `sync()` (queue wait)
     *   - OpenCL    : `sync(cl_command_queue queue)` (clFinish)
     *   - PMDK      : `sync(size_t start, size_t end)` (persist range)
     *   - GPUDirect : `sync()` (device synchronize)
     * @return `true` on success; host arenas return `false`.
     */
    template <typename... Params>
    bool sync(Params&&... params)
    {
        return sync_va(sizeof...(params), std::forward<Params>(params)...);
    }

    bool sync_va(std::size_t provided, ...)
    {
        va_list args;
        bool ok = false;
        (void)provided;
#ifdef ENGRAM_ENABLE_XDNA
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_xdna))
        {
            va_start(args, provided);
            bool host_to_device = va_arg(args, int) != 0;
            va_end(args);

            auto it = xdna_buffer_mapping.find(m_ptr);
            if (it != xdna_buffer_mapping.end())
                ok = xrtBOSync(it->second, host_to_device ? XCL_BO_SYNC_BO_TO_DEVICE : XCL_BO_SYNC_BO_FROM_DEVICE, 
                    m_size, 0) == 0;
        }
#endif
#ifdef ENGRAM_ENABLE_CUDA
        if (m_ptr && m_is_managed && (m_extra == (void*)&vendor::free_cuda))
            ok = cudaDeviceSynchronize() == cudaSuccess;
#endif
#ifdef ENGRAM_ENABLE_ROCM
        if (m_ptr && m_is_managed && (m_extra == (void*)&vendor::free_rocm))
            ok = hipDeviceSynchronize() == hipSuccess;
#endif
#ifdef ENGRAM_ENABLE_SYCL
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_sycl))
        {
            auto it = vendor::sycl_mem_info_map.find(m_ptr);
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
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_opencl))
        {
            va_start(args, provided);
            cl_command_queue queue = va_arg(args, cl_command_queue);
            va_end(args);

            if (queue)
                ok = clFinish(queue) == CL_SUCCESS;
        }
#endif
#ifdef ENGRAM_ENABLE_PMDK
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_pmdk))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            va_end(args);

            assert(start <= end && end <= m_size);
            auto it = vendor::pmdk_map_info.find(m_ptr);
            if (it != vendor::pmdk_map_info.end())
            {
                if (it->second.second)   // is_pmem: flush + drain
                {
                    pmem_persist(m_ptr + start, end - start);
                    ok = true;
                }
                else                     // not real pmem: fall back to msync
                    ok = pmem_msync(m_ptr + start, end - start) == 0;
            }
        }
#endif
#ifdef ENGRAM_ENABLE_GPUDIRECT
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_gpudirect))
            ok = cudaDeviceSynchronize() == cudaSuccess;
#endif
        (void)args;
        return ok;
    }

    /**
     * @brief Prefetch a range of the arena. Extra arguments are backend-specific:
     *   - Heap        : `prefetch(size_t start, size_t size)` (madvise / PrefetchVirtualMemory)
     *   - CUDA/ROCm   : `prefetch(size_t start, size_t end, int device)`
     *   - SYCL        : `prefetch(size_t start, size_t end)`
     *   - OpenCL      : `prefetch(size_t start, size_t end, cl_command_queue queue)`
     *   - RDMA        : `prefetch(size_t start, size_t end)` (ibv_advise_mr)
     * @return `true` if the prefetch was issued.
     */
    template <typename... Params>
    bool prefetch(Params&&... params)
    {
        return prefetch_va(sizeof...(params), std::forward<Params>(params)...);
    }

    bool prefetch_va(std::size_t provided, ...)
    {
        va_list args;
        bool ok = false;
        (void)provided;

        if (m_ptr && m_type == memory_source::heap)
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t size = va_arg(args, std::size_t);
            va_end(args);

            assert(start + size <= m_size);
            ok = engram::prefetch(m_ptr + start, size);
        }
#ifdef ENGRAM_ENABLE_CUDA
        if (m_ptr && m_is_managed && (m_extra == (void*)&vendor::free_cuda))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            int device = va_arg(args, int);
            va_end(args);

            assert(start <= end && end <= m_size);
            ok = cudaMemPrefetchAsync(m_ptr + start, end - start, device) == cudaSuccess;
        }
#endif
#ifdef ENGRAM_ENABLE_ROCM
        if (m_ptr && m_is_managed && (m_extra == (void*)&vendor::free_rocm))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            int device = va_arg(args, int);
            va_end(args);

            assert(start <= end && end <= m_size);
            ok = hipMemPrefetchAsync(m_ptr + start, end - start, device) == hipSuccess;
        }
#endif
#ifdef ENGRAM_ENABLE_SYCL
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_sycl))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            va_end(args);

            assert(start <= end && end <= m_size);
            auto it = vendor::sycl_mem_info_map.find(m_ptr);
            if (it != vendor::sycl_mem_info_map.end())
            {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
                try { it->second.prefetch(m_ptr + start, end - start); ok = true; }
                catch (...) { ok = false; }
#else
                it->second.prefetch(m_ptr + start, end - start);
                ok = true;
#endif
            }
        }
#endif
#ifdef ENGRAM_ENABLE_OPENCL
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_opencl))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            cl_command_queue queue = va_arg(args, cl_command_queue);
            va_end(args);

            assert(start <= end && end <= m_size);
            if (queue)
            {
                const void* ptrs[1] = { m_ptr + start };
                std::size_t sizes[1] = { end - start };
                ok = clEnqueueSVMMigrateMem(queue, 1, ptrs, sizes, 0, 0, nullptr, nullptr) == CL_SUCCESS;
            }
        }
#endif
#ifdef ENGRAM_ENABLE_RDMA
        if (m_ptr && (m_type == memory_source::custom) && (m_extra == (void*)&vendor::free_rdma))
        {
            va_start(args, provided);
            std::size_t start = va_arg(args, std::size_t);
            std::size_t end = va_arg(args, std::size_t);
            va_end(args);

            assert(start <= end && end <= m_size);
            auto it = vendor::rdma_mr_map.find(m_ptr);
            if (it != vendor::rdma_mr_map.end())
            {
                ibv_sge sg{};
                sg.addr = (uintptr_t)(m_ptr + start);
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
#endif

#if !defined(ENGRAM_ENABLE_FREESTANDING) || defined(ENGRAM_ENABLE_FSEXTRA)
    /**
     * @brief Emit CPU prefetch hints over a range of the arena's storage.
     * @param locality Target cache level.
     * @param ioflags  `flags::read` / `flags::write` access intent.
     * @param start    Byte offset to start from.
     * @param size     Number of bytes (0 = to the end of the arena).
     */
    void warm_cache(cache_locality locality, int32_t ioflags, std::size_t start = 0, std::size_t size = 0)
    {
        if (m_ptr && (m_type != memory_source::custom))
        {
            size = (size == 0) ? m_size - start : size;
            assert(start + size <= m_size);

            for (auto idx = start; idx < start + size; idx++)
                PrefetchIntoCache(m_ptr + idx, (ioflags & flags::write) ? 1 : 0, 
                    static_cast<int>(locality));
        }
    }

    /** @brief Warm the `sizeof(T)` bytes at @p ptr (which must lie within this arena). */
    template <typename T>
    void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
    {
        if (m_ptr && (m_type != memory_source::custom))
        {
            assert((std::byte*)ptr >= m_ptr && (std::byte*)ptr < m_ptr + m_size);
            PrefetchIntoCache(ptr, (ioflags & flags::write) ? 1 : 0, static_cast<int>(locality));
        }
    }
#endif

    bool is_valid() const noexcept { return m_ptr != nullptr; }   ///< @return `true` if the arena holds valid storage.
    bool empty() const noexcept { return m_offset == 0; }         ///< @return `true` if nothing has been pushed yet.

    std::size_t used() const noexcept { return m_offset; }        ///< @return Bytes currently in use.
    std::size_t capacity() const noexcept { return m_size; }      ///< @return Total capacity in bytes.
    std::size_t remaining() const noexcept { return m_size - m_offset; } ///< @return Bytes still available.
#ifndef ENGRAM_DISABLE_TRACKING
    std::size_t count() const noexcept { return m_count; }        ///< @return Live allocation count (every `push*` that has not been popped).
    std::size_t total() const noexcept { return m_total; }        ///< @return Lifetime allocation count.
#else
    std::size_t count() const noexcept { return 0; }              ///< @return 0 (tracking disabled).
    std::size_t total() const noexcept { return 0; }              ///< @return 0 (tracking disabled).
#endif
    memory_source source() const noexcept { return m_type; }      ///< @return The arena's @ref memory_source.
    arena_error error() const noexcept { return m_error; }        ///< @return The error recorded during creation.

    /**
     * @brief Access the arena's underlying storage as a contiguous byte range.
     * @return A `std::span<std::byte>` over the whole managed block `[base, capacity)`,
     *         or an empty span if the arena holds no storage.
     */
    std::span<std::byte> data() const noexcept
    {
        return m_ptr ? std::span<std::byte>{ m_ptr, m_size } : std::span<std::byte>{};
    }

    /**
     * @brief Carve out a sub-arena over a fixed region of this arena's storage.
     *
     * @details The returned arena is a @ref memory_source::external view of
     * `[start, start + size)` within this arena; it does **not** own the memory, so
     * destroying it leaves the parent untouched. This lets you hand an independent
     * slice to a child function or a worker thread with no extra allocation. The
     * caller is responsible for choosing non-overlapping regions.
     * @param start Byte offset of the region within this arena.
     * @param size  Size of the region in bytes.
     * @param flags @ref flags controlling initialisation: the region is zeroed by
     *              default (and with `commit`); pass `no_clear` to skip zeroing.
     * @return A sub-arena bound to `[start, start + size)`.
     */
    [[nodiscard]] arena partition(std::size_t start, std::size_t size, int32_t flags = 0)
    {
        assert((start + size) <= m_size);
        return adopt(m_ptr + start, size, (flags & engram::flags::no_clear) ? 0 : engram::flags::commit);
    }
};

#ifdef ENGRAM_ENABLE_FREESTANDING

// Nothing to query without an OS, so stack arenas are sized against a compile-time budget.
inline std::size_t get_total_stack_space()
{
    return ENGRAM_FREESTANDING_STACKSZ;
}

#elif defined(ENGRAM_UNIX_ENV)

inline std::size_t get_total_stack_space()
{
	pthread_attr_t attr;
    void *stack_base;
    std::size_t stack_size;

    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_base, &stack_size);
    pthread_attr_destroy(&attr);
	
	return stack_size;
}

inline std::size_t get_page_size()
{
    auto pagesz = sysconf(_SC_PAGESIZE);
    return pagesz == -1 ? ENGRAM_FALLBACK_PAGESZ : pagesz;
}

inline std::pair<std::byte*, bool> heap_allocate_impl(std::size_t size, int32_t flags, std::size_t alignment, int fd = -1)
{
    auto shared = (flags & (engram::flags::shared | engram::flags::unified)) != 0;
    auto contiguous = (flags & engram::flags::true_contiguous) != 0;

	if (shared || contiguous)
	{
        auto fd = 
#ifdef __linux__
		    (flags & engram::flags::unified) ? fd : -1;
#else
            -1;
#endif
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
		void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, 
#ifdef __APPLE__
            contiguous ? VM_FLAGS_SUPERPAGE_SIZE_2MB : -1, 
#else
            fd,
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

inline std::size_t get_total_stack_space()
{
	ULONG_PTR low_limit, high_limit;
    GetCurrentThreadStackLimits(&low_limit, &high_limit);
    auto pagesz = high_limit - low_limit;
    return pagesz > 0 ? pagesz : ENGRAM_FALLBACK_PAGESZ;
}

inline std::size_t get_page_size()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

inline void* allocate_contiguous(std::size_t size)
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

inline std::pair<std::byte*, bool> heap_allocate_impl(std::size_t size, int32_t flags, std::size_t alignment, int fd = -1)
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

#ifndef ENGRAM_ENABLE_FREESTANDING
inline void heap_allocate(arena& arena, int32_t flags, std::size_t alignment, int fd)
{
	std::tie(arena.m_ptr, arena.m_use_sys_free) = heap_allocate_impl(arena.m_size, flags, alignment, fd);
    arena.m_alignment = alignment;
    if (arena.m_ptr == nullptr)
        arena.m_error = arena_error::alloc_failed;
    else
        arena.m_type = memory_source::heap;
}

inline void heap_free(arena& arena)
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

inline void handle_heap_fallback(arena& arena, int32_t flags)
{
    if ((arena.m_ptr == nullptr) && (flags & engram::flags::heap_fallback))
		heap_allocate(arena, flags);
    else
    {
		arena.m_ptr = nullptr;
        arena.m_error = arena_error::alloc_failed;
    }
}
#endif

namespace vendor {

#ifdef ENGRAM_ENABLE_METAL

#ifdef ENGRAM_METAL_CPP
inline std::unordered_map<std::byte*, MTL::Buffer*> metal_mem_info_map;

inline void free_metal(arena& arena)
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

inline void allocate_metal(arena& arena, MTL::Device* device, int32_t flags)
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

inline std::unordered_map<std::byte*, std::pair<id, id>> metal_mem_info_map;

inline void free_metal(arena& arena)
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

inline void allocate_metal(arena& arena, id device, int32_t flags)
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

inline std::optional<uint32_t> vk_find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) 
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;

    return std::nullopt;
}

inline void free_vulkan(arena& arena)
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

inline void allocate_vulkan(arena& arena, VkDevice& device, VkPhysicalDevice& physicalDevice, const VkAllocationCallbacks* allocCbs = nullptr, 
    VkDeviceSize offset = 0, VkMemoryMapFlags vkflags = 0, int32_t flags = 0)
{
	VkBuffer buffer;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = BUFFER_SIZE;
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

inline void free_dx12(arena& arena)
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

inline void allocate_dx12(arena& arena, ComPtr<ID3D12Device> device, D3D12_RESOURCE_FLAGS descflags, 
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

inline void free_cuda(arena& arena) 
{ 
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
			cudaFree(arena.m_ptr);
		else
			heap_free(arena);
}

inline void allocate_cuda(arena& arena, int32_t flags)
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

inline void free_rocm(arena& arena) 
{ 
	if (arena.m_ptr)
		if (arena.m_type == memory_source::custom)
			hipFree(arena.m_ptr);
		else
			heap_free(arena);
}

inline void allocate_rocm(arena& arena, int32_t flags)
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

inline void free_opencl(arena& arena)
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

inline void allocate_opencl(arena& arena, cl_context context, cl_svm_mem_flags clflags, cl_uint alignment, int32_t flags)
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
	
inline void free_xdna(arena& arena) 
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

inline void allocate_xdna(xrtDeviceHandle device, xrtBufferFlags xoflags, xrtMemoryGroup group, arena& arena, int32_t flags)
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

inline xrtBufferHandle get_xrt_buffer_handle(arena& arena)
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

inline void free_dpdk(arena& arena)
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

inline void allocate_dpdk(std::string_view name, int align, int socketId, unsigned int dpdkFlags, bool useVirtAddr, arena& arena, int32_t flags)
{
	if (flags & engram::flags::true_contiguous)
	{
		auto memzone = rte_memzone_reserve(name.c_str(), arena.m_size, socketId, dpdkFlags);
		arena.m_ptr = useVirtAddr ? (std::byte*)memzone->addr : (std::byte*)memzone->iova;
		dpdk_mem_zone_mapping.emplace(arena.m_ptr, memzone);
	}
	else
		arena.m_ptr = (std::byte*)rte_malloc(name.c_str(), arena.m_size, align);
		
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

#endif

#ifdef ENGRAM_ENABLE_OP_TEE

inline void free_op_tee(arena& arena)
{
    if (arena.m_ptr)
        if (arena.m_type == memory_source::custom)
            TEE_Free(arena.m_ptr);
        else
            heap_free(arena);
}

inline void allocate_op_tee(arena& arena, uint32_t hint, int32_t flags)
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

inline void free_sycl(arena& arena)
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

inline void allocate_sycl(arena& arena, sycl::queue& queue, int32_t flags)
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

inline void free_level_zero(arena& arena)
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

inline void allocate_level_zero(arena& arena, ze_context_handle_t context, ze_device_handle_t device, int32_t flags)
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

inline void free_webgpu(arena& arena)
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

inline void allocate_webgpu(arena& arena, WGPUDevice device, int32_t flags)
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

inline void free_pmdk(arena& arena)
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

inline void allocate_pmdk(arena& arena, const char* path, int pmdk_flags, mode_t mode, int32_t flags)
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

inline void free_rdma(arena& arena)
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

inline void allocate_rdma(arena& arena, void* buffer, ibv_pd* pd, int access, int32_t flags)
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

inline ibv_mr* get_rdma_mr(arena& arena)
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

inline void free_gpudirect(arena& arena)
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

inline void allocate_gpudirect(arena& arena, int32_t flags)
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

inline void free_dmabuf(arena& arena)
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

inline void allocate_dmabuf(arena& arena, int deviceFd, int32_t flags)
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

#define ENGRAM_STACK_ARENA_EXPAND_(x) x
#define ENGRAM_STACK_ARENA_IMPL_(varname, size, flags, ...)                        \
    const std::size_t varname##_engram_size_ = (std::size_t)(size);               \
    void* varname##_engram_storage_ = engram::stack_fits(varname##_engram_size_)   \
        ? ENGRAM_STACK_ALLOC(varname##_engram_size_)                               \
        : nullptr;                                                                 \
    engram::arena varname = engram::arena::wrap_stack(                             \
        varname##_engram_storage_, varname##_engram_size_, (flags))

/**
 * @brief Declare a stack-backed arena named @p varname in the current scope.
 *
 * @details `alloca` storage belongs to the frame that requests it, so this has to be
 * a macro: a factory function would hand back a pointer into its own dead frame. The
 * arena is therefore usable only within the enclosing scope, and its storage is gone
 * once that scope exits.
 *
 * Usage: `ENGRAM_STACK_ARENA(scratch, 4096)` or
 * `ENGRAM_STACK_ARENA(scratch, 4096, engram::flags::commit)`.
 *
 * The request is checked against the thread's stack size first; if it does not fit,
 * @p varname is an invalid arena reporting @ref engram::arena_error::stack_overflow
 * rather than a smashed stack. Two extra names (`<varname>_engram_size_` and
 * `<varname>_engram_storage_`) are declared alongside it, so the macro needs a block
 * scope rather than a bare `if` branch.
 */
#define ENGRAM_STACK_ARENA(varname, ...) \
    ENGRAM_STACK_ARENA_EXPAND_(ENGRAM_STACK_ARENA_IMPL_(varname, __VA_ARGS__, engram::flags::none, 0))

#endif // ENGRAM_H_INCLUDED
