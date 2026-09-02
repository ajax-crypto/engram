// Tests for the freestanding single-header configuration.
//
// Built with ENGRAM_ENABLE_FREESTANDING, so only stack and adopted arenas exist:
// there is no heap, no PMR and no vendor backend. The suite still runs on a
// hosted machine — what is under test is the shape of the library once the
// hosted facilities are compiled out.
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

#ifndef ENGRAM_ENABLE_FREESTANDING
#error "test_freestanding.cpp must be compiled with ENGRAM_ENABLE_FREESTANDING"
#endif

#ifndef ENGRAM_MASK_EXCEPTIONS
#error "a freestanding build must imply ENGRAM_MASK_EXCEPTIONS"
#endif

namespace {

constexpr std::size_t kAlign = alignof(std::max_align_t);

constexpr std::size_t aligned(std::size_t n)
{
    return (n + (kAlign - 1)) & ~(kAlign - 1);
}

// The hosted-only surface must not be reachable at all.
template <typename T, typename = void>
struct has_heap_factory : std::false_type {};
template <typename T>
struct has_heap_factory<T, std::void_t<decltype(T::heap(std::size_t{}))>> : std::true_type {};

template <typename T, typename = void>
struct has_stack_factory : std::false_type {};
template <typename T>
struct has_stack_factory<T, std::void_t<decltype(T::stack(std::size_t{}, true))>> : std::true_type {};

template <typename T, typename = void>
struct has_unpin : std::false_type {};
template <typename T>
struct has_unpin<T, std::void_t<decltype(std::declval<T&>().unpin())>> : std::true_type {};

template <typename T, typename = void>
struct has_sync : std::false_type {};
template <typename T>
struct has_sync<T, std::void_t<decltype(std::declval<T&>().sync())>> : std::true_type {};

static_assert(!has_heap_factory<arena>::value, "arena::heap must be gone in a freestanding build");
static_assert(!has_stack_factory<arena>::value, "arena::stack was replaced by ENGRAM_STACK_ARENA");
static_assert(!has_unpin<arena>::value, "arena::unpin must be gone in a freestanding build");
static_assert(!has_sync<arena>::value, "arena::sync must be gone in a freestanding build");

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

// The only storage a freestanding target has: statically reserved bytes.
alignas(std::max_align_t) std::byte g_pool[8192];

arena make_pool_arena(int32_t f = flags::commit)
{
    return arena::adopt(g_pool, sizeof(g_pool), f);
}

bool all_equal(const std::byte* start, std::size_t size, unsigned char value)
{
    for (std::size_t i = 0; i < size; ++i)
        if (std::to_integer<unsigned char>(start[i]) != value)
            return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Memory sources available without an OS
// ---------------------------------------------------------------------------
TEST_GROUP(FreestandingCreation)
{
    void setup() override { std::memset(g_pool, 0, sizeof(g_pool)); }
};

TEST(FreestandingCreation, AdoptedPoolIsAnExternalArena)
{
    auto a = make_pool_arena();

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(a.empty());
    LONGS_EQUAL((long)memory_source::external, (long)a.source());
    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    UNSIGNED_LONGS_EQUAL(sizeof(g_pool), a.capacity());
    POINTERS_EQUAL(g_pool, a.data().data());
}

TEST(FreestandingCreation, AdoptedStorageSurvivesTheArena)
{
    std::memset(g_pool, 0xA5, sizeof(g_pool));

    {
        auto a = arena::adopt(g_pool, sizeof(g_pool), flags::none);
        CHECK_TRUE(a.is_valid());
    }

    CHECK_TRUE(all_equal(g_pool, sizeof(g_pool), 0xA5));
}

TEST(FreestandingCreation, MovedArenaKeepsItsStorage)
{
    auto source = make_pool_arena();
    auto& value = source.push<int>(7);
    (void)value;

    auto moved = std::move(source);

    CHECK_TRUE(moved.is_valid());
    POINTERS_EQUAL(g_pool, moved.data().data());
    UNSIGNED_LONGS_EQUAL(sizeof(g_pool), moved.capacity());
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), moved.used());
    CHECK_FALSE(source.is_valid());
}

TEST(FreestandingCreation, StackArenaMacroProducesUsableStorage)
{
    ENGRAM_STACK_ARENA(a, 1024, flags::commit);

    CHECK_TRUE(a.is_valid());
    LONGS_EQUAL((long)memory_source::stack, (long)a.source());
    UNSIGNED_LONGS_EQUAL(1024u, a.capacity());

    auto span = a.push_array<int>(64);
    UNSIGNED_LONGS_EQUAL(64u, span.size());
    for (int i = 0; i < 64; ++i)
        span[i] = i;
    for (int i = 0; i < 64; ++i)
        LONGS_EQUAL(i, span[i]);
}

TEST(FreestandingCreation, OversizedStackArenaMacroReportsOverflow)
{
    // Checked against ENGRAM_FREESTANDING_STACKSZ, since there is nothing to query.
    ENGRAM_STACK_ARENA(a, std::size_t{ ENGRAM_FREESTANDING_STACKSZ } * 2);

    CHECK_FALSE(a.is_valid());
    LONGS_EQUAL((long)arena_error::stack_overflow, (long)a.error());
}

TEST(FreestandingCreation, StackArenaCannotEscapeItsFrame)
{
    ENGRAM_STACK_ARENA(a, 256);

    // A reference, so `return a;` picks the deleted copy constructor rather than
    // silently moving storage that dies with the frame.
    static_assert(std::is_reference_v<decltype(a)>,
        "ENGRAM_STACK_ARENA must bind a reference");
    static_assert(!std::is_constructible_v<arena, decltype(a)>,
        "a stack arena must not be returnable or copyable by value");

    CHECK_TRUE(a.is_valid());
}

TEST(FreestandingCreation, StackArenaMacroOrsEveryFlagArgument)
{
    static_assert(engram::detail::combine_flags(flags::commit, flags::no_clear)
        == (flags::commit | flags::no_clear));

    ENGRAM_STACK_ARENA(a, 512, flags::no_clear, flags::commit);

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

// ---------------------------------------------------------------------------
// Flags that still mean something without an OS
// ---------------------------------------------------------------------------
TEST_GROUP(FreestandingFlags)
{
    void setup() override { std::memset(g_pool, 0xCD, sizeof(g_pool)); }
};

TEST(FreestandingFlags, CommitZeroesTheAdoptedPool)
{
    auto a = arena::adopt(g_pool, sizeof(g_pool), flags::commit);

    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(FreestandingFlags, NoneLeavesThePoolAlone)
{
    auto a = arena::adopt(g_pool, sizeof(g_pool), flags::none);

    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0xCD));
}

// ---------------------------------------------------------------------------
// Allocation / deallocation
// ---------------------------------------------------------------------------
TEST_GROUP(FreestandingAllocation)
{
    void setup() override
    {
        Tracked::reset();
        std::memset(g_pool, 0, sizeof(g_pool));
    }
};

TEST(FreestandingAllocation, PushAdvancesTheOffsetByAnAlignedAmount)
{
    auto a = make_pool_arena();

    auto& value = a.push<int>(42);

    LONGS_EQUAL(42, value);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), a.used());
    UNSIGNED_LONGS_EQUAL(a.capacity() - aligned(sizeof(int)), a.remaining());
}

TEST(FreestandingAllocation, EveryAllocationIsMaxAligned)
{
    auto a = make_pool_arena();

    for (int i = 0; i < 8; ++i)
    {
        auto& c = a.push<char>('x');
        UNSIGNED_LONGS_EQUAL(0u, reinterpret_cast<std::uintptr_t>(&c) % kAlign);
    }
}

TEST(FreestandingAllocation, PopRunsTheDestructorAndRewinds)
{
    auto a = make_pool_arena();

    auto& value = a.push<Tracked>(5);
    LONGS_EQUAL(5, value.value);
    LONGS_EQUAL(1, Tracked::constructed);

    a.pop<Tracked>();

    LONGS_EQUAL(1, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(FreestandingAllocation, PushArrayConstructsEveryElement)
{
    auto a = make_pool_arena();

    auto span = a.push_array<Tracked>(4, 9);

    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(4, Tracked::constructed);
    for (auto& element : span)
        LONGS_EQUAL(9, element.value);

    a.pop_array(span);

    LONGS_EQUAL(4, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(FreestandingAllocation, PushStringCopiesAndNulTerminates)
{
    auto a = make_pool_arena();

    auto view = a.push_string(std::string_view{ "engram" });

    UNSIGNED_LONGS_EQUAL(6u, view.size());
    STRCMP_EQUAL("engram", view.data());
    UNSIGNED_LONGS_EQUAL(aligned(7), a.used());

    a.pop_string(view);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(FreestandingAllocation, ExhaustedArenaReturnsAnEmptySpan)
{
    alignas(std::max_align_t) static std::byte small[128];
    auto a = arena::adopt(small, sizeof(small), flags::commit);

    auto ok = a.push_array<std::byte>(128);
    UNSIGNED_LONGS_EQUAL(128u, ok.size());
    UNSIGNED_LONGS_EQUAL(0u, a.remaining());

    auto overflow = a.push_array<std::byte>(1);
    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(128u, a.used());
}

TEST(FreestandingAllocation, LifoOrderRestoresTheOffsetStepByStep)
{
    auto a = make_pool_arena();

    auto& first = a.push<int>(1);
    (void)first;
    auto after_first = a.used();
    auto span = a.push_array<double>(8);
    (void)span;

    a.pop_array<double>(8);
    UNSIGNED_LONGS_EQUAL(after_first, a.used());

    a.pop<int>();
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(FreestandingAllocation, EveryPushIsCountedAndEveryPopUncountsIt)
{
    auto a = make_pool_arena();
    UNSIGNED_LONGS_EQUAL(0u, a.count());

    auto& value = a.push<int>(1);
    (void)value;
    UNSIGNED_LONGS_EQUAL(1u, a.count());

    auto span = a.push_array<int>(4);
    UNSIGNED_LONGS_EQUAL(2u, a.count());

    auto view = a.push_string(std::string_view{ "abc" });
    UNSIGNED_LONGS_EQUAL(3u, a.count());

    a.pop_string(view);
    a.pop_array(span);
    a.pop<int>();

    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(3u, a.total());
}

TEST(FreestandingAllocation, ResetReclaimsEverythingAtOnce)
{
    auto a = make_pool_arena();
    auto span = a.push_array<int>(32);
    (void)span;

    a.reset();

    CHECK_TRUE(a.empty());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(a.capacity(), a.remaining());
}

// ---------------------------------------------------------------------------
// Partitions
// ---------------------------------------------------------------------------
TEST_GROUP(FreestandingPartition)
{
    void setup() override { std::memset(g_pool, 0xEE, sizeof(g_pool)); }
};

TEST(FreestandingPartition, IsAnExternalViewOfTheParent)
{
    auto parent = arena::adopt(g_pool, sizeof(g_pool), flags::none);

    auto sub = parent.partition(1024, 512);

    LONGS_EQUAL((long)memory_source::external, (long)sub.source());
    UNSIGNED_LONGS_EQUAL(512u, sub.capacity());
    POINTERS_EQUAL(g_pool + 1024, sub.data().data());
}

TEST(FreestandingPartition, IsZeroedByDefault)
{
    auto parent = arena::adopt(g_pool, sizeof(g_pool), flags::none);

    auto sub = parent.partition(1024, 256);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0x00));
    CHECK_TRUE(all_equal(g_pool, 1024, 0xEE));
    CHECK_TRUE(all_equal(g_pool + 1280, 256, 0xEE));
}

TEST(FreestandingPartition, NoClearKeepsTheExistingBytes)
{
    auto parent = arena::adopt(g_pool, sizeof(g_pool), flags::none);

    auto sub = parent.partition(1024, 256, flags::no_clear);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0xEE));
}

TEST(FreestandingPartition, DoesNotDisturbTheParentOffset)
{
    auto parent = arena::adopt(g_pool, sizeof(g_pool), flags::commit);
    auto& anchor = parent.push<int>(1);
    (void)anchor;
    auto used = parent.used();

    auto sub = parent.partition(2048, 512);
    auto span = sub.push_array<int>(16);
    (void)span;

    UNSIGNED_LONGS_EQUAL(used, parent.used());
    UNSIGNED_LONGS_EQUAL(aligned(16 * sizeof(int)), sub.used());
}

TEST(FreestandingPartition, SiblingsAreIndependent)
{
    auto parent = arena::adopt(g_pool, sizeof(g_pool), flags::commit);

    auto left = parent.partition(0, 1024);
    auto right = parent.partition(2048, 1024);

    auto ls = left.push_array<std::byte>(64);
    auto rs = right.push_array<std::byte>(128);

    std::memset(ls.data(), 0x11, ls.size());
    std::memset(rs.data(), 0x22, rs.size());

    UNSIGNED_LONGS_EQUAL(64u, left.used());
    UNSIGNED_LONGS_EQUAL(128u, right.used());
    CHECK_TRUE(all_equal(ls.data(), ls.size(), 0x11));
    CHECK_TRUE(all_equal(rs.data(), rs.size(), 0x22));
}

// ---------------------------------------------------------------------------
// Save / restore
// ---------------------------------------------------------------------------
TEST_GROUP(FreestandingSaveRestore)
{
    void setup() override { std::memset(g_pool, 0, sizeof(g_pool)); }
};

TEST(FreestandingSaveRestore, RestoreRewindsToTheSavePoint)
{
    auto a = make_pool_arena();
    auto& keep = a.push<int>(3);
    auto base = a.used();

    CHECK_TRUE(a.save());
    UNSIGNED_LONGS_EQUAL(1u, a.save_depth());

    auto span = a.push_array<double>(32);
    (void)span;
    CHECK_TRUE(a.used() > base);

    CHECK_TRUE(a.restore());

    UNSIGNED_LONGS_EQUAL(base, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
    LONGS_EQUAL(3, keep);
}

TEST(FreestandingSaveRestore, RestoreZeroesTheReclaimedBytes)
{
    auto a = make_pool_arena();

    a.save();
    auto span = a.push_array<std::byte>(256);
    std::memset(span.data(), 0x5A, span.size());
    a.restore();

    CHECK_TRUE(all_equal(g_pool, 256, 0x00));
}

TEST(FreestandingSaveRestore, NoClearArenasKeepTheReclaimedBytes)
{
    auto a = arena::adopt(g_pool, sizeof(g_pool), flags::no_clear);

    a.save();
    auto span = a.push_array<std::byte>(64);
    std::memset(span.data(), 0x77, span.size());
    a.restore();

    CHECK_TRUE(all_equal(g_pool, 64, 0x77));
}

TEST(FreestandingSaveRestore, SavePointsNestLifo)
{
    auto a = make_pool_arena();

    auto outer = a.used();
    a.save();
    auto s1 = a.push_array<int>(8);
    (void)s1;
    auto inner = a.used();

    a.save();
    auto s2 = a.push_array<int>(8);
    (void)s2;
    UNSIGNED_LONGS_EQUAL(2u, a.save_depth());

    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(inner, a.used());

    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(outer, a.used());
}

TEST(FreestandingSaveRestore, RestoreWithoutASavePointFails)
{
    auto a = make_pool_arena();

    CHECK_FALSE(a.restore());
    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
}

TEST(FreestandingSaveRestore, SaveStackIsBounded)
{
    auto a = make_pool_arena();

    for (std::size_t i = 0; i < ENGRAM_MAX_SAVE_STACKSZ; ++i)
        CHECK_TRUE(a.save());

    CHECK_FALSE(a.save());

    for (std::size_t i = 0; i < ENGRAM_MAX_SAVE_STACKSZ; ++i)
        CHECK_TRUE(a.restore());

    CHECK_FALSE(a.restore());
}

TEST(FreestandingSaveRestore, ResetDropsPendingSavePoints)
{
    auto a = make_pool_arena();

    a.save();
    a.save();

    a.reset();

    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
    CHECK_FALSE(a.restore());
}

// ---------------------------------------------------------------------------
// ENGRAM_ENABLE_FSEXTRA
// ---------------------------------------------------------------------------
#ifdef ENGRAM_ENABLE_FSEXTRA

TEST_GROUP(FreestandingExtra)
{
};

TEST(FreestandingExtra, WarmCacheIsAvailableAndHarmless)
{
    auto a = arena::adopt(g_pool, sizeof(g_pool), flags::commit);
    auto span = a.push_array<std::byte>(256);
    std::memset(span.data(), 0x33, span.size());

    a.warm_cache(engram::cache_locality::L1, flags::read, 0, 256);
    a.warm_cache(engram::cache_locality::Discard, flags::write);
    a.warm_cache(span.data(), engram::cache_locality::L2, flags::read);
    engram::warm_cache(g_pool, engram::cache_locality::L3, flags::read);

    CHECK_TRUE(all_equal(span.data(), span.size(), 0x33));
}

#endif

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
