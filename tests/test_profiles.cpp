// Tests for the trimmed build profiles: ENGRAM_DISABLE_SAVE_RESTORE and ENGRAM_MINIMAL.
//
// Compiled once per profile against each layout, so the same file has to cover both
// the single header and the header + source build. What is under test is the *shape*
// of the library once a feature is compiled out, plus the fact that everything that
// survived still works.
//
// engram.h must be included before any CppUTest header: CppUTest's leak detector
// defines a `new` macro that would otherwise rewrite the library's placement-new
// expressions.
#include "engram.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/TestHarness.h>

using engram::arena;
using engram::arena_error;
using engram::memory_source;
namespace flags = engram::flags;

#if !defined(ENGRAM_DISABLE_SAVE_RESTORE)
#error "test_profiles.cpp must be compiled with ENGRAM_DISABLE_SAVE_RESTORE (directly or via ENGRAM_MINIMAL)"
#endif

namespace {

constexpr std::size_t kAlign = alignof(std::max_align_t);

constexpr std::size_t aligned(std::size_t n)
{
    return (n + (kAlign - 1)) & ~(kAlign - 1);
}

// The compiled-out surface must not be reachable at all.
template <typename T, typename = void>
struct has_save : std::false_type {};
template <typename T>
struct has_save<T, std::void_t<decltype(std::declval<T&>().save())>> : std::true_type {};

template <typename T, typename = void>
struct has_restore : std::false_type {};
template <typename T>
struct has_restore<T, std::void_t<decltype(std::declval<T&>().restore())>> : std::true_type {};

template <typename T, typename = void>
struct has_save_depth : std::false_type {};
template <typename T>
struct has_save_depth<T, std::void_t<decltype(std::declval<const T&>().save_depth())>> : std::true_type {};

template <typename T, typename = void>
struct has_pmr : std::false_type {};
template <typename T>
struct has_pmr<T, std::void_t<decltype(std::declval<T&>().get_pmr_resource())>> : std::true_type {};

static_assert(!has_save<arena>::value, "ENGRAM_DISABLE_SAVE_RESTORE must remove arena::save");
static_assert(!has_restore<arena>::value, "ENGRAM_DISABLE_SAVE_RESTORE must remove arena::restore");
static_assert(!has_save_depth<arena>::value, "ENGRAM_DISABLE_SAVE_RESTORE must remove arena::save_depth");

#ifdef ENGRAM_MINIMAL
static_assert(!has_pmr<arena>::value, "ENGRAM_MINIMAL implies ENGRAM_DISABLE_PMR");

// The profile is a bundle of macros, so check it actually set them.
#ifndef ENGRAM_DISABLE_TRACKING
#error "ENGRAM_MINIMAL must define ENGRAM_DISABLE_TRACKING"
#endif
#ifndef ENGRAM_DISABLE_PMR
#error "ENGRAM_MINIMAL must define ENGRAM_DISABLE_PMR"
#endif
#ifndef ENGRAM_MASK_EXCEPTIONS
#error "ENGRAM_MINIMAL must define ENGRAM_MASK_EXCEPTIONS"
#endif
#ifdef ENGRAM_EASY_POP
#error "ENGRAM_MINIMAL must switch ENGRAM_EASY_POP off"
#endif

// One native GPU backend per platform survives; everything else is gone.
#if defined(_WIN32) && !defined(ENGRAM_ENABLE_DX12)
#error "ENGRAM_MINIMAL should keep DX12 on Windows"
#elif defined(__linux__) && !defined(ENGRAM_ENABLE_VULKAN)
#error "ENGRAM_MINIMAL should keep Vulkan on Linux"
#elif defined(__APPLE__) && !defined(ENGRAM_ENABLE_METAL)
#error "ENGRAM_MINIMAL should keep Metal on Apple"
#endif
#ifdef ENGRAM_ENABLE_CUDA
#error "ENGRAM_MINIMAL must switch the non-native backends off"
#endif
#endif // ENGRAM_MINIMAL

struct Tracked
{
    static int constructed;
    static int destroyed;

    int value;

    explicit Tracked(int v = 0) : value(v) { ++constructed; }
    ~Tracked() { ++destroyed; }

    static void reset() { constructed = destroyed = 0; }
};

int Tracked::constructed = 0;
int Tracked::destroyed = 0;

bool all_equal(const std::byte* start, std::size_t size, unsigned char value)
{
    for (std::size_t i = 0; i < size; ++i)
        if (std::to_integer<unsigned char>(start[i]) != value)
            return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Everything that survived the trimming still has to work
// ---------------------------------------------------------------------------
TEST_GROUP(TrimmedProfile)
{
    void setup() override { Tracked::reset(); }
};

TEST(TrimmedProfile, HeapArenasStillWork)
{
    auto a = arena::heap(4096);

    CHECK_TRUE(a.is_valid());
    LONGS_EQUAL((long)memory_source::heap, (long)a.source());
    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    CHECK_TRUE(a.capacity() >= 4096u);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(TrimmedProfile, PushAndPopStillWork)
{
    auto a = arena::heap(4096);

    auto& value = a.push<Tracked>(5);
    LONGS_EQUAL(5, value.value);
    LONGS_EQUAL(1, Tracked::constructed);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(Tracked)), a.used());

    a.pop<Tracked>();

    LONGS_EQUAL(1, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    CHECK_TRUE(a.empty());
}

TEST(TrimmedProfile, ArraysAndStringsStillWork)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<int>(64);
    UNSIGNED_LONGS_EQUAL(64u, span.size());
    for (int i = 0; i < 64; ++i)
        span[i] = i;

    auto view = a.push_string(std::string_view{ "engram" });
    STRCMP_EQUAL("engram", view.data());

    a.pop(view);
    a.pop(span);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(TrimmedProfile, ResetStillRewinds)
{
    auto a = arena::heap(4096);

    (void)a.push_array<int>(32);
    CHECK_TRUE(a.used() > 0u);

    a.reset();

    UNSIGNED_LONGS_EQUAL(0u, a.used());
    CHECK_TRUE(a.empty());
}

TEST(TrimmedProfile, PartitionsStillWork)
{
    auto parent = arena::heap(4096);
    auto sub = parent.partition(0, 1024);

    CHECK_TRUE(sub.is_valid());
    LONGS_EQUAL((long)memory_source::external, (long)sub.source());
    UNSIGNED_LONGS_EQUAL(1024u, sub.capacity());

    auto& value = sub.push<int>(7);
    LONGS_EQUAL(7, value);
    UNSIGNED_LONGS_EQUAL(0u, parent.used());
}

TEST(TrimmedProfile, StackArenasStillWork)
{
    ENGRAM_STACK_ARENA(a, 1024, flags::commit);

    CHECK_TRUE(a.is_valid());
    LONGS_EQUAL((long)memory_source::stack, (long)a.source());
    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(TrimmedProfile, MovesStillTransferOwnership)
{
    auto source = arena::heap(4096);
    auto& value = source.push<int>(7);
    (void)value;

    auto moved = std::move(source);

    CHECK_TRUE(moved.is_valid());
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), moved.used());
    CHECK_FALSE(source.is_valid());
}

TEST(TrimmedProfile, ExhaustionIsStillReported)
{
    auto a = arena::heap(64);

    auto span = a.push_array<std::byte>(a.capacity() * 2);

    CHECK_TRUE(span.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
}

#ifdef ENGRAM_DISABLE_TRACKING
// The accessors stay so callers keep compiling; they just stop counting.
TEST(TrimmedProfile, TrackingCountersReadZero)
{
    auto a = arena::heap(4096);

    (void)a.push<int>(1);
    (void)a.push_array<int>(4);

    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(0u, a.total());
    CHECK_TRUE(a.used() > 0u);
}
#endif

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
