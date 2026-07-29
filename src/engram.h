#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <utility>
#include <type_traits>
#include <span>
#include <string_view>
#include <new>
#include <cstdarg>
#include <stdlib.h>
#include <assert.h>

#ifndef ENGRAM_DISABLE_PMR
#include <optional>
#include <memory_resource>
#endif

#ifdef ENGRAM_EASY_POP
#include <array>
#ifndef ENGRAM_MAX_ARRAY_STACKSZ
#define ENGRAM_MAX_ARRAY_STACKSZ 64
#endif
#endif

#ifdef _WIN32
#define ENGRAM_ENABLE_DX12
#elif defined(__unix__) || defined(__UNIX__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/param.h>
#define ENGRAM_UNIX_ENV 1
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
#endif
#endif

namespace engram {

enum class memory_source { stack, heap, external, custom = 1000 };
enum class arena_error { no_error, stack_overflow, alloc_failed, custom = 1000 };
enum class cache_locality { Discard, L3, L2, L1 };
enum class custom { CUDA, ROCm, Vulkan, DX12, OpenCL, SYCL, LevelZero, WebGPU, DPDK, RDMA, GPUDirect, XDNA, PMDK, Metal, OpTee };
namespace flags 
{ 
    constexpr int32_t none = 0; 
    constexpr int32_t heap_fallback = 1;
    constexpr int32_t true_contiguous = 2;
    constexpr int32_t page_aligned = 4;
    constexpr int32_t commit = 8;
    constexpr int32_t shared = 16;
    constexpr int32_t no_clear = 32;
    constexpr int32_t pin_to_physical = 64;
    constexpr int32_t unified = 128;

    constexpr int32_t read = 1;
    constexpr int32_t write = 2;
}

// General-purpose, backend-agnostic prefetch helpers. They operate on any host
// pointer (heap allocation, arena storage, etc.) and are implemented in engram.cpp.
void warm_cache(std::byte* ptr, std::size_t size, cache_locality locality, int32_t ioflags);
bool prefetch(std::byte* ptr, std::size_t size);

template <typename T>
void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
{
    warm_cache((std::byte*)ptr, sizeof(T), locality, ioflags);
}

template <typename T>
bool prefetch(T* ptr, std::size_t size)
{
    return prefetch((std::byte*)ptr, size * sizeof(T));
}

// Opaque implementation storage. The full definition lives in engram.cpp so that
// no allocation details (buffer, offsets, PMR resource, vendor bookkeeping) leak
// into this header (PIMPL idiom).
struct impl_data;

class arena
{
private:
    arena();

    template <typename T, typename... ArgsT>
	static void initialize(std::byte* ptr, ArgsT&&... args)
	{
		if constexpr (std::is_constructible_v<T>)
			new ((void*)ptr) T{ std::forward<ArgsT>(args)... };
		else if constexpr (sizeof...(args) == 0 && !std::is_scalar_v<T> && std::is_default_constructible_v<T>)
			new ((void*)ptr) T{};
	}

    template <typename T>
    static void release(std::byte* ptr)
    {
        if constexpr (!std::is_scalar_v<T> && !std::is_reference_v<T> && std::is_destructible_v<T>)
			((T*)(ptr))->~T();
    }

    // ---- PIMPL bookkeeping helpers (defined in engram.cpp) ------------------
    // The templated push/pop/string helpers below never touch m_impl directly;
    // they obtain a pointer and update the internal bookkeeping through these.

    // Base address of the managed buffer (m_impl->m_ptr).
    std::byte* base_ptr() const;

    // Reserve `bytes` at the current head and return its address. Advances the
    // offset; optionally bumps the allocation counters and (for ENGRAM_EASY_POP)
    // records the block so it can be popped without re-specifying its size.
    std::byte* reserve(std::size_t bytes, bool countable, bool track);

    // Release `bytes` from the head and return the address of the freed block
    // (post-retreat head) so the caller can run destructors.
    std::byte* unreserve(std::size_t bytes, bool countable);

#ifdef ENGRAM_EASY_POP
    // Pop the most recently tracked block: returns { address, byte-size }.
    std::pair<std::byte*, std::size_t> unreserve_tracked(bool countable);
#endif

    // Build an arena that adopts external storage (defined in engram.cpp).
    static arena make_external(std::byte* storage, std::size_t size, int32_t flags);

public:

    impl_data* m_impl = nullptr;
	
	[[nodiscard]] static arena create(memory_source type, std::size_t size, int32_t flags = 0, 
        std::size_t alignment = alignof(std::max_align_t), int fd = -1);

    [[nodiscard]] static arena stack(std::size_t size, bool fallbackToHeap)
    {
        return create(memory_source::stack, size, fallbackToHeap ? engram::flags::heap_fallback : 0);
    }

    [[nodiscard]] static arena heap(std::size_t size, bool trueContiguous, std::size_t alignment = alignof(std::max_align_t), int fd = -1)
    {
        return create(memory_source::heap, size, trueContiguous ? engram::flags::true_contiguous | engram::flags::page_aligned : engram::flags::page_aligned, alignment, fd);
    }

#ifdef __linux__
    [[nodiscard]] static arena heap(std::size_t size, std::string_view name, std::size_t alignment = alignof(std::max_align_t));
#endif

    template <typename T>
    [[nodiscard]] static arena adopt(T* storage, std::size_t size, int32_t flags = 0)
    {
        assert(storage != nullptr);
        return make_external((std::byte*)storage, size, flags);
    }

    template <typename T, std::size_t size>
    [[nodiscard]] static arena adopt(T (&storage)[size], int32_t flags = 0)
    {
        static_assert(size > 0);
        return adopt(storage, size, flags);
    }

    // Allocate through a vendor backend selected by `type`. The remaining
    // backend-specific parameters are passed as variadic arguments and are
    // extracted in engram.cpp before dispatching to the matching allocator:
    //   CUDA / ROCm / GPUDirect : create_custom(size, type, flags)
    //   Vulkan   : (..., VkDevice, VkPhysicalDevice, const VkAllocationCallbacks*, VkDeviceSize offset, VkMemoryMapFlags)
    //   DX12     : (..., ID3D12Device*, D3D12_RESOURCE_FLAGS, D3D12_RESOURCE_STATES)
    //   OpenCL   : (..., cl_context, cl_svm_mem_flags, cl_uint alignment)
    //   SYCL     : (..., sycl::queue* queue)
    //   LevelZero: (..., ze_context_handle_t, ze_device_handle_t)
    //   WebGPU   : (..., WGPUDevice)
    //   XDNA     : (..., xrtDeviceHandle, xrtBufferFlags, xrtMemoryGroup)
    //   DPDK     : (..., const char* name, int align, int socketId, unsigned int dpdkFlags, int useVirtAddr)
    //   PMDK     : (..., const char* path, int pmdk_flags, mode_t mode)
    //   RDMA     : (..., void* buffer, ibv_pd*, int access)
    //   OpTee    : (..., uint32_t hint)
    //   Metal    : (..., MTL::Device* | id device)
    [[nodiscard]] static arena create_custom(std::size_t size, custom type, int32_t flags, ...);

	arena(const arena&) = delete;
	arena& operator=(const arena&) = delete;

    arena(arena&& other) noexcept
    {
        std::swap(m_impl, other.m_impl);
    }

    arena& operator=(arena&& other) noexcept
    { 
        std::swap(m_impl, other.m_impl);
        return *this;
    }
	
	~arena();

#ifndef ENGRAM_DISABLE_PMR
    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::pmr::memory_resource* upstream = 
        std::pmr::null_memory_resource());

    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::size_t start, std::size_t size, 
        std::pmr::memory_resource* upstream = std::pmr::null_memory_resource());
#endif
	
	template <typename T, typename... ArgsT>
	[[nodiscard]] T& push(ArgsT&&... args)
	{
		auto slot = reserve(sizeof(T), false, false);
        if constexpr (!std::is_same_v<T, arena>)
        {
		    initialize<T, ArgsT...>(slot, std::forward<ArgsT>(args)...);
            return *reinterpret_cast<T*>(slot);
        }
        else
            return *new (slot) arena{ create(std::forward<ArgsT>(args)...) };
	}
	
	template <std::size_t size, typename T, typename... ArgsT>
	[[nodiscard]] std::span<T> push_array(ArgsT&&... args)
	{
		static_assert(size > 0);

		auto valptr = reinterpret_cast<T*>(reserve(sizeof(T) * size, true, false));
		if constexpr (sizeof...(args) > 0)
		{
			auto ptr = valptr;
			for (std::size_t idx = 0; idx < size; ++idx, ++ptr)
				initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
		}
		return { valptr, (std::size_t)size };
	}

    template <std::size_t size, typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(CharT (&str)[size])
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, false));
        std::memcpy(valptr, str, size * sizeof(CharT));
        valptr[size] = CharT{};
        return { valptr, size };
    }

    template <std::size_t size, typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(CharT fillchar = CharT{})
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, false));
        std::memset(valptr, fillchar, size * sizeof(CharT));
        valptr[size] = CharT{};
        return { valptr, size };
    }

    template <typename T, typename... ArgsT>
	[[nodiscard]] std::span<T> push_array(std::size_t size, ArgsT&&... args)
	{
		assert(size > 0);

		auto valptr = reinterpret_cast<T*>(reserve(sizeof(T) * size, true, true));
		if constexpr (sizeof...(args) > 0)
		{
			auto ptr = valptr;
			for (std::size_t idx = 0; idx < size; ++idx, ++ptr)
				initialize<T>((std::byte*)ptr, std::forward<ArgsT>(args)...);
		}
		return { valptr, (std::size_t)size };
	}

    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(std::basic_string_view<CharT, Traits> str)
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((str.size() + 1) * sizeof(CharT), true, true));
        std::memcpy(valptr, str.data(), str.size() * sizeof(CharT));
        valptr[str.size()] = CharT{};
        return { valptr, str.size() };
    }

    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(std::size_t size, CharT fillchar = CharT{})
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true, true));
        std::memset(valptr, fillchar, size * sizeof(CharT));
        valptr[size] = CharT{};
        return { valptr, size };
    }
	
	template <typename T>
	void pop() 
	{ 
		release<T>(unreserve(sizeof(T), true));
	}

    template <typename T>
	void pop_array(std::size_t size) 
    {
		auto valptr = unreserve(sizeof(T) * size, true);
		if constexpr (!std::is_scalar_v<T> && std::is_destructible_v<T>)
			for (std::size_t idx = 0; idx < size; ++idx)
				release<T>(valptr + (idx * sizeof(T)));
    }
	
	template <int size, typename T>
	void pop_array() 
	{ 
		pop_array<T>(size);
	}

    template <typename T>
	void pop_array(std::span<T> sp) 
    {
        pop_array<T>(sp.size());
    }

    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    void pop_string(std::basic_string_view<CharT, Traits> str)
    {
        unreserve((str.size() + 1) * sizeof(CharT), true);
    }

#ifdef ENGRAM_EASY_POP

    template <typename T>
	void pop_array() 
	{ 
		auto [valptr, bytes] = unreserve_tracked(true);
		if constexpr (!std::is_scalar_v<T> && std::is_destructible_v<T>)
			for (std::size_t idx = 0; idx < bytes / sizeof(T); ++idx)
				release<T>(valptr + (idx * sizeof(T)));
	}

    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    void pop_string()
    {
        unreserve_tracked(true);
    }

#endif

    void unpin();

    // C-style variadic sync. The vendor-specific arguments (if any) are pulled
    // from the va_list inside the branch matched via the backend's free hook.
    //   XDNA : sync(bool host_to_device)
    //   CUDA : sync()          (device synchronize)
    //   ROCm : sync()
    //   SYCL : sync()          (queue wait)
    //   OpenCL : sync(cl_command_queue queue)   (clFinish)
    //   PMDK : sync(size_t start, size_t end)    (persist range)
    //   GPUDirect : sync()      (device synchronize)
    bool sync(...);

    // C-style variadic prefetch. Arguments are extracted from the va_list per the
    // matched backend.
    //   Heap        : prefetch(size_t start, size_t size)   (madvise / PrefetchVirtualMemory)
    //   CUDA / ROCm : prefetch(size_t start, size_t end, int device)
    //   SYCL        : prefetch(size_t start, size_t end)
    //   OpenCL      : prefetch(size_t start, size_t end, cl_command_queue queue)
    //   RDMA        : prefetch(size_t start, size_t end)   (ibv_advise_mr)
    bool prefetch(...);

    void warm_cache(cache_locality locality, int32_t ioflags, std::size_t start = 0, std::size_t size = 0);

    template <typename T>
    void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
    {
        if (auto base = base_ptr())
            warm_cache(locality, ioflags, (std::size_t)((std::byte*)ptr - base), sizeof(T));
    }

    bool is_valid() const;
    bool empty() const;

    std::size_t used() const;
    std::size_t capacity() const;
    std::size_t remaining() const;
    std::size_t count() const;
    std::size_t total() const;
    memory_source source() const;
};

} // namespace engram
