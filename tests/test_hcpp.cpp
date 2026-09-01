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

} // namespace

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
    UNSIGNED_LONGS_EQUAL(aligned(64 * sizeof(int)), a.used());
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
    UNSIGNED_LONGS_EQUAL(aligned(7), a.used());

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
    alignas(std::max_align_t) unsigned char storage[128];
    auto a = arena::adopt(storage, sizeof(storage));

    auto ok = a.push_array<std::byte>(128);
    UNSIGNED_LONGS_EQUAL(128u, ok.size());
    UNSIGNED_LONGS_EQUAL(0u, a.remaining());

    auto overflow = a.push_array<std::byte>(1);
    CHECK_TRUE(overflow.empty());
    LONGS_EQUAL((long)arena_error::alloc_failed, (long)a.error());
    UNSIGNED_LONGS_EQUAL(128u, a.used());
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
// Partitions (sub-arenas)
// ---------------------------------------------------------------------------
TEST_GROUP(Partition)
{
};

TEST(Partition, IsAnExternalViewOfTheParent)
{
    auto parent = arena::heap(4096);

    auto sub = parent.partition(1024, 512);

    LONGS_EQUAL((long)memory_source::external, (long)sub.source());
    UNSIGNED_LONGS_EQUAL(512u, sub.capacity());
    POINTERS_EQUAL(parent.data().data() + 1024, sub.data().data());
}

TEST(Partition, DoesNotDisturbTheParentOffset)
{
    auto parent = arena::heap(4096);
    auto& anchor = parent.push<int>(1);
    (void)anchor;
    auto used = parent.used();

    auto sub = parent.partition(2048, 512);
    auto span = sub.push_array<int>(16);
    (void)span;

    UNSIGNED_LONGS_EQUAL(used, parent.used());
    UNSIGNED_LONGS_EQUAL(aligned(16 * sizeof(int)), sub.used());
}

TEST(Partition, IsZeroedByDefault)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.partition(1024, 256);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0x00));
    // Bytes outside the region are left as they were.
    CHECK_TRUE(all_equal(parent.data().data(), 1024, 0xEE));
    CHECK_TRUE(all_equal(parent.data().data() + 1280, 256, 0xEE));
}

TEST(Partition, NoClearKeepsTheExistingBytes)
{
    auto parent = arena::heap(4096);
    fill(parent.data(), 0xEE);

    auto sub = parent.partition(1024, 256, flags::no_clear);

    CHECK_TRUE(all_equal(sub.data().data(), sub.data().size(), 0xEE));
}

TEST(Partition, DestroyingItLeavesTheParentIntact)
{
    auto parent = arena::heap(4096);
    auto& anchor = parent.push<int>(99);

    {
        auto sub = parent.partition(2048, 256);
        auto span = sub.push_array<int>(8);
        (void)span;
    }

    CHECK_TRUE(parent.is_valid());
    LONGS_EQUAL(99, anchor);
    UNSIGNED_LONGS_EQUAL(aligned(sizeof(int)), parent.used());
}

TEST(Partition, SiblingsAreIndependent)
{
    auto parent = arena::heap(4096);

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

TEST(SaveRestore, WorksOnAPartition)
{
    auto parent = arena::heap(4096);
    auto sub = parent.partition(1024, 1024);

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
    UNSIGNED_LONGS_EQUAL(2u * aligned(4 * sizeof(Tracked)), a.used());

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

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
