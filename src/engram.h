#pragma once

/**
 * @file engram.h
 * @brief engram — a move-only bump-allocation `arena` over stack, heap, external,
 *        and GPU / accelerator device memory (PIMPL header + source build).
 */

// This build keeps arena state in a heap-allocated impl_data, so it cannot run
// without a heap. Freestanding targets need single_header/engram.h instead.
#if defined(ENGRAM_ENABLE_FREESTANDING) || (defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0))
#error "engram: the header + source build requires a hosted implementation - arena state is heap-allocated through the PIMPL pointer. Use single_header/engram.h, which supports ENGRAM_ENABLE_FREESTANDING."
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <type_traits>
#include <span>
#include <string_view>
#include <new>
#include <array>
#include <stdlib.h>
#include <assert.h>

#ifndef ENGRAM_DISABLE_PMR
#include <memory_resource>
#endif

#ifdef ENGRAM_EASY_POP
#ifndef ENGRAM_MAX_ARRAY_STACKSZ
#define ENGRAM_MAX_ARRAY_STACKSZ 64
#endif
#endif

/** @brief Depth of the fixed save-point stack used by @ref engram::arena::save. */
#ifndef ENGRAM_MAX_SAVE_STACKSZ
#define ENGRAM_MAX_SAVE_STACKSZ 32
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
#define ENGRAM_ENABLE_DMABUF
#endif
#endif

namespace engram {

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
    constexpr int32_t heap_fallback = 1;     ///< Stack arenas fall back to the heap when too large.
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

/**
 * @brief Emit CPU prefetch hints for a host memory range.
 * @param ptr      Start of the range (any host pointer).
 * @param size     Number of bytes to warm.
 * @param locality Target cache level.
 * @param ioflags  `flags::read` or `flags::write` access-intent hint.
 */
void warm_cache(std::byte* ptr, std::size_t size, cache_locality locality, int32_t ioflags);

/**
 * @brief Page a host memory range into RAM (`madvise(MADV_WILLNEED)` / `PrefetchVirtualMemory`).
 * @param ptr  Start of the range.
 * @param size Number of bytes.
 * @return `true` if the OS accepted the prefetch request.
 */
bool prefetch(std::byte* ptr, std::size_t size);

/** @brief Typed overload of @ref warm_cache; warms `sizeof(T)` bytes at @p ptr. */
template <typename T>
void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
{
    warm_cache((std::byte*)ptr, sizeof(T), locality, ioflags);
}

/** @brief Typed overload of @ref prefetch; warms `size * sizeof(T)` bytes at @p ptr. */
template <typename T>
bool prefetch(T* ptr, std::size_t size)
{
    return prefetch((std::byte*)ptr, size * sizeof(T));
}

/** @brief Opaque implementation storage (PIMPL); the full definition lives in engram.cpp. */
struct impl_data;

/**
 * @brief Move-only bump-pointer allocator that owns one block of memory.
 *
 * @details Create an arena for a target (stack, heap, an external buffer, or a
 * device backend) with one of the static factories, then `push` objects, arrays
 * and strings into it and `pop` them in LIFO order. All storage is reclaimed when
 * the arena is destroyed. Arenas are non-copyable but movable; allocation state is
 * hidden behind an opaque @ref impl_data pointer (PIMPL).
 */
class arena
{
private:
    /** @brief Opaque pointer to internal state (implementation detail). */
    impl_data* m_impl = nullptr;

    arena();

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
#if defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
        try {
#endif
			((T*)(ptr))->~T();
#if defined(ENGRAM_MASK_EXCEPTIONS) && (defined(__cpp_exceptions) || defined(_CPPUNWIND))
        }
        catch (...) {
        }
#endif
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

    // Varargs backend dispatch (defined in engram.cpp). `provided` is the number of
    // backend params that follow; any optional trailing param not provided is filled
    // with the vendor default (see create_custom).
    static arena create_custom_va(std::size_t size, custom type, int32_t flags, std::size_t provided, ...);

    // Varargs backends for sync/prefetch (defined in engram.cpp). `provided` is the
    // number of backend params that follow; see the sync/prefetch templates.
    bool sync_va(std::size_t provided, ...);
    bool prefetch_va(std::size_t provided, ...);

public:
	
	/**
	 * @brief Create an arena backed by @p type.
	 * @param type      Memory source (stack / heap / …).
	 * @param size      Requested capacity in bytes.
	 * @param flags     Bitwise-OR of @ref flags values.
	 * @param alignment Minimum alignment of the storage.
	 * @param fd        Optional file descriptor for file-backed / unified mappings.
	 * @return The new arena (check @ref is_valid).
	 */
	[[nodiscard]] static arena create(memory_source type, std::size_t size, int32_t flags = 0, 
        std::size_t alignment = alignof(std::max_align_t), int fd = -1);

    /**
     * @brief Create a stack (`alloca`) arena.
     * @param size           Capacity in bytes.
     * @param fallbackToHeap Fall back to the heap when the request is too large for the stack.
     */
    [[nodiscard]] static arena stack(std::size_t size, bool fallbackToHeap)
    {
        return create(memory_source::stack, size, fallbackToHeap ? engram::flags::heap_fallback : 0);
    }

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

    /** @brief Create a heap arena backed by an anonymous `memfd` named @p name (Linux only). */
    [[nodiscard]] static arena heapfile(std::size_t size, std::string_view name, std::size_t alignment = alignof(std::max_align_t));
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
        return make_external((std::byte*)storage, size, flags);
    }

    /** @brief Adopt a caller-owned C array (size deduced from the array bound). */
    template <typename T, std::size_t size>
    [[nodiscard]] static arena adopt(T (&storage)[size], int32_t flags = 0)
    {
        static_assert(size > 0);
        return adopt(storage, size, flags);
    }

    /**
     * @brief Create an arena from a vendor / device backend selected by @p type.
     *
     * @details The backend-specific parameters follow @p flags as variadic arguments
     * and are extracted in engram.cpp before dispatching to the matching allocator:
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
    template <typename... Params>
    [[nodiscard]] static arena create_custom(std::size_t size, custom type, int32_t flags, Params&&... params)
    {
        return create_custom_va(size, type, flags, sizeof...(params), std::forward<Params>(params)...);
    }

	arena(const arena&) = delete;            ///< Arenas are non-copyable.
	arena& operator=(const arena&) = delete; ///< Arenas are non-copyable.

    /** @brief Move constructor: transfers ownership and leaves @p other empty. */
    arena(arena&& other) noexcept
    {
        std::swap(m_impl, other.m_impl);
    }

    /** @brief Move assignment: transfers ownership and leaves @p other empty. */
    arena& operator=(arena&& other) noexcept
    { 
        std::swap(m_impl, other.m_impl);
        return *this;
    }
	
	/** @brief Destroy the arena, reclaiming its storage (runs the backend free hook for device arenas). */
	~arena();

#ifndef ENGRAM_DISABLE_PMR
    /**
     * @brief View the arena's storage as a `std::pmr::monotonic_buffer_resource`.
     * @param upstream Upstream resource used once the arena is exhausted.
     */
    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::pmr::memory_resource* upstream = 
        std::pmr::null_memory_resource());

    /**
     * @brief View a sub-range `[start, start+size)` of the arena as a PMR resource.
     * @param start    Byte offset into the arena.
     * @param size     Size of the sub-range in bytes.
     * @param upstream Upstream resource used once the sub-range is exhausted.
     */
    std::pmr::monotonic_buffer_resource& get_pmr_resource(std::size_t start, std::size_t size, 
        std::pmr::memory_resource* upstream = std::pmr::null_memory_resource());
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
                    unreserve(sizeof(T) * (size - idx), true);
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
		return { valptr, size };
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
                    unreserve(sizeof(T) * (size - idx), true);
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
		release<T>(unreserve(sizeof(T), false));
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
    void reset() noexcept;

    /**
     * @brief Push the current head onto the save stack (an implicit sub-arena marker).
     * @details Save points nest LIFO and are held in a fixed array of
     * `ENGRAM_MAX_SAVE_STACKSZ` (default 32) entries; define that macro before
     * including engram.h to change the depth.
     * @return `false` if the save stack is full.
     */
    bool save() noexcept;

    /**
     * @brief Rewind to the most recent @ref save point, discarding everything pushed since.
     * @details Zeroes `[saved offset, current offset)` unless the arena was created
     * with `flags::no_clear` (device / `memory_source::custom` arenas are never
     * host-cleared), then restores the head and the allocation bookkeeping to their
     * saved values. Like @ref reset it runs no destructors.
     * @return `false` if no save point is pending.
     */
    bool restore() noexcept;

    /** @return Number of save points currently pending. */
    std::size_t save_depth() const noexcept;

    /** @brief Release pages pinned via `flags::pin_to_physical` (munlock / VirtualUnlock). */
    void unpin();

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

    /**
     * @brief Emit CPU prefetch hints over a range of the arena's storage.
     * @param locality Target cache level.
     * @param ioflags  `flags::read` / `flags::write` access intent.
     * @param start    Byte offset to start from.
     * @param size     Number of bytes (0 = to the end of the arena).
     */
    void warm_cache(cache_locality locality, int32_t ioflags, std::size_t start = 0, std::size_t size = 0);

    /** @brief Warm the `sizeof(T)` bytes at @p ptr (which must lie within this arena). */
    template <typename T>
    void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
    {
        if (auto base = base_ptr())
            warm_cache(locality, ioflags, (std::size_t)((std::byte*)ptr - base), sizeof(T));
    }

    bool is_valid() const noexcept;        ///< @return `true` if the arena holds valid storage.
    bool empty() const noexcept;           ///< @return `true` if nothing has been pushed yet.

    std::size_t used() const noexcept;     ///< @return Bytes currently in use.
    std::size_t capacity() const noexcept; ///< @return Total capacity in bytes.
    std::size_t remaining() const noexcept;///< @return Bytes still available.
    std::size_t count() const noexcept;    ///< @return Live array/string allocation count.
    std::size_t total() const noexcept;    ///< @return Lifetime array/string allocation count.
    memory_source source() const noexcept; ///< @return The arena's @ref memory_source.

    /**
     * @brief Access the arena's underlying storage as a contiguous byte range.
     * @return A `std::span<std::byte>` over the whole managed block `[base, capacity)`,
     *         or an empty span if the arena holds no storage.
     */
    std::span<std::byte> data() const noexcept;

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
    [[nodiscard]] arena partition(std::size_t start, std::size_t size, int32_t flags = 0);
};

} // namespace engram
