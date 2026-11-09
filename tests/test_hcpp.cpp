// Tests for the header + source (PIMPL) build: src/engram.h + src/engram.cpp.
//
// engram.h must be included before any CppUTest header: CppUTest's leak detector
// defines a `new` macro that would otherwise rewrite the library's placement-new
// expressions.
#include "engram.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/TestHarness.h>

using engram::arena;
using engram::arena_error;
using engram::memory_source;
namespace flags = engram::flags;

namespace {

constexpr std::size_t kAlign = alignof(std::max_align_t);

// Every reservation is rounded up to the maximum fundamental alignment, so
// offset expectations have to be computed the same way to stay portable.
constexpr std::size_t aligned(std::size_t n)
{
    return (n + (kAlign - 1)) & ~(kAlign - 1);
}

// push_array/push_string reserve this many extra bytes for an overwrite-detection
// canary when ENGRAM_ENABLE_DIAGNOSTICS is active, so byte-exact expectations below
// have to account for it to stay correct in both configurations.
#ifdef ENGRAM_ENABLE_DIAGNOSTICS
constexpr std::size_t kGuardSz = 4;
#else
constexpr std::size_t kGuardSz = 0;
#endif

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

struct NoDefault
{
    int value;
    explicit NoDefault(int v) : value(v) {}
};

struct Boom : std::runtime_error
{
    Boom() : std::runtime_error("boom") {}
};

// Throws out of the constructor on the `throw_at`-th attempt.
struct Fragile
{
    static int attempts;
    static int live;
    static int throw_at;

    int value;

    Fragile() : Fragile(0) {}

    explicit Fragile(int v) : value(v)
    {
        if (attempts++ == throw_at)
            throw Boom{};
        ++live;
    }

    ~Fragile() { --live; }

    static void reset(int at = -1) { attempts = 0; live = 0; throw_at = at; }
};

int Fragile::attempts = 0;
int Fragile::live = 0;
int Fragile::throw_at = -1;

struct NoisyDtor
{
    static int destroyed;

    ~NoisyDtor() noexcept(false)
    {
        ++destroyed;
        throw Boom{};
    }
};

int NoisyDtor::destroyed = 0;

void fill(std::span<std::byte> bytes, unsigned char value)
{
    std::memset(bytes.data(), value, bytes.size());
}

bool all_equal(const std::byte* start, std::size_t size, unsigned char value)
{
    for (std::size_t i = 0; i < size; ++i)
        if (std::to_integer<unsigned char>(start[i]) != value)
            return false;
    return true;
}

// Simulates a child function handing a large array back to its caller through a
// slice the caller already owns: the return value is pushed first, then local
// scratch is pushed and popped on top of it before returning, so the scratch
// never has to pop through (and destroy) the result underneath it.
std::span<int> fill_squares_via_slice(arena& scratch, std::size_t count)
{
    auto result = scratch.push_array<int>(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<int>(i * i);

    auto workspace = scratch.push_array<int>(count);
    for (std::size_t i = 0; i < count; ++i)
        workspace[i] += result[i];
    scratch.pop_array(workspace);

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Byte-count literals
// ---------------------------------------------------------------------------
TEST_GROUP(Literals)
{
};

TEST(Literals, MatchTheBinaryUnits)
{
    using namespace engram;

    UNSIGNED_LONGS_EQUAL(1024u, (std::size_t)1_KB);
    UNSIGNED_LONGS_EQUAL(1024u * 1024u, (std::size_t)1_MB);
    UNSIGNED_LONGS_EQUAL(1024u * 1024u * 1024u, (std::size_t)1_GB);
    UNSIGNED_LONGS_EQUAL(1024ull * 1024ull * 1024ull * 1024ull, (unsigned long long)1_TB);

    UNSIGNED_LONGS_EQUAL(4u * 1024u * 1024u, (std::size_t)4_MB);
}

TEST(Literals, WorkDirectlyAsAnArenaSize)
{
    using namespace engram;

    auto a = arena::heap(4_MB);

    CHECK_TRUE(a.is_valid());
    UNSIGNED_LONGS_EQUAL(4u * 1024u * 1024u, a.capacity());
}

// ---------------------------------------------------------------------------
// Creation, memory sources and error reporting
// ---------------------------------------------------------------------------
TEST_GROUP(Creation)
{
};

TEST(Creation, HeapArenaStartsEmptyAndValid)
{
    auto a = arena::heap(1 << 16);

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(a.empty());
    LONGS_EQUAL((long)memory_source::heap, (long)a.source());
    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    UNSIGNED_LONGS_EQUAL(1u << 16, a.capacity());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(a.capacity(), a.remaining());
    UNSIGNED_LONGS_EQUAL(a.capacity(), a.data().size());
}

TEST(Creation, HeapArenaBaseIsMaxAligned)
{
    auto a = arena::heap(4096);
    auto address = reinterpret_cast<std::uintptr_t>(a.data().data());

    UNSIGNED_LONGS_EQUAL(0u, address % kAlign);
}

TEST(Creation, MovedArenaKeepsItsStorage)
{
    auto source = arena::heap(1 << 14);
    auto* base = source.data().data();
    auto& value = source.push<int>(7);
    (void)value;

    auto moved = std::move(source);

    CHECK_TRUE(moved.is_valid());
    POINTERS_EQUAL(base, moved.data().data());
    UNSIGNED_LONGS_EQUAL(1u << 14, moved.capacity());
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), moved.used());
    CHECK_FALSE(source.is_valid());
}

TEST(Creation, StackArenaMacroProducesUsableStorage)
{
    ENGRAM_STACK_ARENA(a, 1024);

    CHECK_TRUE(a.is_valid());
    LONGS_EQUAL((long)memory_source::stack, (long)a.source());
    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    UNSIGNED_LONGS_EQUAL(1024u, a.capacity());
    UNSIGNED_LONGS_EQUAL(0u, a.used());

    auto span = a.push_array<int>(64);
    UNSIGNED_LONGS_EQUAL(64u, span.size());
    for (int i = 0; i < 64; ++i)
        span[i] = i;
    for (int i = 0; i < 64; ++i)
        LONGS_EQUAL(i, span[i]);
}

TEST(Creation, StackArenaMacroAcceptsFlags)
{
    ENGRAM_STACK_ARENA(a, 512, flags::commit);

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(Creation, StackArenaMacroOrsEveryFlagArgument)
{
    static_assert(engram::detail::combine_flags() == flags::none);
    static_assert(engram::detail::combine_flags(flags::commit) == flags::commit);
    static_assert(engram::detail::combine_flags(flags::commit, flags::no_clear)
        == (flags::commit | flags::no_clear));

    ENGRAM_STACK_ARENA(a, 512, flags::no_clear, flags::commit);

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(Creation, StackArenaCannotEscapeItsFrame)
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

TEST(Creation, OversizedStackArenaMacroReportsOverflow)
{
    ENGRAM_STACK_ARENA(a, std::size_t{ 1 } << 40);

    CHECK_FALSE(a.is_valid());
    LONGS_EQUAL((long)arena_error::stack_overflow, (long)a.error());
    UNSIGNED_LONGS_EQUAL(0u, a.capacity());
}

TEST(Creation, StackArenasAreIndependentPerScope)
{
    ENGRAM_STACK_ARENA(outer, 256);
    auto& outer_value = outer.push<int>(1);

    {
        ENGRAM_STACK_ARENA(inner, 256);
        auto& inner_value = inner.push<int>(2);

        CHECK_TRUE(inner.data().data() != outer.data().data());
        LONGS_EQUAL(2, inner_value);
    }

    LONGS_EQUAL(1, outer_value);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), outer.used());
}

TEST(Creation, CreateRejectsTheStackSource)
{
    // The stack source is reachable only through the macro.
    LONGS_EQUAL((long)memory_source::heap,
                (long)arena::create(memory_source::heap, 1024).source());
}

TEST(Creation, FailedHeapAllocationReportsAllocFailed)
{
    auto a = arena::create(memory_source::heap, ~std::size_t{ 0 } / 2, flags::none);

    CHECK_FALSE(a.is_valid());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(0u, a.data().size());
}

TEST(Creation, AdoptedBufferBecomesAnExternalArena)
{
    alignas(std::max_align_t) unsigned char storage[1024];

    auto a = arena::adopt(storage, sizeof(storage));

    CHECK_TRUE(a.is_valid());
    LONGS_EQUAL((long)memory_source::external, (long)a.source());
    UNSIGNED_LONGS_EQUAL(sizeof(storage), a.capacity());
    POINTERS_EQUAL(storage, a.data().data());
}

TEST(Creation, AdoptedStorageSurvivesTheArena)
{
    alignas(std::max_align_t) unsigned char storage[256];
    std::memset(storage, 0xAB, sizeof(storage));

    {
        auto a = arena::adopt(storage, sizeof(storage));
        CHECK_TRUE(a.is_valid());
    }

    CHECK_TRUE(all_equal(reinterpret_cast<std::byte*>(storage), sizeof(storage), 0xAB));
}

// ---------------------------------------------------------------------------
// Allocation flags
// ---------------------------------------------------------------------------
TEST_GROUP(Flags)
{
};

TEST(Flags, CommitZeroesAdoptedStorage)
{
    alignas(std::max_align_t) unsigned char storage[512];
    std::memset(storage, 0xCD, sizeof(storage));

    auto a = arena::adopt(storage, sizeof(storage), flags::commit);

    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(Flags, AdoptWithoutCommitLeavesStorageAlone)
{
    alignas(std::max_align_t) unsigned char storage[512];
    std::memset(storage, 0xCD, sizeof(storage));

    auto a = arena::adopt(storage, sizeof(storage), flags::none);

    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0xCD));
}

TEST(Flags, CommitZeroesHeapStorage)
{
    auto a = arena::create(memory_source::heap, 4096, flags::commit);

    CHECK_TRUE(a.is_valid());
    CHECK_TRUE(all_equal(a.data().data(), a.data().size(), 0x00));
}

TEST(Flags, ExplicitAlignmentIsHonoured)
{
    auto a = arena::create(memory_source::heap, 4096, flags::none, 256);

    CHECK_TRUE(a.is_valid());
    UNSIGNED_LONGS_EQUAL(0u, reinterpret_cast<std::uintptr_t>(a.data().data()) % 256);
}

TEST(Flags, PageAlignedRaisesTheBaseAlignment)
{
    auto a = arena::create(memory_source::heap, 4096, flags::page_aligned);

    CHECK_TRUE(a.is_valid());
    // Page size is at least 4 KiB on every platform engram builds for.
    UNSIGNED_LONGS_EQUAL(0u, reinterpret_cast<std::uintptr_t>(a.data().data()) % 4096);
    // capacity() reports the request; only the underlying block is rounded up.
    UNSIGNED_LONGS_EQUAL(4096u, a.capacity());
}

TEST(Flags, PinToPhysicalStillProducesAUsableArena)
{
    // Locking pages can be refused (rlimit / privileges); the arena must not
    // fail because of it.
    auto a = arena::create(memory_source::heap, 4096, flags::pin_to_physical);

    CHECK_TRUE(a.is_valid());
    auto span = a.push_array<int>(16);
    UNSIGNED_LONGS_EQUAL(16u, span.size());
    a.unpin();
}

// ---------------------------------------------------------------------------
// Allocation / deallocation
// ---------------------------------------------------------------------------
TEST_GROUP(Allocation)
{
    void setup() override { Tracked::reset(); }
};

TEST(Allocation, PushAdvancesTheOffsetByAnAlignedAmount)
{
    auto a = arena::heap(4096);

    auto& value = a.push<int>(42);

    LONGS_EQUAL(42, value);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), a.used());
    UNSIGNED_LONGS_EQUAL(a.capacity() - aligned(sizeof(int)), a.remaining());
    CHECK_FALSE(a.empty());
}

TEST(Allocation, EveryAllocationIsMaxAligned)
{
    auto a = arena::heap(4096);

    for (int i = 0; i < 8; ++i)
    {
        auto& c = a.push<char>('x');
        UNSIGNED_LONGS_EQUAL(0u, reinterpret_cast<std::uintptr_t>(&c) % kAlign);
    }
}

TEST(Allocation, PopRunsTheDestructorAndRewinds)
{
    auto a = arena::heap(4096);

    auto& value = a.push<Tracked>(5);
    LONGS_EQUAL(5, value.value);
    LONGS_EQUAL(1, Tracked::constructed);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(Tracked)), a.used());

    a.pop<Tracked>();

    LONGS_EQUAL(1, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, PushArrayConstructsEveryElement)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<Tracked>(4, 9);

    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(4, Tracked::constructed);
    for (auto& element : span)
        LONGS_EQUAL(9, element.value);

    a.pop_array(span);

    LONGS_EQUAL(4, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, PushArrayIsContiguousAndRewindsExactly)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<int>(64);

    UNSIGNED_LONGS_EQUAL(64u, span.size());
    UNSIGNED_LONGS_EQUAL(aligned(64 * sizeof(int) + kGuardSz), a.used());
    POINTERS_EQUAL(a.data().data(), reinterpret_cast<std::byte*>(span.data()));

    a.pop_array<int>(64);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, PushStringCopiesAndNulTerminates)
{
    auto a = arena::heap(4096);

    auto view = a.push_string(std::string_view{ "engram" });

    UNSIGNED_LONGS_EQUAL(6u, view.size());
    STRCMP_EQUAL("engram", view.data());
    UNSIGNED_LONGS_EQUAL(aligned(7 + kGuardSz), a.used());

    a.pop_string(view);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, PushStringCanReserveAFilledBuffer)
{
    auto a = arena::heap(4096);

    auto view = a.push_string(4, 'z');

    UNSIGNED_LONGS_EQUAL(4u, view.size());
    STRCMP_EQUAL("zzzz", view.data());
}

TEST(Allocation, LifoOrderRestoresTheOffsetStepByStep)
{
    auto a = arena::heap(4096);

    auto& first = a.push<int>(1);
    (void)first;
    auto after_first = a.used();
    auto span = a.push_array<double>(8);
    (void)span;
    auto after_span = a.used();

    CHECK_TRUE(after_span > after_first);

    a.pop_array<double>(8);
    UNSIGNED_LONGS_EQUAL(after_first, a.used());

    a.pop<int>();
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, ExhaustedArenaReturnsAnEmptySpan)
{
    alignas(std::max_align_t) unsigned char storage[aligned(128 + kGuardSz)];
    auto a = arena::adopt(storage, sizeof(storage));

    auto ok = a.push_array<std::byte>(128);
    UNSIGNED_LONGS_EQUAL(128u, ok.size());
    UNSIGNED_LONGS_EQUAL(0u, a.remaining());

    auto overflow = a.push_array<std::byte>(1);
    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(aligned(128 + kGuardSz), a.used());
}

TEST(Allocation, EveryPushIsCountedAndEveryPopUncountsIt)
{
    auto a = arena::heap(4096);
    UNSIGNED_LONGS_EQUAL(0u, a.count());

    auto& value = a.push<int>(1);
    (void)value;
    UNSIGNED_LONGS_EQUAL(1u, a.count());

    auto span = a.push_array<int>(4);
    UNSIGNED_LONGS_EQUAL(2u, a.count());

    auto fixed = a.push_array<8, int>();
    UNSIGNED_LONGS_EQUAL(3u, a.count());

    auto view = a.push_string(std::string_view{ "abc" });
    UNSIGNED_LONGS_EQUAL(4u, a.count());

    a.pop_string(view);
    UNSIGNED_LONGS_EQUAL(3u, a.count());

    a.pop_array<8, int>();
    UNSIGNED_LONGS_EQUAL(2u, a.count());

    a.pop_array(span);
    UNSIGNED_LONGS_EQUAL(1u, a.count());

    a.pop<int>();
    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, PushPopCyclesDoNotDriftTheLiveCount)
{
    auto a = arena::heap(4096);

    for (int i = 0; i < 100; ++i)
    {
        auto& value = a.push<int>(i);
        (void)value;
        a.pop<int>();
    }

    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(Allocation, TotalOnlyEverGrows)
{
    auto a = arena::heap(4096);

    auto& value = a.push<int>(1);
    (void)value;
    auto span = a.push_array<int>(4);
    UNSIGNED_LONGS_EQUAL(2u, a.total());

    a.pop_array(span);
    a.pop<int>();

    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(2u, a.total());
}

TEST(Allocation, ResetReclaimsEverythingAtOnce)
{
    auto a = arena::heap(4096);
    auto span = a.push_array<int>(32);
    (void)span;
    auto view = a.push_string(std::string_view{ "hello" });
    (void)view;

    a.reset();

    CHECK_TRUE(a.empty());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
    UNSIGNED_LONGS_EQUAL(a.capacity(), a.remaining());
}

// ---------------------------------------------------------------------------
// Contains
// ---------------------------------------------------------------------------
TEST_GROUP(Contains)
{
};

TEST(Contains, NullptrIsNeverContained)
{
    auto a = arena::heap(4096);
    CHECK_FALSE(a.contains(nullptr));
}

TEST(Contains, InvalidArenaContainsNothing)
{
    ENGRAM_STACK_ARENA(a, std::size_t{ 1 } << 40);
    CHECK_FALSE(a.is_valid());
    CHECK_FALSE(a.contains(nullptr));
}

TEST(Contains, BaseAndLastByteAreContained)
{
    auto a = arena::heap(4096);

    CHECK_TRUE(a.contains(a.data().data()));
    CHECK_TRUE(a.contains(a.data().data() + a.capacity() - 1));
}

TEST(Contains, OnePastTheEndIsNotContained)
{
    auto a = arena::heap(4096);
    CHECK_FALSE(a.contains(a.data().data() + a.capacity()));
}

TEST(Contains, PointerBeforeTheBaseIsNotContained)
{
    auto a = arena::heap(4096);
    CHECK_FALSE(a.contains(a.data().data() - 1));
}

TEST(Contains, PushedObjectIsContained)
{
    auto a = arena::heap(4096);
    auto& value = a.push<int>(1);
    CHECK_TRUE(a.contains(&value));
}

TEST(Contains, SlicePointerIsContainedInBothArenas)
{
    auto parent = arena::heap(4096);
    auto sub = parent.slice(1024, 512);

    CHECK_TRUE(parent.contains(sub.data().data()));
    CHECK_TRUE(sub.contains(sub.data().data()));
    CHECK_FALSE(sub.contains(parent.data().data()));
}

// ---------------------------------------------------------------------------
// Slices (sub-arenas)
// ---------------------------------------------------------------------------
TEST_GROUP(Slice)
{
};

TEST(Slice, IsAnExternalViewOfTheParent)
{
    auto parent = arena::heap(4096);

    auto sub = parent.slice(1024, 512);

    LONGS_EQUAL((long)memory_source::external, (long)sub.source());
    UNSIGNED_LONGS_EQUAL(512u, sub.capacity());
    POINTERS_EQUAL(parent.data().data() + 1024, sub.data().data());
}

TEST(Slice, DoesNotDisturbTheParentOffset)
{
    auto parent = arena::heap(4096);
    auto& anchor = parent.push<int>(1);
    (void)anchor;
    auto used = parent.used();

    auto sub = parent.slice(2048, 512);
    auto span = sub.push_array<int>(16);
    (void)span;

    UNSIGNED_LONGS_EQUAL(used, parent.used());
    UNSIGNED_LONGS_EQUAL(aligned(16 * sizeof(int) + kGuardSz), sub.used());
}

TEST(Slice, IsZeroedByDefault)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.slice(1024, 256);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0x00));
    // Bytes outside the region are left as they were.
    CHECK_TRUE(all_equal(parent.data().data(), 1024, 0xEE));
    CHECK_TRUE(all_equal(parent.data().data() + 1280, 256, 0xEE));
}

TEST(Slice, NoClearKeepsTheExistingBytes)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.slice(1024, 256, flags::no_clear);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0xEE));
}

TEST(Slice, DestroyingItLeavesTheParentIntact)
{
    auto parent = arena::heap(4096);
    auto& anchor = parent.push<int>(99);

    {
        auto sub = parent.slice(2048, 256);
        auto span = sub.push_array<int>(8);
        (void)span;
    }

    CHECK_TRUE(parent.is_valid());
    LONGS_EQUAL(99, anchor);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), parent.used());
}

TEST(Slice, SiblingsAreIndependent)
{
    auto parent = arena::heap(4096);

    auto left = parent.slice(0, 1024);
    auto right = parent.slice(2048, 1024);

    auto ls = left.push_array<std::byte>(64);
    auto rs = right.push_array<std::byte>(128);

    std::memset(ls.data(), 0x11, ls.size());
    std::memset(rs.data(), 0x22, rs.size());

    UNSIGNED_LONGS_EQUAL(aligned(64 + kGuardSz), left.used());
    UNSIGNED_LONGS_EQUAL(aligned(128 + kGuardSz), right.used());
    CHECK_TRUE(all_equal(ls.data(), ls.size(), 0x11));
    CHECK_TRUE(all_equal(rs.data(), rs.size(), 0x22));
}

TEST(Slice, AvoidFalseSharingKeepsTheRequestedCapacity)
{
    auto parent = arena::heap(4096);

    auto sub = parent.slice(1024, 512, flags::avoid_false_sharing);

    UNSIGNED_LONGS_EQUAL(512u, sub.capacity());
    POINTERS_EQUAL(parent.data().data() + 1024, sub.data().data());
}

TEST(Slice, AvoidFalseSharingReservesSizePlusPadInTheParent)
{
    auto parent = arena::heap(4096);

    auto guarded = parent.slice(0, 512, flags::avoid_false_sharing);
    auto next = parent.slice(512 + ENGRAM_FALSE_SHARING_PAD, 64);

    auto gs = guarded.push_array<std::byte>(guarded.capacity() - kGuardSz);
    auto ns = next.push_array<std::byte>(64 - kGuardSz);

    // The pad keeps `next`'s storage ENGRAM_FALSE_SHARING_PAD bytes away from
    // whatever `guarded` actually uses.
    CHECK_TRUE((ns.data() - (gs.data() + gs.size())) >= ENGRAM_FALSE_SHARING_PAD);
}

TEST(Slice, AvoidFalseSharingZeroesTheGuardBytesToo)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.slice(1024, 512, flags::avoid_false_sharing);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0x00));
    CHECK_TRUE(all_equal(parent.data().data() + 1024, 512 + ENGRAM_FALSE_SHARING_PAD, 0x00));
}

TEST(Slice, AvoidFalseSharingWithNoClearKeepsTheExistingBytes)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.slice(1024, 512, flags::avoid_false_sharing | flags::no_clear);

    CHECK_TRUE(all_equal(parent.data().data() + 1024, 512 + ENGRAM_FALSE_SHARING_PAD, 0xEE));
}

TEST(Slice, ChildFunctionReturnsALargeArrayThroughASliceToItsParent)
{
    auto parent = arena::heap(1 << 20);
    auto sub = parent.slice(0, 64 * 1024);

    constexpr std::size_t count = 4096;
    auto values = fill_squares_via_slice(sub, count);

    // The child's own scratch is gone, but its return value survived.
    UNSIGNED_LONGS_EQUAL(aligned(count * sizeof(int)), sub.used());
    UNSIGNED_LONGS_EQUAL(count, values.size());
    for (std::size_t i = 0; i < count; ++i)
        LONGS_EQUAL((long)(i * i), (long)values[i]);

    // The parent reads the data back through the slice it handed the child.
    CHECK_TRUE(parent.contains(values.data()));
    CHECK_TRUE(sub.contains(values.data()));
    POINTERS_EQUAL(sub.data().data(), values.data());
}

// ---------------------------------------------------------------------------
// Partitioning (bulk slicing via callback)
// ---------------------------------------------------------------------------
TEST_GROUP(Partition)
{
};

TEST(Partition, CallsFuncOnceForEachSizeInOrder)
{
    auto parent = arena::heap(4096);
    std::size_t sizes[] = { 256, 512, 128 };
    std::vector<arena> subs;

    auto ok = parent.partition(sizes, [&](arena&& sub) { subs.push_back(std::move(sub)); });

    CHECK_TRUE(ok);
    UNSIGNED_LONGS_EQUAL(3u, subs.size());
    UNSIGNED_LONGS_EQUAL(256u, subs[0].capacity());
    UNSIGNED_LONGS_EQUAL(512u, subs[1].capacity());
    UNSIGNED_LONGS_EQUAL(128u, subs[2].capacity());
    POINTERS_EQUAL(parent.data().data(), subs[0].data().data());
    POINTERS_EQUAL(parent.data().data() + 256, subs[1].data().data());
    POINTERS_EQUAL(parent.data().data() + 256 + 512, subs[2].data().data());
}

TEST(Partition, IsZeroedByDefault)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);
    std::size_t sizes[] = { 256, 512 };
    std::vector<arena> subs;

    parent.partition(sizes, [&](arena&& sub) { subs.push_back(std::move(sub)); });

    CHECK_TRUE(all_equal(subs[0].data().data(), subs[0].data().size(), 0x00));
    CHECK_TRUE(all_equal(subs[1].data().data(), subs[1].data().size(), 0x00));
    // Bytes past the requested regions are left as they were.
    CHECK_TRUE(all_equal(parent.data().data() + 256 + 512, 64, 0xEE));
}

TEST(Partition, NoClearKeepsTheExistingBytes)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);
    std::size_t sizes[] = { 256, 512 };
    std::vector<arena> subs;

    parent.partition(sizes, [&](arena&& sub) { subs.push_back(std::move(sub)); }, flags::no_clear);

    CHECK_TRUE(all_equal(subs[0].data().data(), subs[0].data().size(), 0xEE));
    CHECK_TRUE(all_equal(subs[1].data().data(), subs[1].data().size(), 0xEE));
}

TEST(Partition, AvoidFalseSharingSpacesSlicesApart)
{
    auto parent = arena::heap(4096);
    std::size_t sizes[] = { 256, 512 };
    std::vector<arena> subs;

    auto ok = parent.partition(sizes, [&](arena&& sub) { subs.push_back(std::move(sub)); },
        flags::avoid_false_sharing);

    CHECK_TRUE(ok);
    UNSIGNED_LONGS_EQUAL(256u, subs[0].capacity());
    UNSIGNED_LONGS_EQUAL(512u, subs[1].capacity());
    POINTERS_EQUAL(parent.data().data() + 256 + ENGRAM_FALSE_SHARING_PAD, subs[1].data().data());
}

TEST(Partition, SiblingsAreIndependent)
{
    auto parent = arena::heap(4096);
    std::size_t sizes[] = { 64, 128 };
    std::vector<arena> subs;

    parent.partition(sizes, [&](arena&& sub) { subs.push_back(std::move(sub)); });

    auto ls = subs[0].push_array<std::byte>(64);
    auto rs = subs[1].push_array<std::byte>(128);

    std::memset(ls.data(), 0x11, ls.size());
    std::memset(rs.data(), 0x22, rs.size());

    CHECK_TRUE(all_equal(ls.data(), ls.size(), 0x11));
    CHECK_TRUE(all_equal(rs.data(), rs.size(), 0x22));
}

TEST(Partition, ReturnsFalseAndSkipsFuncWhenItDoesNotFit)
{
    auto parent = arena::heap(4096);
    std::size_t sizes[] = { 2048, 4096 };
    int calls = 0;

    auto ok = parent.partition(sizes, [&](arena&&) { ++calls; });

    CHECK_FALSE(ok);
    LONGS_EQUAL(0, calls);
}

TEST(Partition, AvoidFalseSharingPaddingCanTipItOverTheEdge)
{
    // Fits without the pad, but the pad for each of the two slices pushes it over.
    // arena::heap() page-aligns (rounds up) its request, so adopt a fixed-size
    // buffer instead to keep the parent's capacity exact.
    alignas(std::max_align_t) std::byte buf[2 * ENGRAM_FALSE_SHARING_PAD];
    auto parent = arena::adopt(buf, sizeof(buf));
    std::size_t sizes[] = { ENGRAM_FALSE_SHARING_PAD, ENGRAM_FALSE_SHARING_PAD };
    int calls = 0;

    auto ok = parent.partition(sizes, [&](arena&&) { ++calls; }, flags::avoid_false_sharing);

    CHECK_FALSE(ok);
    LONGS_EQUAL(0, calls);
}

// ---------------------------------------------------------------------------
// Growth (flags::expand / flags::expand_custom): a heap arena grows by linking
// in another region instead of failing once the active one is full.
// ---------------------------------------------------------------------------
TEST_GROUP(Growth)
{
};

TEST(Growth, WithoutExpandExhaustionFailsOutright)
{
    auto a = arena::create(memory_source::heap, kAlign * 2, flags::none);

    auto first = a.push_array<std::byte>(kAlign);
    auto second = a.push_array<std::byte>(kAlign);
    (void)first; (void)second;

    auto overflow = a.push_array<std::byte>(1);

    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(kAlign * 2, a.capacity());
}

TEST(Growth, ExpandGrowsCapacityByAnotherRegionWhenFull)
{
    auto a = arena::create(memory_source::heap, kAlign * 2, flags::expand);
    CHECK_TRUE(a.is_valid());
    UNSIGNED_LONGS_EQUAL(kAlign * 2, a.capacity());

    auto first = a.push_array<std::byte>(kAlign);
    auto second = a.push_array<std::byte>(kAlign);
    (void)first; (void)second;
    UNSIGNED_LONGS_EQUAL(kAlign * 2, a.used());

    // The region is exactly full; this grows a same-sized second region instead
    // of failing.
    auto& value = a.push<int>(7);

    LONGS_EQUAL(7, value);
    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    UNSIGNED_LONGS_EQUAL(kAlign * 4, a.capacity());
    UNSIGNED_LONGS_EQUAL(kAlign * 3, a.used());
}

TEST(Growth, GrowthIsTriggeredEvenWhenTheTailIsOnlyPartiallyFull)
{
    // Leftover room smaller than one quantum still has to grow rather than
    // silently fail or corrupt the next region.
    auto regionSize = kAlign + (kAlign / 2);
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    auto first = a.push_array<std::byte>(1);
    (void)first;
    UNSIGNED_LONGS_EQUAL(kAlign, a.used());
    UNSIGNED_LONGS_EQUAL(regionSize, a.capacity());

    auto& value = a.push<std::byte>(std::byte{ 9 });
    (void)value;

    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
    UNSIGNED_LONGS_EQUAL(kAlign * 2, a.used());

    // Neither region's leftover half-quantum is ever reclaimed by later pushes.
    CHECK_TRUE(a.used() < a.capacity());
}

TEST(Growth, PopUnwindsBackAcrossARegionBoundaryToEmpty)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    a.push<int>(1);
    a.push<int>(2);                 // fills region 1 exactly
    auto& third = a.push<int>(3);   // grows into region 2
    LONGS_EQUAL(3, third);
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
    UNSIGNED_LONGS_EQUAL(regionSize + kAlign, a.used());

    a.pop<int>();   // region 2
    a.pop<int>();   // region 1 (last)
    a.pop<int>();   // region 1 (first)

    CHECK_TRUE(a.empty());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity()); // regions stay allocated
}

TEST(Growth, DrainedRegionIsReusedOnTheNextGrowthInsteadOfAllocatingAgain)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    a.push<int>(1);
    a.push<int>(2);
    a.push<int>(3); // grows into region 2
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());

    a.pop<int>();
    a.pop<int>();
    a.pop<int>();
    CHECK_TRUE(a.empty());

    // Refilling past the first region's boundary again reuses the already-linked
    // region 2 rather than growing a third one.
    a.push<int>(4);
    a.push<int>(5);
    a.push<int>(6);

    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
}

TEST(Growth, ContainsFindsPointersInEveryLinkedRegion)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    a.push<int>(1);
    a.push<int>(2);
    auto& grown = a.push<int>(3); // lives in region 2

    CHECK_TRUE(a.contains(&grown));
    CHECK_FALSE(a.contains(a.data().data() + a.capacity())); // past everything
}

TEST(Growth, DataOnlyCoversTheFirstRegion)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    a.push<int>(1);
    a.push<int>(2);
    a.push<int>(3); // grows

    UNSIGNED_LONGS_EQUAL(regionSize, a.data().size());
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
}

TEST(Growth, SliceLandsInASecondRegionViaTheOffsetAfterTheFirst)
{
    auto regionSize = kAlign * 4;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    auto fill = a.push_array<std::byte>(regionSize);
    (void)fill;
    UNSIGNED_LONGS_EQUAL(regionSize, a.used());

    auto& marker = a.push<int>(55); // grows into region 2, at its offset 0

    auto sub = a.slice(regionSize, kAlign);

    CHECK_TRUE(sub.is_valid());
    POINTERS_EQUAL(&marker, sub.data().data());
}

TEST(Growth, PartitionAlwaysFailsOnAGrowableArenaEvenWhenEverythingFits)
{
    auto a = arena::create(memory_source::heap, kAlign * 8, flags::expand);
    std::size_t sizes[] = { kAlign, kAlign };
    int calls = 0;

    auto ok = a.partition(sizes, [&](arena&&) { ++calls; });

    CHECK_FALSE(ok);
    LONGS_EQUAL(0, calls);
}

TEST(Growth, CommitFlagStillWorksTogetherWithExpand)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand | flags::commit);
    CHECK_TRUE(a.is_valid());

    auto fill = a.push_array<std::byte>(regionSize);
    (void)fill;
    auto& grown = a.push<int>(5);

    LONGS_EQUAL(5, grown);
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
}

TEST(Growth, PinToPhysicalAppliesToEachGrownRegionWithoutFailing)
{
    // Locking pages can be refused (rlimit / privileges); growth must not fail
    // because of it, and unpin() must still work across every linked region.
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand | flags::pin_to_physical);
    CHECK_TRUE(a.is_valid());

    auto fill = a.push_array<std::byte>(regionSize);
    (void)fill;
    auto& grown = a.push<int>(1);
    (void)grown;

    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
    a.unpin();
}

TEST(Growth, ResetReclaimsEveryRegionAndMovesTheTailBackToTheFirstOne)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    auto fill = a.push_array<std::byte>(regionSize);
    (void)fill;
    a.push<int>(1); // grows into region 2

    a.reset();

    CHECK_TRUE(a.empty());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity()); // regions stay allocated, just emptied

    // Filling past the first region's boundary again reuses region 2 instead of
    // growing a third one.
    auto refill = a.push_array<std::byte>(regionSize);
    (void)refill;
    a.push<int>(2);

    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
}

TEST(Growth, SaveRestoreAbandonsRegionsGrownAfterTheSavePoint)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    auto fill = a.push_array<std::byte>(regionSize); // fills region 1
    (void)fill;
    CHECK_TRUE(a.save());

    a.push<int>(1); // grows into region 2
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());

    CHECK_TRUE(a.restore());

    UNSIGNED_LONGS_EQUAL(regionSize, a.used());         // back to exactly-full region 1
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity()); // region 2 stays linked, just emptied

    // Growing again reuses region 2 rather than allocating a third one.
    a.push<int>(2);
    UNSIGNED_LONGS_EQUAL(regionSize * 2, a.capacity());
}

TEST(Growth, CapacityUsedAndRemainingAggregateAcrossSeveralRegions)
{
    constexpr std::size_t regionSize = kAlign * 2;
    using Chunk = std::array<std::byte, regionSize>;

    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    for (int i = 0; i < 5; ++i)
    {
        auto& chunk = a.push<Chunk>(); // each push exactly fills one whole region
        (void)chunk;
    }

    LONGS_EQUAL((long)arena_error::no_error, (long)a.error());
    UNSIGNED_LONGS_EQUAL(regionSize * 5, a.capacity());
    UNSIGNED_LONGS_EQUAL(regionSize * 5, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.remaining());
}

TEST(Growth, ExpandCustomUsesTheCallbackForTheNextRegionsSize)
{
    auto a = arena::heap(4096, std::function<std::size_t(std::size_t)>(
        [](std::size_t cur) { return cur * 2; }));
    CHECK_TRUE(a.is_valid());
    auto region1 = a.capacity();

    auto fill = a.push_array<std::byte>(region1); // fills region 1 in one call
    (void)fill;
    auto& grown = a.push<int>(1);                  // grows into region 2
    (void)grown;

    UNSIGNED_LONGS_EQUAL(region1 + region1 * 2, a.capacity());
}

TEST(Growth, ExpandCustomReceivesTheCurrentRegionsSizeEachTime)
{
    std::vector<std::size_t> seen;
    auto a = arena::heap(4096, std::function<std::size_t(std::size_t)>(
        [&](std::size_t cur) { seen.push_back(cur); return cur * 2; }));
    auto region1 = a.capacity();

    auto fill1 = a.push_array<std::byte>(region1); // fills region 1
    (void)fill1;
    a.push<int>(1);                                 // grows into region 2 (cb sees region1)

    auto region2 = a.capacity() - region1;
    auto fill2 = a.push_array<std::byte>(region2 - aligned(sizeof(int))); // fills the rest of region 2
    (void)fill2;
    a.push<int>(2);                                 // grows into region 3 (cb sees region2)

    UNSIGNED_LONGS_EQUAL(2u, seen.size());
    UNSIGNED_LONGS_EQUAL(region1, seen[0]);
    UNSIGNED_LONGS_EQUAL(region2, seen[1]);
}

TEST(Growth, ExpandCustomGrowthIsClampedToFitAnOversizedPushEvenIfTheCallbackReturnsLess)
{
    auto a = arena::heap(64, std::function<std::size_t(std::size_t)>(
        [](std::size_t) { return 8; })); // deliberately too small
    auto region1 = a.capacity();

    auto fill = a.push_array<std::byte>(region1);
    (void)fill;

    // The callback says 8, but a push bigger than that still has to fit.
    auto big = a.push_array<std::byte>(kAlign * 2);

    CHECK_FALSE(big.empty());
    UNSIGNED_LONGS_EQUAL(kAlign * 2, big.size());
}

TEST(Growth, FailedGrowthReportsAllocFailedWithoutCorruptingTheArena)
{
    auto a = arena::heap(4096, std::function<std::size_t(std::size_t)>(
        [](std::size_t) { return ~std::size_t{ 0 } / 2; })); // an impossible region size
    auto region1 = a.capacity();

    auto fill = a.push_array<std::byte>(region1);
    (void)fill;

    auto overflow = a.push_array<int>(1);

    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    CHECK_TRUE(a.is_valid());
    UNSIGNED_LONGS_EQUAL(region1, a.capacity());
}

// ---------------------------------------------------------------------------
// Save / restore
// ---------------------------------------------------------------------------
TEST_GROUP(SaveRestore)
{
};

TEST(SaveRestore, RestoreRewindsToTheSavePoint)
{
    auto a = arena::heap(4096);
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

TEST(SaveRestore, RestoreZeroesTheReclaimedBytes)
{
    auto a = arena::heap(4096);
    auto base = a.used();

    a.save();
    auto span = a.push_array<std::byte>(256);
    std::memset(span.data(), 0x5A, span.size());
    a.restore();

    CHECK_TRUE(all_equal(a.data().data() + base, 256, 0x00));
}

TEST(SaveRestore, NoClearArenasKeepTheReclaimedBytes)
{
    alignas(std::max_align_t) unsigned char storage[1024];
    auto a = arena::adopt(storage, sizeof(storage), flags::no_clear);

    a.save();
    auto span = a.push_array<std::byte>(64);
    std::memset(span.data(), 0x77, span.size());
    a.restore();

    CHECK_TRUE(all_equal(a.data().data(), 64, 0x77));
}

TEST(SaveRestore, SavePointsNestLifo)
{
    auto a = arena::heap(4096);

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
    UNSIGNED_LONGS_EQUAL(1u, a.save_depth());

    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(outer, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
}

TEST(SaveRestore, RestoreWithoutASavePointFails)
{
    auto a = arena::heap(4096);

    CHECK_FALSE(a.restore());
    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
}

TEST(SaveRestore, RestoreRewindsTheAllocationCount)
{
    auto a = arena::heap(4096);
    auto before = a.count();

    a.save();
    auto s1 = a.push_array<int>(4);
    (void)s1;
    auto s2 = a.push_array<int>(4);
    (void)s2;
    UNSIGNED_LONGS_EQUAL(before + 2, a.count());

    a.restore();

    UNSIGNED_LONGS_EQUAL(before, a.count());
}

TEST(SaveRestore, SaveStackIsBounded)
{
    auto a = arena::heap(4096);

    for (std::size_t i = 0; i < ENGRAM_MAX_SAVE_STACKSZ; ++i)
        CHECK_TRUE(a.save());

    UNSIGNED_LONGS_EQUAL((std::size_t)ENGRAM_MAX_SAVE_STACKSZ, a.save_depth());
    CHECK_FALSE(a.save());

    for (std::size_t i = 0; i < ENGRAM_MAX_SAVE_STACKSZ; ++i)
        CHECK_TRUE(a.restore());

    CHECK_FALSE(a.restore());
}

TEST(SaveRestore, ResetDropsPendingSavePoints)
{
    auto a = arena::heap(4096);

    a.save();
    a.save();
    UNSIGNED_LONGS_EQUAL(2u, a.save_depth());

    a.reset();

    UNSIGNED_LONGS_EQUAL(0u, a.save_depth());
    CHECK_FALSE(a.restore());
}

TEST(SaveRestore, WorksOnASlice)
{
    auto parent = arena::heap(4096);
    auto sub = parent.slice(1024, 1024);

    sub.save();
    auto span = sub.push_array<int>(32);
    (void)span;
    CHECK_TRUE(sub.used() > 0);

    CHECK_TRUE(sub.restore());

    UNSIGNED_LONGS_EQUAL(0u, sub.used());
    UNSIGNED_LONGS_EQUAL(0u, parent.used());
}

// ---------------------------------------------------------------------------
// Element construction
// ---------------------------------------------------------------------------
TEST_GROUP(Construction)
{
    void setup() override { Tracked::reset(); }
};

TEST(Construction, PushConstructsTypesWithoutADefaultConstructor)
{
    auto a = arena::heap(4096);

    auto& value = a.push<NoDefault>(7);

    LONGS_EQUAL(7, value.value);
}

TEST(Construction, PushArrayWithoutArgsValueInitialisesNonTrivialTypes)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<Tracked>(4);

    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(4, Tracked::constructed);
    for (auto& element : span)
        LONGS_EQUAL(0, element.value);

    a.pop_array(span);
    LONGS_EQUAL(4, Tracked::destroyed);
}

TEST(Construction, PushArrayLeavesTrivialTypesUninitialised)
{
    alignas(std::max_align_t) unsigned char storage[256];
    std::memset(storage, 0x7E, sizeof(storage));
    auto a = arena::adopt(storage, sizeof(storage));

    auto span = a.push_array<int>(4);

    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(0x7E7E7E7E, span[0]);
}

TEST(Construction, CompileTimeAndRuntimeArraysBehaveIdentically)
{
    auto a = arena::heap(4096);

    auto fixed = a.push_array<4, Tracked>(5);
    auto runtime = a.push_array<Tracked>(4, 5);

    UNSIGNED_LONGS_EQUAL(fixed.size(), runtime.size());
    LONGS_EQUAL(8, Tracked::constructed);
    UNSIGNED_LONGS_EQUAL(2u * aligned(4 * sizeof(Tracked) + kGuardSz), a.used());

    a.pop_array(runtime);
    a.pop_array(fixed);

    LONGS_EQUAL(8, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#ifndef ENGRAM_MASK_EXCEPTIONS

// ---------------------------------------------------------------------------
// Exception safety (default: constructor exceptions propagate)
// ---------------------------------------------------------------------------
TEST_GROUP(ExceptionSafety)
{
    void setup() override { Fragile::reset(); }
    void teardown() override { Fragile::reset(); }
};

TEST(ExceptionSafety, PushRethrowsAndRewinds)
{
    auto a = arena::heap(4096);
    auto before = a.used();
    auto count = a.count();

    Fragile::reset(0);
    CHECK_THROWS(Boom, (void)a.push<Fragile>(1));

    UNSIGNED_LONGS_EQUAL(before, a.used());
    UNSIGNED_LONGS_EQUAL(count, a.count());
    LONGS_EQUAL(0, Fragile::live);
}

TEST(ExceptionSafety, PushArrayRethrowsAndDestroysWhatItBuilt)
{
    auto a = arena::heap(4096);
    auto before = a.used();
    auto count = a.count();

    Fragile::reset(3);
    CHECK_THROWS(Boom, (void)a.push_array<Fragile>(8, 1));

    LONGS_EQUAL(4, Fragile::attempts);
    LONGS_EQUAL(0, Fragile::live);
    UNSIGNED_LONGS_EQUAL(before, a.used());
    UNSIGNED_LONGS_EQUAL(count, a.count());
}

TEST(ExceptionSafety, CompileTimeSizedArrayRollsBackTheSameWay)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    Fragile::reset(2);
    CHECK_THROWS(Boom, ((void)a.push_array<4, Fragile>(1)));

    LONGS_EQUAL(0, Fragile::live);
    UNSIGNED_LONGS_EQUAL(before, a.used());
}

TEST(ExceptionSafety, ArenaStaysUsableAfterAThrow)
{
    auto a = arena::heap(4096);
    auto& keep = a.push<int>(11);
    auto anchor = a.used();

    Fragile::reset(0);
    CHECK_THROWS(Boom, (void)a.push_array<Fragile>(4, 1));
    UNSIGNED_LONGS_EQUAL(anchor, a.used());

    Fragile::reset();
    auto span = a.push_array<Fragile>(4, 2);

    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(4, Fragile::live);
    for (auto& element : span)
        LONGS_EQUAL(2, element.value);
    LONGS_EQUAL(11, keep);

    a.pop_array(span);
    LONGS_EQUAL(0, Fragile::live);
    UNSIGNED_LONGS_EQUAL(anchor, a.used());
}

TEST(ExceptionSafety, SaveRestoreIsUnaffectedByAThrow)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    CHECK_TRUE(a.save());
    Fragile::reset(2);
    CHECK_THROWS(Boom, (void)a.push_array<Fragile>(6, 1));

    UNSIGNED_LONGS_EQUAL(1u, a.save_depth());
    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(before, a.used());
}

#else

// ---------------------------------------------------------------------------
// ENGRAM_MASK_EXCEPTIONS: no arena operation throws
// ---------------------------------------------------------------------------
TEST_GROUP(MaskedExceptions)
{
    void setup() override { Fragile::reset(); }
    void teardown() override { Fragile::reset(); }
};

TEST(MaskedExceptions, PushSwallowsTheThrowAndKeepsTheSlot)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    Fragile::reset(0);
    auto& value = a.push<Fragile>(1);
    (void)value;

    // Documented: the storage is reserved but may not hold a constructed object.
    UNSIGNED_LONGS_EQUAL(before + aligned(sizeof(Fragile)), a.used());
    LONGS_EQUAL(0, Fragile::live);
}

TEST(MaskedExceptions, PushArrayReturnsWhatItManagedToBuild)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    Fragile::reset(3);
    auto span = a.push_array<Fragile>(8, 1);

    UNSIGNED_LONGS_EQUAL(3u, span.size());
    LONGS_EQUAL(3, Fragile::live);
    UNSIGNED_LONGS_EQUAL(before + aligned(3 * sizeof(Fragile)), a.used());

    a.pop_array(span);

    LONGS_EQUAL(0, Fragile::live);
    UNSIGNED_LONGS_EQUAL(before, a.used());
}

TEST(MaskedExceptions, PushArrayReturnsAnEmptySpanWhenNothingBuilds)
{
    auto a = arena::heap(4096);
    auto before = a.used();
    auto count = a.count();

    Fragile::reset(0);
    auto span = a.push_array<Fragile>(8, 1);

    CHECK_TRUE(span.empty());
    LONGS_EQUAL(0, Fragile::live);
    UNSIGNED_LONGS_EQUAL(before, a.used());
    UNSIGNED_LONGS_EQUAL(count, a.count());
}

TEST(MaskedExceptions, ThrowingDestructorsAreIgnored)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    NoisyDtor::destroyed = 0;
    auto& value = a.push<NoisyDtor>();
    (void)value;

    a.pop<NoisyDtor>();

    LONGS_EQUAL(1, NoisyDtor::destroyed);
    UNSIGNED_LONGS_EQUAL(before, a.used());
}

#endif // ENGRAM_MASK_EXCEPTIONS
#endif // exceptions enabled

#ifdef ENGRAM_HAS_MDSPAN

// ---------------------------------------------------------------------------
// Multi-dimensional arrays
// ---------------------------------------------------------------------------
TEST_GROUP(MdArray)
{
    void setup() override { Tracked::reset(); }
};

TEST(MdArray, ShapeAndStorageMatchTheExtents)
{
    auto a = arena::heap(4096);

    auto md = a.push_md_array<int, 2>({ 2, 3 });

    UNSIGNED_LONGS_EQUAL(2u, md.rank());
    UNSIGNED_LONGS_EQUAL(2u, md.extent(0));
    UNSIGNED_LONGS_EQUAL(3u, md.extent(1));
    UNSIGNED_LONGS_EQUAL(6u, md.size());
    UNSIGNED_LONGS_EQUAL(aligned(6 * sizeof(int)), a.used());
    POINTERS_EQUAL(a.data().data(), (std::byte*)md.data_handle());
}

TEST(MdArray, IndexingIsRowMajorByDefault)
{
    auto a = arena::heap(4096);

    auto md = a.push_md_array<int, 2>({ 2, 3 });
    for (std::size_t r = 0; r < md.extent(0); ++r)
        for (std::size_t c = 0; c < md.extent(1); ++c)
            md[r, c] = static_cast<int>(r * 10 + c);

    // layout_right lays rows out contiguously.
    auto* flat = md.data_handle();
    LONGS_EQUAL(0, flat[0]);
    LONGS_EQUAL(1, flat[1]);
    LONGS_EQUAL(2, flat[2]);
    LONGS_EQUAL(10, flat[3]);
    LONGS_EQUAL(11, flat[4]);
    LONGS_EQUAL(12, flat[5]);
}

TEST(MdArray, ColumnMajorLayoutIsHonoured)
{
    auto a = arena::heap(4096);

    auto md = a.push_md_array<int, 2, std::layout_left>({ 2, 3 });
    for (std::size_t r = 0; r < md.extent(0); ++r)
        for (std::size_t c = 0; c < md.extent(1); ++c)
            md[r, c] = static_cast<int>(r * 10 + c);

    auto* flat = md.data_handle();
    LONGS_EQUAL(0, flat[0]);
    LONGS_EQUAL(10, flat[1]);
    LONGS_EQUAL(1, flat[2]);
}

TEST(MdArray, EveryElementIsConstructedFromTheGivenArguments)
{
    auto a = arena::heap(4096);

    auto md = a.push_md_array<Tracked, 3>({ 2, 3, 4 }, 9);

    UNSIGNED_LONGS_EQUAL(24u, md.size());
    LONGS_EQUAL(24, Tracked::constructed);
    for (std::size_t i = 0; i < md.extent(0); ++i)
        for (std::size_t j = 0; j < md.extent(1); ++j)
            for (std::size_t k = 0; k < md.extent(2); ++k)
                LONGS_EQUAL(9, (md[i, j, k].value));

    a.pop_md_array(md);

    LONGS_EQUAL(24, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(MdArray, RankIsDeducedFromAnExtentsArray)
{
    auto a = arena::heap(4096);
    const std::array<std::size_t, 3> extents{ 2, 3, 4 };

    auto md = a.push_md_array<float>(extents);

    UNSIGNED_LONGS_EQUAL(3u, md.rank());
    UNSIGNED_LONGS_EQUAL(24u, md.size());
}

TEST(MdArray, OneDimensionalArraysWork)
{
    auto a = arena::heap(4096);

    auto md = a.push_md_array<int, 1>({ 5 }, 3);

    UNSIGNED_LONGS_EQUAL(5u, md.size());
    for (std::size_t i = 0; i < md.extent(0); ++i)
        LONGS_EQUAL(3, (md[i]));
}

TEST(MdArray, ExhaustedArenaReturnsAnEmptyMdspan)
{
    alignas(std::max_align_t) unsigned char storage[64];
    auto a = arena::adopt(storage, sizeof(storage));

    auto md = a.push_md_array<int, 2>({ 64, 64 });

    UNSIGNED_LONGS_EQUAL(0u, md.size());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(MdArray, ParticipatesInSaveRestore)
{
    auto a = arena::heap(4096);
    auto before = a.used();

    CHECK_TRUE(a.save());
    auto md = a.push_md_array<int, 2>({ 4, 4 }, 1);
    UNSIGNED_LONGS_EQUAL(16u, md.size());
    CHECK_TRUE(a.used() > before);

    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(before, a.used());
}

#endif // ENGRAM_HAS_MDSPAN

#ifdef ENGRAM_ENABLE_SOURCE_INFO

// ---------------------------------------------------------------------------
// Creation-site capture
// ---------------------------------------------------------------------------
TEST_GROUP(SourceInfo)
{
};

TEST(SourceInfo, HeapArenaRecordsTheCallSite)
{
    const auto here = std::source_location::current();
    auto a = arena::heap(4096);

    STRCMP_EQUAL(here.file_name(), a.origin().file_name());
    UNSIGNED_LONGS_EQUAL(here.line() + 1, a.origin().line());
}

TEST(SourceInfo, AdoptRecordsTheCallSite)
{
    alignas(std::max_align_t) unsigned char storage[256];

    const auto here = std::source_location::current();
    auto a = arena::adopt(storage, sizeof(storage));

    UNSIGNED_LONGS_EQUAL(here.line() + 1, a.origin().line());
}

TEST(SourceInfo, StackArenaMacroRecordsTheCallSite)
{
    const auto here = std::source_location::current();
    ENGRAM_STACK_ARENA(a, 512);

    STRCMP_EQUAL(here.file_name(), a.origin().file_name());
    UNSIGNED_LONGS_EQUAL(here.line() + 1, a.origin().line());
}

TEST(SourceInfo, OversizedStackArenaStillRecordsWhereItWasAsked)
{
    const auto here = std::source_location::current();
    ENGRAM_STACK_ARENA(a, std::size_t{ 1 } << 40);

    CHECK_FALSE(a.is_valid());
    UNSIGNED_LONGS_EQUAL(here.line() + 1, a.origin().line());
}

TEST(SourceInfo, SliceRecordsItsOwnCallSite)
{
    auto parent = arena::heap(4096);

    const auto here = std::source_location::current();
    auto sub = parent.slice(0, 512);

    UNSIGNED_LONGS_EQUAL(here.line() + 1, sub.origin().line());
    CHECK_TRUE(sub.origin().line() != parent.origin().line());
}

TEST(SourceInfo, OriginSurvivesAMove)
{
    auto source = arena::heap(4096);
    const auto line = source.origin().line();

    auto moved = std::move(source);

    UNSIGNED_LONGS_EQUAL(line, moved.origin().line());
}

#endif // ENGRAM_ENABLE_SOURCE_INFO

#ifdef ENGRAM_TRACK_TYPE_SIZE

// ---------------------------------------------------------------------------
// ENGRAM_TRACK_TYPE_SIZE: the untyped pop()
// ---------------------------------------------------------------------------
TEST_GROUP(EasyPop)
{
    void setup() override { Tracked::reset(); }
};

TEST(EasyPop, PopRewindsASingleObject)
{
    auto a = arena::heap(4096);

    auto& value = a.push<int>(42);
    (void)value;
    UNSIGNED_LONGS_EQUAL(1u, a.push_depth());
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), a.used());

    a.pop();

    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
}

TEST(EasyPop, PopHandlesEveryKindOfPush)
{
    auto a = arena::heap(4096);

    auto& value = a.push<int>(1);
    (void)value;
    auto after_value = a.used();

    auto span = a.push_array<float>(32);
    (void)span;
    auto after_span = a.used();

    auto fixed = a.push_array<8, double>();
    (void)fixed;
    auto after_fixed = a.used();

    auto view = a.push_string(std::string_view{ "engram" });
    (void)view;
    UNSIGNED_LONGS_EQUAL(4u, a.push_depth());

    a.pop();
    UNSIGNED_LONGS_EQUAL(after_fixed, a.used());
    a.pop();
    UNSIGNED_LONGS_EQUAL(after_span, a.used());
    a.pop();
    UNSIGNED_LONGS_EQUAL(after_value, a.used());
    a.pop();

    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
}

TEST(EasyPop, PopZeroesTheReclaimedBytes)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<std::byte>(256);
    std::memset(span.data(), 0x5A, span.size());

    a.pop();

    CHECK_TRUE(all_equal(a.data().data(), 256, 0x00));
}

TEST(EasyPop, NoClearArenasKeepTheReclaimedBytes)
{
    alignas(std::max_align_t) unsigned char storage[1024];
    auto a = arena::adopt(storage, sizeof(storage), flags::no_clear);

    auto span = a.push_array<std::byte>(64);
    std::memset(span.data(), 0x77, span.size());

    a.pop();

    CHECK_TRUE(all_equal(a.data().data(), 64, 0x77));
}

TEST(EasyPop, PopRunsNoDestructors)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<Tracked>(4, 7);
    UNSIGNED_LONGS_EQUAL(4u, span.size());
    LONGS_EQUAL(4, Tracked::constructed);

    a.pop();

    LONGS_EQUAL(0, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(EasyPop, TypedPopsKeepTheRecordStackInStep)
{
    auto a = arena::heap(4096);

    auto& value = a.push<int>(1);
    (void)value;
    auto span = a.push_array<int>(4);
    UNSIGNED_LONGS_EQUAL(2u, a.push_depth());

    a.pop_array(span);
    UNSIGNED_LONGS_EQUAL(1u, a.push_depth());

    a.pop();
    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(EasyPop, ResetAndRestoreRewindTheRecordStack)
{
    auto a = arena::heap(4096);

    auto& anchor = a.push<int>(1);
    (void)anchor;
    CHECK_TRUE(a.save());

    auto span = a.push_array<int>(4);
    (void)span;
    auto view = a.push_string(std::string_view{ "abc" });
    (void)view;
    UNSIGNED_LONGS_EQUAL(3u, a.push_depth());

    CHECK_TRUE(a.restore());
    UNSIGNED_LONGS_EQUAL(1u, a.push_depth());

    a.pop();
    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());

    auto& again = a.push<int>(2);
    (void)again;
    a.reset();
    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
}

TEST(EasyPop, RecordStackIsBounded)
{
    auto a = arena::heap(1 << 16);

    for (std::size_t i = 0; i < ENGRAM_MAX_PUSH_DEPTH; ++i)
    {
        auto& value = a.push<int>(1);
        (void)value;
    }

    UNSIGNED_LONGS_EQUAL((std::size_t)ENGRAM_MAX_PUSH_DEPTH, a.push_depth());

    // One push past the depth limit fails instead of overrunning the record array.
    auto overflow = a.push_array<int>(1);
    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL((std::size_t)ENGRAM_MAX_PUSH_DEPTH, a.push_depth());

    for (std::size_t i = 0; i < ENGRAM_MAX_PUSH_DEPTH; ++i)
        a.pop();

    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(EasyPop, WorksOnASlice)
{
    auto parent = arena::heap(4096);
    auto sub = parent.slice(1024, 1024);

    auto span = sub.push_array<int>(16);
    (void)span;
    UNSIGNED_LONGS_EQUAL(1u, sub.push_depth());

    sub.pop();

    UNSIGNED_LONGS_EQUAL(0u, sub.used());
    UNSIGNED_LONGS_EQUAL(0u, parent.used());
}

TEST(EasyPop, UntypedPopWorksAcrossAGrownRegionBoundary)
{
    auto regionSize = kAlign * 2;
    auto a = arena::create(memory_source::heap, regionSize, flags::expand);

    auto fill = a.push_array<std::byte>(regionSize); // fills region 1
    (void)fill;
    auto& grown = a.push<int>(9);                    // grows into region 2
    (void)grown;
    UNSIGNED_LONGS_EQUAL(2u, a.push_depth());

    a.pop(); // rewinds the int in region 2
    UNSIGNED_LONGS_EQUAL(regionSize, a.used());

    a.pop(); // rewinds the push_array, back in region 1
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.push_depth());
}

#endif // ENGRAM_TRACK_TYPE_SIZE

// ---------------------------------------------------------------------------
// pop(T&): dispatching on what was pushed
// ---------------------------------------------------------------------------
TEST_GROUP(PopDispatch)
{
    void setup() override { Tracked::reset(); }
};

TEST(PopDispatch, PlainObjectsRunTheirDestructor)
{
    auto a = arena::heap(4096);

    auto& value = a.push<Tracked>(5);
    LONGS_EQUAL(1, Tracked::constructed);

    a.pop(value);

    LONGS_EQUAL(1, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
}

TEST(PopDispatch, SpansRouteToPopArray)
{
    auto a = arena::heap(4096);

    auto span = a.push_array<Tracked>(4, 9);
    LONGS_EQUAL(4, Tracked::constructed);

    a.pop(span);

    LONGS_EQUAL(4, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(PopDispatch, StringViewsRouteToPopString)
{
    auto a = arena::heap(4096);

    auto view = a.push_string(std::string_view{ "engram" });
    UNSIGNED_LONGS_EQUAL(aligned(7 + kGuardSz), a.used());

    a.pop(view);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

// The std::span<T> overload of pop_array cannot deduce from a static extent.
TEST(PopDispatch, StaticExtentSpansRewindExactly)
{
    auto a = arena::heap(4096);

    auto dynamic = a.push_array<int>(8);
    std::span<int, 8> fixed{ dynamic.data(), 8 };

    a.pop(fixed);

    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(PopDispatch, ConstHandlesAreAccepted)
{
    auto a = arena::heap(4096);

    const auto span = a.push_array<Tracked>(3);
    const auto view = a.push_string(std::string_view{ "hi" });

    a.pop(view);
    a.pop(span);

    LONGS_EQUAL(3, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

TEST(PopDispatch, UnwindsAMixedStackInReverse)
{
    auto a = arena::heap(4096);

    auto& one = a.push<Tracked>(1);
    auto many = a.push_array<Tracked>(2, 2);
    auto text = a.push_string(std::string_view{ "tail" });

    const auto expected = aligned(sizeof(Tracked)) + aligned(2 * sizeof(Tracked) + kGuardSz) + aligned(5 + kGuardSz);
    UNSIGNED_LONGS_EQUAL(expected, a.used());
    LONGS_EQUAL(3, Tracked::constructed);

    a.pop(text);
    a.pop(many);
    a.pop(one);

    LONGS_EQUAL(3, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
    UNSIGNED_LONGS_EQUAL(0u, a.count());
}

#ifdef ENGRAM_HAS_MDSPAN

TEST(PopDispatch, MdSpansRouteToPopMdArray)
{
    auto a = arena::heap(4096);

    auto grid = a.push_md_array<Tracked, 2>({ 2, 3 }, 4);
    LONGS_EQUAL(6, Tracked::constructed);

    a.pop(grid);

    LONGS_EQUAL(6, Tracked::destroyed);
    UNSIGNED_LONGS_EQUAL(0u, a.used());
}

#endif // ENGRAM_HAS_MDSPAN

// ---------------------------------------------------------------------------
// Cache hints and paging (advisory: the calls must be harmless, not observable)
// ---------------------------------------------------------------------------
TEST_GROUP(CacheHints)
{
};

TEST(CacheHints, WarmCacheLeavesTheContentsAlone)
{
    auto a = arena::heap(8192);
    auto span = a.push_array<std::byte>(4096);
    fill(span, 0x5A);

    a.warm_cache(engram::cache_locality::L1, flags::read);
    a.warm_cache(engram::cache_locality::L2, flags::write, 64, 512);
    a.warm_cache(span.data(), engram::cache_locality::L3, flags::read);

    CHECK_TRUE(all_equal(span.data(), span.size(), 0x5A));
}

TEST(CacheHints, WarmCacheCoversTypesLargerThanACacheLine)
{
    struct Big { std::byte bytes[256]; };

    auto a = arena::heap(4096);
    auto& big = a.push<Big>();
    std::memset(&big, 0x77, sizeof(Big));

    a.warm_cache(&big, engram::cache_locality::L1, flags::read);
    engram::warm_cache(&big, engram::cache_locality::L1, flags::read);

    CHECK_TRUE(all_equal(reinterpret_cast<std::byte*>(&big), sizeof(Big), 0x77));
}

TEST(CacheHints, WarmCacheToleratesEdgeRanges)
{
    auto a = arena::heap(4096);

    engram::warm_cache(static_cast<std::byte*>(nullptr), 4096, engram::cache_locality::L1, flags::read);
    engram::warm_cache(a.data().data(), 0, engram::cache_locality::L1, flags::read);
    a.warm_cache(engram::cache_locality::L1, flags::read, a.capacity(), 0);

    CHECK_TRUE(a.is_valid());
}

TEST(CacheHints, PrefetchAcceptsAHeapRange)
{
    auto a = arena::heap(8192);
    auto span = a.push_array<int>(256);
    fill(std::as_writable_bytes(span), 0x11);

    // Element counts, not bytes: the typed overload scales by sizeof(T).
    engram::prefetch(span.data(), span.size());
    engram::prefetch(a.data().data(), a.capacity());

    CHECK_TRUE(all_equal(reinterpret_cast<std::byte*>(span.data()), span.size() * sizeof(int), 0x11));
}

TEST(CacheHints, PrefetchRejectsEmptyRanges)
{
    auto a = arena::heap(4096);

    CHECK_FALSE(engram::prefetch(static_cast<std::byte*>(nullptr), 4096));
    CHECK_FALSE(engram::prefetch(a.data().data(), std::size_t{ 0 }));
}

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
