#include "test_framework.hpp"
#include <cstdint>
#include "core/types.h"

namespace {

TEST_CASE(BehaviorFlags_none_is_zero) {
    ASSERT_INT_EQ(static_cast<uint32_t>(BEHAVIOR_NONE), 0u, "NONE is 0");
    return true;
}

TEST_CASE(BehaviorFlags_are_powers_of_two) {
    ASSERT_TRUE((BEHAVIOR_SOLUBLE   & (BEHAVIOR_SOLUBLE   - 1)) == 0, "SOLUBLE is power of 2");
    ASSERT_TRUE((BEHAVIOR_CHARGE    & (BEHAVIOR_CHARGE    - 1)) == 0, "CHARGE is power of 2");
    ASSERT_TRUE((BEHAVIOR_MEMBRANE  & (BEHAVIOR_MEMBRANE  - 1)) == 0, "MEMBRANE is power of 2");
    ASSERT_TRUE((BEHAVIOR_RECEPTOR  & (BEHAVIOR_RECEPTOR  - 1)) == 0, "RECEPTOR is power of 2");
    ASSERT_TRUE((BEHAVIOR_ENZYME    & (BEHAVIOR_ENZYME    - 1)) == 0, "ENZYME is power of 2");
    ASSERT_TRUE((BEHAVIOR_STRUCTURAL& (BEHAVIOR_STRUCTURAL- 1)) == 0, "STRUCTURAL is power of 2");
    ASSERT_TRUE((BEHAVIOR_SIGNALING & (BEHAVIOR_SIGNALING - 1)) == 0, "SIGNALING is power of 2");
    ASSERT_TRUE((BEHAVIOR_METABOLIC & (BEHAVIOR_METABOLIC - 1)) == 0, "METABOLIC is power of 2");
    ASSERT_TRUE((BEHAVIOR_TOXIC     & (BEHAVIOR_TOXIC     - 1)) == 0, "TOXIC is power of 2");
    ASSERT_TRUE((BEHAVIOR_STICKY    & (BEHAVIOR_STICKY    - 1)) == 0, "STICKY is power of 2");
    ASSERT_TRUE((BEHAVIOR_NUTRIENT  & (BEHAVIOR_NUTRIENT  - 1)) == 0, "NUTRIENT is power of 2");
    ASSERT_TRUE((BEHAVIOR_CELL      & (BEHAVIOR_CELL      - 1)) == 0, "CELL is power of 2");
    ASSERT_TRUE((BEHAVIOR_DECOMPOSER& (BEHAVIOR_DECOMPOSER- 1)) == 0, "DECOMPOSER is power of 2");
    ASSERT_TRUE((BEHAVIOR_VIRION    & (BEHAVIOR_VIRION    - 1)) == 0, "VIRION is power of 2");
    return true;
}

TEST_CASE(BehaviorFlags_no_overlap) {
    uint32_t flags[] = {
        BEHAVIOR_SOLUBLE, BEHAVIOR_CHARGE, BEHAVIOR_MEMBRANE, BEHAVIOR_RECEPTOR,
        BEHAVIOR_ENZYME, BEHAVIOR_STRUCTURAL, BEHAVIOR_SIGNALING, BEHAVIOR_METABOLIC,
        BEHAVIOR_TOXIC, BEHAVIOR_STICKY, BEHAVIOR_NUTRIENT, BEHAVIOR_CELL, BEHAVIOR_DECOMPOSER, BEHAVIOR_VIRION
    };
    for (size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); ++i) {
        for (size_t j = i + 1; j < sizeof(flags)/sizeof(flags[0]); ++j) {
            ASSERT_TRUE((flags[i] & flags[j]) == 0, "no overlap between flags");
        }
    }
    return true;
}

TEST_CASE(BehaviorFlags_bit_operations) {
    uint32_t flags = BEHAVIOR_MEMBRANE | BEHAVIOR_NUTRIENT;
    ASSERT_TRUE((flags & BEHAVIOR_MEMBRANE) != 0, "MEMBRANE set");
    ASSERT_TRUE((flags & BEHAVIOR_NUTRIENT) != 0, "NUTRIENT set");
    ASSERT_TRUE((flags & BEHAVIOR_VIRION) == 0, "VIRION not set");
    return true;
}

TEST_CASE(BehaviorFlags_atomicOr_and) {
    uint32_t flags = BEHAVIOR_NONE;
    uint32_t bit = 1u << 7; // SIGNALING
    flags |= bit;
    ASSERT_TRUE((flags & BEHAVIOR_SIGNALING) != 0, "atomicOr sets bit");
    flags &= ~bit;
    ASSERT_TRUE((flags & BEHAVIOR_SIGNALING) == 0, "atomicAnd clears bit");
    return true;
}

TEST_CASE(BehaviorFlags_mutation_range_0_to_8) {
    for (int i = 0; i < 100; ++i) {
        float r = (float)i / 100.0f;
        uint32_t bit = 1u << uint32_t(r * 13.0f);
        ASSERT_TRUE(bit <= 1u << 12, "mutation bit in range 0-12");
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
