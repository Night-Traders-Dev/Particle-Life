#include "test_framework.hpp"
#include <cstdint>
#include "core/types.h"

namespace {

TEST_CASE(BehaviorFlags_none_is_zero) {
    ASSERT_INT_EQ(static_cast<uint32_t>(BEHAVIOR_NONE), 0u, "NONE is 0");
    return true;
}

TEST_CASE(BehaviorFlags_are_powers_of_two) {
    ASSERT_TRUE((BEHAVIOR_REPEL    & (BEHAVIOR_REPEL    - 1)) == 0, "REPEL is power of 2");
    ASSERT_TRUE((BEHAVIOR_POLAR    & (BEHAVIOR_POLAR    - 1)) == 0, "POLAR is power of 2");
    ASSERT_TRUE((BEHAVIOR_HEAVY    & (BEHAVIOR_HEAVY    - 1)) == 0, "HEAVY is power of 2");
    ASSERT_TRUE((BEHAVIOR_CATALYST & (BEHAVIOR_CATALYST - 1)) == 0, "CATALYST is power of 2");
    ASSERT_TRUE((BEHAVIOR_VIRAL    & (BEHAVIOR_VIRAL    - 1)) == 0, "VIRAL is power of 2");
    ASSERT_TRUE((BEHAVIOR_LEECH    & (BEHAVIOR_LEECH    - 1)) == 0, "LEECH is power of 2");
    ASSERT_TRUE((BEHAVIOR_SHIELD   & (BEHAVIOR_SHIELD   - 1)) == 0, "SHIELD is power of 2");
    ASSERT_TRUE((BEHAVIOR_POSITIVE & (BEHAVIOR_POSITIVE - 1)) == 0, "POSITIVE is power of 2");
    ASSERT_TRUE((BEHAVIOR_NEGATIVE & (BEHAVIOR_NEGATIVE - 1)) == 0, "NEGATIVE is power of 2");
    ASSERT_TRUE((BEHAVIOR_FOOD     & (BEHAVIOR_FOOD     - 1)) == 0, "FOOD is power of 2");
    ASSERT_TRUE((BEHAVIOR_PREDATOR & (BEHAVIOR_PREDATOR - 1)) == 0, "PREDATOR is power of 2");
    ASSERT_TRUE((BEHAVIOR_SIGNALER & (BEHAVIOR_SIGNALER - 1)) == 0, "SIGNALER is power of 2");
    return true;
}

TEST_CASE(BehaviorFlags_no_overlap) {
    uint32_t flags[] = {
        BEHAVIOR_REPEL, BEHAVIOR_POLAR, BEHAVIOR_HEAVY, BEHAVIOR_CATALYST,
        BEHAVIOR_VIRAL, BEHAVIOR_LEECH, BEHAVIOR_SHIELD, BEHAVIOR_POSITIVE,
        BEHAVIOR_NEGATIVE, BEHAVIOR_FOOD, BEHAVIOR_PREDATOR, BEHAVIOR_SIGNALER
    };
    for (size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(flags)/sizeof(flags[0]); ++j) {
            ASSERT_TRUE((flags[i] & flags[j]) == 0, "no overlap between flags");
        }
    }
    return true;
}

TEST_CASE(BehaviorFlags_bit_operations) {
    uint32_t flags = BEHAVIOR_HEAVY | BEHAVIOR_POSITIVE;
    ASSERT_TRUE((flags & BEHAVIOR_HEAVY) != 0, "HEAVY set");
    ASSERT_TRUE((flags & BEHAVIOR_POSITIVE) != 0, "POSITIVE set");
    ASSERT_TRUE((flags & BEHAVIOR_VIRAL) == 0, "VIRAL not set");
    return true;
}

TEST_CASE(BehaviorFlags_atomicOr_and) {
    uint32_t flags = BEHAVIOR_NONE;
    uint32_t bit = 1u << 3; // CATALYST
    flags |= bit;
    ASSERT_TRUE((flags & BEHAVIOR_CATALYST) != 0, "atomicOr sets bit");
    flags &= ~bit;
    ASSERT_TRUE((flags & BEHAVIOR_CATALYST) == 0, "atomicAnd clears bit");
    return true;
}

TEST_CASE(BehaviorFlags_mutation_range_0_to_8) {
    for (int i = 0; i < 100; ++i) {
        float r = (float)i / 100.0f;
        uint32_t bit = 1u << uint32_t(r * 9.0f);
        ASSERT_TRUE(bit <= 1u << 8, "mutation bit in range 0-8");
    }
    return true;
}

} // anonymous

test::TestResult run_behavior_flag_tests() {
    test::print_header("Behavior Flags");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("BehaviorFlag") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
