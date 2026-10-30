#pragma once
#ifndef ENGRAM_H_INCLUDED
#define ENGRAM_H_INCLUDED

/**
 * @file engram.h
 * @brief engram — a move-only bump-allocation `arena` over stack and external
 *        memory for freestanding targets (single-header, freestanding-only build).
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

// This build only ever targets a freestanding implementation: no OS, no heap, no
// vendor SDKs. Only stack and adopted-external arenas are available. Use the
// header + source build (src/engram.h + src/engram.cpp) for heap and device backends.
#ifndef ENGRAM_ENABLE_FREESTANDING
#define ENGRAM_ENABLE_FREESTANDING
#endif

// monotonic_buffer_resource is a hosted facility, so PMR is never available here.
#ifndef ENGRAM_DISABLE_PMR
#define ENGRAM_DISABLE_PMR
#endif

// Nothing is going to catch a throwing constructor out here.
#ifndef ENGRAM_MASK_EXCEPTIONS
#define ENGRAM_MASK_EXCEPTIONS
#endif

/** @brief Stack budget a freestanding stack arena is checked against (nothing to query). */
#ifndef ENGRAM_FREESTANDING_STACKSZ
#define ENGRAM_FREESTANDING_STACKSZ (64 * 1024)
#endif

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <span>
#include <string_view>
#include <new>
#include <cstdarg>
#include <version>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifndef ENGRAM_FALLBACK_PAGESZ
#define ENGRAM_FALLBACK_PAGESZ 4096
#endif

/** @brief Bytes covered by one prefetch instruction. */
#ifndef ENGRAM_CACHELINE_SZ
#define ENGRAM_CACHELINE_SZ 64
#endif

/** @brief Taken when an unrecoverable misuse is detected; must not return. */
#ifndef ENGRAM_ABORT
#define ENGRAM_ABORT() abort()
#endif

// std::mdspan arrived in C++23; the helpers switch themselves on when the
// standard library provides it.
#ifdef __cpp_lib_mdspan
#include <mdspan>
#define ENGRAM_HAS_MDSPAN
#endif

#ifdef ENGRAM_ENABLE_SOURCE_INFO
#include <source_location>
// Appended to every factory so the default argument is evaluated at the call site.
#define ENGRAM_SOURCE_PARAM , const std::source_location& loc = std::source_location::current()
#define ENGRAM_SOURCE_ARG   , loc
#else
#define ENGRAM_SOURCE_PARAM
#define ENGRAM_SOURCE_ARG
#endif

#include <array>

#ifdef ENGRAM_EASY_POP
/** @brief Number of pushes @ref engram::arena::pop can rewind through. */
#ifndef ENGRAM_MAX_PUSH_DEPTH
#define ENGRAM_MAX_PUSH_DEPTH 32
#endif
#endif

/** @brief Depth of the fixed save-point stack used by @ref engram::arena::save. */
#if !defined(ENGRAM_DISABLE_SAVE_RESTORE) && !defined(ENGRAM_MAX_SAVE_STACKSZ)
#define ENGRAM_MAX_SAVE_STACKSZ 32
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

namespace engram {

class arena;

/** @brief Where an arena's memory comes from. */
enum class memory_source
{
    stack,          ///< Stack memory (`alloca`).
    external        ///< A caller-owned buffer the arena does not free.
};

/** @brief Error state recorded on an arena after a failed creation. */
enum class arena_error
{
    no_error,        ///< No error.
    stack_overflow,  ///< Requested stack size exceeded the thread stack.
    alloc_failed     ///< The requested region did not fit in the backing storage.
};

/** @brief Target cache level for @ref arena::warm_cache and @ref warm_cache. */
enum class cache_locality
{
    Discard,   ///< Non-temporal / streaming (do not retain in cache).
    L3,        ///< Keep in L3.
    L2,        ///< Keep in L2.
    L1         ///< Keep in L1 (highest temporal locality).
};

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
    /// Reserve heap address space without committing/backing it (`mmap(PROT_NONE)` /
    /// `VirtualAlloc(MEM_RESERVE)`); the range is inaccessible until the caller commits a
    /// sub-range through platform-specific means.
    constexpr int32_t reserve_only = 256;

    constexpr int32_t read = 1;              ///< Cache-warm read intent (@ref warm_cache ioflags).
    constexpr int32_t write = 2;             ///< Cache-warm write intent (@ref warm_cache ioflags).
}

// `get_total_stack_space` needs a complete `arena` for later use, so it is only declared here.
inline std::size_t get_total_stack_space();

/**
 * @brief Check a request against the calling thread's total stack size.
 * @return `true` if @p size bytes can plausibly be taken from the stack.
 */
inline bool stack_fits(std::size_t size)
{
    return size < get_total_stack_space();
}

#ifdef ENGRAM_ENABLE_FSEXTRA
/**
 * @brief Emit CPU prefetch hints for a host memory range.
 * @param ptr      Start of the range.
 * @param size     Number of bytes to warm.
 * @param locality Target cache level.
 * @param ioflags  `flags::read` or `flags::write` access-intent hint.
 */
inline void warm_cache(std::byte* ptr, std::size_t size, cache_locality locality, int32_t ioflags)
{
    if (!ptr)
        return;

    auto rw = (ioflags & flags::write) ? 1 : 0;
    auto loc = static_cast<int>(locality);
    if (size == 0)
        size = 1;

    for (std::size_t off = 0; off < size; off += ENGRAM_CACHELINE_SZ)
        PrefetchIntoCache((const void*)(ptr + off), rw, loc);
}

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
    warm_cache((std::byte*)ptr, sizeof(T), locality, ioflags);
}
#endif

namespace detail
{

// Shape traits backing arena::pop(T&); each is specialised for the one type it names.
template <typename T> inline constexpr bool is_span_v = false;
template <typename T, std::size_t Extent> inline constexpr bool is_span_v<std::span<T, Extent>> = true;

template <typename T> inline constexpr bool is_string_view_v = false;
template <typename CharT, typename Traits>
inline constexpr bool is_string_view_v<std::basic_string_view<CharT, Traits>> = true;

#ifdef ENGRAM_HAS_MDSPAN
template <typename T> inline constexpr bool is_mdspan_v = false;
template <typename T, typename Extents, typename LayoutPolicy, typename AccessorPolicy>
inline constexpr bool is_mdspan_v<std::mdspan<T, Extents, LayoutPolicy, AccessorPolicy>> = true;
#endif

// Binary fold, so the trailing pad ENGRAM_STACK_ARENA appends is enough to keep an
// empty flag list well-formed.
template <typename... FlagsT>
constexpr int32_t combine_flags(FlagsT... values)
{
    return (int32_t{ engram::flags::none } | ... | static_cast<int32_t>(values));
}

} // namespace detail

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
        static_assert(requires { T{ std::forward<ArgsT>(args)... }; } || std::is_constructible_v<T, ArgsT...>,
            "engram: T cannot be constructed from the supplied arguments");
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
            if constexpr (requires { T{ std::forward<ArgsT>(args)... }; })
                new ((void*)ptr) T{ std::forward<ArgsT>(args)... };
            else
                new ((void*)ptr) T( std::forward<ArgsT>(args)... );
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
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
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
			((T*)(ptr))->~T();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        }
        catch (...) {
        }
#endif
    }

    // Round a request up to the maximum fundamental alignment; keeps every bump
    // offset a multiple of that quantum so each returned address is aligned for
    // any standard type and pop stays exact without tracking padding.
    static constexpr std::size_t align_up_max(std::size_t n)
    {
        constexpr std::size_t A = alignof(std::max_align_t);
        return (n + (A - 1)) & ~(A - 1);
    }

    // ENGRAM_MASK_EXCEPTIONS: keep the `built` elements that were constructed and
    // reclaim the rest of the reservation.
    template <typename T>
    std::span<T> truncate_array(T* base, std::size_t built, std::size_t size)
    {
        if (built == 0)
        {
            unreserve(align_up_max(sizeof(T) * size), true);
            return {};
        }

        unreserve(align_up_max(sizeof(T) * size) - align_up_max(sizeof(T) * built), false);
        return { base, built };
    }

    std::byte* reserve(std::size_t bytes, bool countable)
    {
        auto rounded = align_up_max(bytes);
        if (!m_ptr || rounded > m_size - m_offset)
        {
            m_error = arena_error::alloc_failed;
            return nullptr;
        }

#ifdef ENGRAM_EASY_POP
        // A full record stack fails the reservation rather than losing the record
        // pop() would need.
        if (countable && m_push_depth >= m_push_records.size())
        {
            m_error = arena_error::alloc_failed;
            return nullptr;
        }
#endif

        auto slot = m_ptr + m_offset;
#ifdef ENGRAM_EASY_POP
        if (countable)
            m_push_records[m_push_depth++] = { slot, m_offset };
#endif
        m_offset += rounded;
        if (m_offset > m_high_water)
            m_high_water = m_offset;
#ifndef ENGRAM_DISABLE_TRACKING
        if (countable)
        {
            m_count++;
            m_total++;
        }
#endif
#if defined(ENGRAM_DISABLE_TRACKING) && !defined(ENGRAM_EASY_POP)
        (void)countable;
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
#endif
#ifdef ENGRAM_EASY_POP
        if (countable && m_push_depth > 0)
            --m_push_depth;
#endif
#if defined(ENGRAM_DISABLE_TRACKING) && !defined(ENGRAM_EASY_POP)
        (void)countable;
#endif
        return m_ptr + m_offset;
    }

public:

	// The following data members hold the arena's internal state. They are public
	// in the header-only build but are implementation details, not stable API.
	std::byte*  m_ptr = nullptr;
	std::size_t m_offset = 0;
    std::size_t m_size = 0;
    // Highest m_offset ever reached; unlike m_offset (which pop/restore/reset can rewind),
    // this only grows, so free-time scrubbing knows the full extent ever written.
    std::size_t m_high_water = 0;
#ifndef ENGRAM_DISABLE_TRACKING
    std::size_t m_count = 0, m_total = 0;
#endif
	memory_source m_type;
    arena_error m_error = arena_error::no_error;

#ifdef ENGRAM_EASY_POP
    // One entry per push so pop() can rewind without being told what was pushed.
    struct push_record
    {
        std::byte* ptr;
        std::size_t offset;
    };
    std::array<push_record, ENGRAM_MAX_PUSH_DEPTH> m_push_records{};
    std::size_t m_push_depth = 0;
#endif

#ifndef ENGRAM_DISABLE_SAVE_RESTORE
    // One entry per pending save(); restore() rewinds to the newest one.
    struct save_point
    {
        std::size_t offset;
#ifndef ENGRAM_DISABLE_TRACKING
        std::size_t count;
#endif
#ifdef ENGRAM_EASY_POP
        std::size_t push_depth;
#endif
    };
    std::array<save_point, ENGRAM_MAX_SAVE_STACKSZ> m_save_stack{};
    std::size_t m_save_stacksz = 0;
#endif

#ifdef ENGRAM_ENABLE_SOURCE_INFO
    std::source_location m_origin{};
#endif

	bool m_clear_on_free = true;

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
    [[nodiscard]] static arena wrap_stack(void* storage, std::size_t size, int32_t flags = 0 ENGRAM_SOURCE_PARAM)
    {
        if (!storage)
        {
            arena result;
            result.m_type = memory_source::stack;
            result.m_error = arena_error::stack_overflow;
#ifdef ENGRAM_ENABLE_SOURCE_INFO
            result.m_origin = loc;
#endif
            return result;
        }

        auto result = adopt((std::byte*)storage, size, flags ENGRAM_SOURCE_ARG);
        result.m_type = memory_source::stack;
        return result;
    }

    /**
     * @brief Adopt a caller-owned buffer; the arena uses but does not free it.
     * @tparam T      Element type of the storage.
     * @param storage Pointer to the buffer.
     * @param size    Size of the buffer in bytes.
     * @param flags   @ref flags (e.g. `commit`, `no_clear`).
     */
    template <typename T>
    [[nodiscard]] static arena adopt(T* storage, std::size_t size, int32_t flags = 0 ENGRAM_SOURCE_PARAM)
    {
        assert(storage != nullptr);

        arena result;
#ifdef ENGRAM_ENABLE_SOURCE_INFO
        result.m_origin = loc;
#endif

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
    [[nodiscard]] static arena adopt(T (&storage)[size], int32_t flags = 0 ENGRAM_SOURCE_PARAM)
    {
        static_assert(size > 0);
        return adopt(storage, size, flags ENGRAM_SOURCE_ARG);
    }

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
#ifndef ENGRAM_DISABLE_TRACKING
        m_total = other.m_total;
        m_count = other.m_count;
#endif
        m_clear_on_free = other.m_clear_on_free;
#ifdef ENGRAM_EASY_POP
        m_push_records = other.m_push_records;
        m_push_depth = other.m_push_depth;
#endif
#ifdef ENGRAM_ENABLE_SOURCE_INFO
        m_origin = other.m_origin;
#endif
#ifndef ENGRAM_DISABLE_SAVE_RESTORE
        m_save_stack = other.m_save_stack;
        m_save_stacksz = other.m_save_stacksz;
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
#ifndef ENGRAM_DISABLE_TRACKING
        m_total = other.m_total;
        m_count = other.m_count;
#endif
        m_clear_on_free = other.m_clear_on_free;
#ifdef ENGRAM_EASY_POP
        m_push_records = other.m_push_records;
        m_push_depth = other.m_push_depth;
#endif
#ifdef ENGRAM_ENABLE_SOURCE_INFO
        m_origin = other.m_origin;
#endif
#ifndef ENGRAM_DISABLE_SAVE_RESTORE
        m_save_stack = other.m_save_stack;
        m_save_stacksz = other.m_save_stacksz;
#endif
        return *this;
    }
	
	/** @brief Destroy the arena, reclaiming its storage. */
	~arena()
	{
        if (m_ptr && (m_type == memory_source::stack) && m_clear_on_free)
            memset(m_ptr, 0, m_high_water);
		
		m_ptr = nullptr;
		m_offset = 0;
#ifndef ENGRAM_DISABLE_TRACKING
		m_count = m_total = 0;
#endif
	}
	
	/**
	 * @brief Construct a `T` in the arena and return a reference to it.
	 * @tparam T     Type to construct.
	 * @tparam ArgsT Constructor argument types.
	 * @details A `T&` cannot report exhaustion, so running out of room aborts rather than
	 * returning a null reference. Check @ref remaining first, or use @ref push_array /
	 * @ref push_string, which report failure through an empty result and @ref error.
	 */
	template <typename T, typename... ArgsT>
	[[nodiscard]] T& push(ArgsT&&... args)
	{
		auto slot = reserve(sizeof(T), true);
        if (!slot)
        {
            assert(false && "engram: push() on an exhausted arena");
            ENGRAM_ABORT();
        }

        initialize<T, ArgsT...>(slot, std::forward<ArgsT>(args)...);
        return *reinterpret_cast<T*>(slot);
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
		return push_array<T>(size, std::forward<ArgsT>(args)...);
	}

    /** @brief Copy a compile-time-sized character array into the arena (NUL-terminated). */
    template <std::size_t size, typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(CharT (&str)[size])
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true));
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
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true));
        if (!valptr)
            return {};
        Traits::assign(valptr, size, fillchar);
        Traits::assign(valptr[size], CharT{});
        return { valptr, size };
    }

    /**
     * @brief Reserve a runtime-sized array of `T` and return it as a `std::span`.
     * @param size Element count.
     * @param args Constructor arguments; when none are given, trivially default-constructible
     *             elements are left uninitialized and everything else is value-initialized.
     */
    template <typename T, typename... ArgsT>
	[[nodiscard]] std::span<T> push_array(std::size_t size, ArgsT&&... args)
	{
		assert(size > 0);

		auto valptr = reinterpret_cast<T*>(reserve(sizeof(T) * size, true));
		if (!valptr)
			return {};

		if constexpr (sizeof...(args) > 0 || !std::is_trivially_default_constructible_v<T>)
		{
			for (std::size_t idx = 0; idx < size; ++idx)
			{
				auto slot = (std::byte*)(valptr + idx);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
				if (!initialize<T>(slot, std::forward<ArgsT>(args)...))
					return truncate_array<T>(valptr, idx, size);
#else
				initialize<T>(slot, std::forward<ArgsT>(args)...);
#endif
			}
		}
		return { valptr, size };
	}

#ifdef ENGRAM_HAS_MDSPAN
    /**
     * @brief Reserve a multi-dimensional array of `T` and return it as a `std::mdspan`.
     *
     * @details The element count comes from the layout mapping, so the reservation
     * matches what the mapping will actually index. Construction, counting and
     * exception behaviour are exactly @ref push_array's.
     * @tparam T              Element type.
     * @tparam N              Rank. Deduced from a `std::array` argument; pass it
     *                        explicitly when using a braced list.
     * @tparam LayoutPolicy   `std::mdspan` layout mapping.
     * @tparam AccessorPolicy `std::mdspan` accessor.
     * @param extents Size of each dimension.
     * @param args    Constructor arguments applied to every element.
     */
    template <typename T, std::size_t N, class LayoutPolicy = std::layout_right,
              class AccessorPolicy = std::default_accessor<T>, typename... ArgsT>
    [[nodiscard]] auto push_md_array(const std::array<std::size_t, N>& extents, ArgsT&&... args)
    {
        static_assert(N > 0, "engram: an mdspan needs at least one dimension");

        using extents_type = std::dextents<std::size_t, N>;
        using mdspan_type = std::mdspan<T, extents_type, LayoutPolicy, AccessorPolicy>;

        const typename mdspan_type::mapping_type mapping{ extents_type{ extents } };
        const auto count = mapping.required_span_size();
        assert(count > 0 && "engram: mdspan extents must all be non-zero");

        auto storage = push_array<T>(count, std::forward<ArgsT>(args)...);
        if (storage.empty())
            return mdspan_type{};

        return mdspan_type{ storage.data(), mapping };
    }

    /** @brief Destroy and pop an array pushed with @ref push_md_array. */
    template <typename MdSpanT>
    void pop_md_array(const MdSpanT& span)
    {
        pop_array<typename MdSpanT::element_type>(span.mapping().required_span_size());
    }
#endif

    /** @brief Copy a string view into the arena (NUL-terminated); returns a view of the copy. */
    template <typename CharT = char, typename Traits = std::char_traits<CharT>>
    [[nodiscard]] std::basic_string_view<CharT, Traits> push_string(std::basic_string_view<CharT, Traits> str)
    {
        auto valptr = reinterpret_cast<CharT*>(reserve((str.size() + 1) * sizeof(CharT), true));
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
        auto valptr = reinterpret_cast<CharT*>(reserve((size + 1) * sizeof(CharT), true));
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

    /**
     * @brief Destroy and pop @p el, dispatching on what it is.
     *
     * @details A `std::span` routes to @ref pop_array, a `std::basic_string_view` to
     * @ref pop_string, a `std::mdspan` to @ref pop_md_array, and anything else to the
     * plain `pop<T>()`. Lets generic code pop whatever a `push*` handed back without
     * having to name the shape.
     */
    template <typename T>
    void pop(T& el)
    {
        using bare = std::remove_cv_t<T>;

        if constexpr (detail::is_span_v<bare>)
            pop_array<typename bare::element_type>(el.size());
        else if constexpr (detail::is_string_view_v<bare>)
            pop_string(el);
#ifdef ENGRAM_HAS_MDSPAN
        else if constexpr (detail::is_mdspan_v<bare>)
            pop_md_array(el);
#endif
        else
            pop<bare>();
    }

#ifdef ENGRAM_EASY_POP

    /**
     * @brief Pop the most recent push, whatever it was (requires `ENGRAM_EASY_POP`).
     *
     * @details Every `push*` records the offset it started at, so this rewinds to it
     * without being told the type or the element count. It runs **no destructors** — use
     * the typed `pop` / `pop_array` / `pop_string` for anything that needs cleanup. The
     * reclaimed bytes are zeroed unless the arena was created with `flags::no_clear`.
     */
    void pop() noexcept
    {
        assert(m_push_depth > 0 && "engram: pop() with nothing pushed");
        if (m_push_depth == 0)
            return;

        const auto& record = m_push_records[--m_push_depth];
        assert(record.ptr == m_ptr + record.offset);

        if (m_ptr && m_clear_on_free && (m_offset > record.offset))
            memset(m_ptr + record.offset, 0, m_offset - record.offset);

        m_offset = record.offset;
#ifndef ENGRAM_DISABLE_TRACKING
        if (m_count > 0)
            --m_count;
#endif
    }

    /** @return Number of pushes @ref pop can still rewind through. */
    std::size_t push_depth() const noexcept { return m_push_depth; }

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
        m_push_depth = 0;
#endif
#ifndef ENGRAM_DISABLE_SAVE_RESTORE
        m_save_stacksz = 0;
#endif
    }

#ifndef ENGRAM_DISABLE_SAVE_RESTORE
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
        sp.push_depth = m_push_depth;
#endif
        return true;
    }

    /**
     * @brief Rewind to the most recent @ref save point, discarding everything pushed since.
     * @details Zeroes `[saved offset, current offset)` unless the arena was created
     * with `flags::no_clear`, then restores the head and the allocation bookkeeping to
     * their saved values. Like @ref reset it runs no destructors.
     * @return `false` if no save point is pending.
     */
    bool restore() noexcept
    {
        if (m_save_stacksz == 0)
            return false;

        const auto& sp = m_save_stack[--m_save_stacksz];
        if (m_ptr && m_clear_on_free && (m_offset > sp.offset))
            memset(m_ptr + sp.offset, 0, m_offset - sp.offset);

        m_offset = sp.offset;
#ifndef ENGRAM_DISABLE_TRACKING
        m_count = sp.count;
#endif
#ifdef ENGRAM_EASY_POP
        m_push_depth = sp.push_depth;
#endif
        return true;
    }

    /** @return Number of save points currently pending. */
    std::size_t save_depth() const noexcept { return m_save_stacksz; }
#endif

#ifdef ENGRAM_ENABLE_FSEXTRA
    /**
     * @brief Emit CPU prefetch hints over a range of the arena's storage.
     * @param locality Target cache level.
     * @param ioflags  `flags::read` / `flags::write` access intent.
     * @param start    Byte offset to start from.
     * @param size     Number of bytes (0 = to the end of the arena).
     */
    void warm_cache(cache_locality locality, int32_t ioflags, std::size_t start = 0, std::size_t size = 0)
    {
        if (m_ptr)
        {
            size = (size == 0) ? m_size - start : size;
            assert(start + size <= m_size);

            engram::warm_cache(m_ptr + start, size, locality, ioflags);
        }
    }

    /** @brief Warm the `sizeof(T)` bytes at @p ptr (which must lie within this arena). */
    template <typename T>
    void warm_cache(T* ptr, cache_locality locality, int32_t ioflags)
    {
        if (m_ptr)
        {
            assert((std::byte*)ptr >= m_ptr && (std::byte*)ptr < m_ptr + m_size);
            engram::warm_cache((std::byte*)ptr, sizeof(T), locality, ioflags);
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

#ifdef ENGRAM_ENABLE_SOURCE_INFO
    /** @brief Where this arena was created (requires `ENGRAM_ENABLE_SOURCE_INFO`). */
    const std::source_location& origin() const noexcept { return m_origin; }
#endif

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
    [[nodiscard]] arena partition(std::size_t start, std::size_t size, int32_t flags = 0 ENGRAM_SOURCE_PARAM)
    {
        assert((start + size) <= m_size);
        return adopt(m_ptr + start, size, (flags & engram::flags::no_clear) ? 0 : engram::flags::commit ENGRAM_SOURCE_ARG);
    }
};

// Nothing to query without an OS, so stack arenas are sized against a compile-time budget.
inline std::size_t get_total_stack_space()
{
    return ENGRAM_FREESTANDING_STACKSZ;
}

} // namespace engram

#define ENGRAM_STACK_ARENA_EXPAND_(x) x
#define ENGRAM_STACK_ARENA_IMPL_(varname, size, ...)                               \
    const std::size_t varname##_engram_size_ = (std::size_t)(size);                \
    void* varname##_engram_storage_ = engram::stack_fits(varname##_engram_size_)   \
        ? ENGRAM_STACK_ALLOC(varname##_engram_size_)                               \
        : nullptr;                                                                 \
    engram::arena varname##_engram_arena_ = engram::arena::wrap_stack(             \
        varname##_engram_storage_, varname##_engram_size_,                         \
        engram::detail::combine_flags(__VA_ARGS__));                               \
    engram::arena& varname = varname##_engram_arena_

/**
 * @brief Declare a stack-backed arena named @p varname in the current scope.
 *
 * @details `alloca` storage belongs to the frame that requests it, so this has to be
 * a macro: a factory function would hand back a pointer into its own dead frame. The
 * arena is therefore usable only within the enclosing scope, and its storage is gone
 * once that scope exits.
 *
 * Everything after the size is treated as @ref engram::flags and OR-ed together, so
 * `ENGRAM_STACK_ARENA(scratch, 4096)`,
 * `ENGRAM_STACK_ARENA(scratch, 4096, engram::flags::commit)` and
 * `ENGRAM_STACK_ARENA(scratch, 4096, engram::flags::commit, engram::flags::no_clear)`
 * are all valid.
 *
 * @p varname names an `arena&` bound to a hidden local, not an arena object, so
 * `return scratch;` and passing it by value do not compile: implicit move does not
 * apply to references, which leaves only the deleted copy constructor. Escaping the
 * frame therefore takes a deliberate `std::move`, which is on you.
 *
 * The request is checked against the thread's stack size first; if it does not fit,
 * @p varname is an invalid arena reporting @ref engram::arena_error::stack_overflow
 * rather than a smashed stack. Three extra names (`<varname>_engram_size_`,
 * `<varname>_engram_storage_` and `<varname>_engram_arena_`) are declared alongside
 * it, so the macro needs a block scope rather than a bare `if` branch.
 */
#define ENGRAM_STACK_ARENA(varname, ...) \
    ENGRAM_STACK_ARENA_EXPAND_(ENGRAM_STACK_ARENA_IMPL_(varname, __VA_ARGS__, engram::flags::none))

#endif // ENGRAM_H_INCLUDED
