// Tests for ENGRAM_ENABLE_DIAGNOSTICS (header + source build only): the
// push_array/push_string overwrite canary, the push/pop depth balance counter,
// and the RTTI type/size check pop<T>/pop_array<T>/pop_string run against what
// was actually pushed.
//
// Every test here is a happy-path: push and pop stay balanced and typed calls
// always match what was pushed. The library aborts via assert() on a genuine
// misuse (mismatched type, over-pop, or a non-empty arena at destruction), so
// those paths are not exercised in-process here — verified manually instead.
//
// engram.h must be included before any CppUTest header: CppUTest's leak detector
// defines a `new` macro that would otherwise rewrite the library's placement-new
// expressions.
#include "engram.h"

#include <cstdint>
#include <string_view>

#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/TestHarness.h>

using engram::arena;

// ENGRAM_ENABLE_DIAGNOSTICS also requires _DEBUG; outside debug this whole file
// compiles away to nothing. Keep one unconditional test so CppUTest always has
// something to run instead of reporting zero tests as a failure.
TEST_GROUP(Sanity)
{
};

TEST(Sanity, ArenaConstructsAndDestroysCleanly)
{
    auto a = arena::heap(4096);
    CHECK_TRUE(a.is_valid());
}

#ifdef ENGRAM_ENABLE_DIAGNOSTICS

namespace {

constexpr std::size_t kAlign = alignof(std::max_align_t);

constexpr std::size_t aligned(std::size_t n)
{
    return (n + (kAlign - 1)) & ~(kAlign - 1);
}

// Throws out of the constructor on the `throw_at`-th attempt.
struct Boom : std::runtime_error
{
    Boom() : std::runtime_error("boom") {}
};

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

} // namespace

// ---------------------------------------------------------------------------
// Overwrite detection: push_array/push_string's trailing canary.
// ---------------------------------------------------------------------------
TEST_GROUP(OverwriteDetection)
{
};

TEST(OverwriteDetection, GuardBytesFollowAPushedArray)
{
    auto a = arena::heap(4096);
    auto span = a.push_array<int>(4);

    auto guard = reinterpret_cast<const unsigned char*>(span.data() + span.size());
    for (int i = 0; i < 4; ++i)
        LONGS_EQUAL(0xCD, guard[i]);

    a.pop_array(span);   // untouched guard pops cleanly
}

TEST(OverwriteDetection, GuardBytesFollowAPushedString)
{
    auto a = arena::heap(4096);
    auto str = a.push_string(std::string_view{ "hi" });

    // The NUL terminator sits at str.data() + str.size(); the guard follows that.
    auto guard = reinterpret_cast<const unsigned char*>(str.data() + str.size() + 1);
    for (int i = 0; i < 4; ++i)
        LONGS_EQUAL(0xCD, guard[i]);

    a.pop_string(str);
}

TEST(OverwriteDetection, ReservationGrowsByTheGuardSize)
{
    auto a = arena::heap(4096);
    auto before = a.remaining();

    auto span = a.push_array<std::byte>(16);

    // 16 data bytes + 4 guard bytes, rounded up to the max-align quantum.
    UNSIGNED_LONGS_EQUAL(aligned(16 + 4), before - a.remaining());
    a.pop_array(span);
}

#ifdef ENGRAM_MASK_EXCEPTIONS
TEST(OverwriteDetection, PartialArrayStillGetsAGuardAfterTheBuiltElements)
{
    Fragile::reset(2);
    auto a = arena::heap(4096);

    auto span = a.push_array<Fragile>(5);

    UNSIGNED_LONGS_EQUAL(2u, span.size());
    auto guard = reinterpret_cast<const unsigned char*>(span.data() + span.size());
    for (int i = 0; i < 4; ++i)
        LONGS_EQUAL(0xCD, guard[i]);

    a.pop_array(span);
}
#endif

// ---------------------------------------------------------------------------
// Push/pop depth balance: every push must be matched by a pop before the arena
// is destroyed, or an assert fires (in pop() for an over-pop, in the arena's
// destructor for an under-pop). These tests stay balanced throughout.
// ---------------------------------------------------------------------------
TEST_GROUP(PushPopBalance)
{
};

TEST(PushPopBalance, SingleValueStaysBalanced)
{
    auto a = arena::heap(4096);
    auto& v = a.push<int>(42);
    LONGS_EQUAL(42, v);
    a.pop<int>();
}

TEST(PushPopBalance, ArrayAndStringStayBalanced)
{
    auto a = arena::heap(4096);
    auto span = a.push_array<int>(8);
    auto str = a.push_string(std::string_view{ "engram" });

    // LIFO: pop in the reverse order they were pushed.
    a.pop_string(str);
    a.pop_array(span);
}

TEST(PushPopBalance, NestedPushesPopInReverseOrder)
{
    auto a = arena::heap(4096);
    auto& one = a.push<int>(1);
    auto many = a.push_array<double>(3);
    auto text = a.push_string(std::string_view{ "tail" });
    (void)one;

    a.pop_string(text);
    a.pop_array(many);
    a.pop<int>();
}

#ifdef ENGRAM_TRACK_TYPE_SIZE
TEST(PushPopBalance, UntypedPopAlsoCountsAgainstTheDepth)
{
    auto a = arena::heap(4096);
    a.push<int>(7);

    a.pop();   // untyped pop() has no type to check, but still balances the depth
}
#endif

// ---------------------------------------------------------------------------
// RTTI type/size validation: pop<T>/pop_array<T>/pop_string check the template
// type against what push<T>/push_array<T>/push_string recorded.
// ---------------------------------------------------------------------------
TEST_GROUP(TypeValidation)
{
};

TEST(TypeValidation, PopWithTheSameTypeSucceeds)
{
    auto a = arena::heap(4096);
    a.push<double>(3.5);
    a.pop<double>();
}

TEST(TypeValidation, PopArrayWithTheSameTypeSucceeds)
{
    auto a = arena::heap(4096);
    auto span = a.push_array<float>(6);
    a.pop_array(span);
}

TEST(TypeValidation, PopStringWithTheSameCharTypeSucceeds)
{
    auto a = arena::heap(4096);
    auto str = a.push_string(std::string_view{ "hi" });
    a.pop_string(str);
}

#endif // ENGRAM_ENABLE_DIAGNOSTICS

int main(int argc, char** argv)
{
    return CommandLineTestRunner::RunAllTests(argc, argv);
}
