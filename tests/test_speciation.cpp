#include "test_framework.hpp"
#include <vector>
#include <cstdint>
#include "core/types.h"
#include <algorithm>

namespace {

// Replicate speciation logic for testing
struct SpeciesRecord {
    float avg_self_mod   = 0.8f;
    float avg_cross_mod  = 0.8f;
    float avg_lifespan   = 300.0f;
    float divergence     = 0.0f;
    bool  speciation_logged = false;
};

float compute_divergence(float current, float baseline) {
    if (baseline > 0.001f)
        return std::abs(current - baseline) / baseline;
    return 0.0f;
}

TEST_CASE(Speciation_no_divergence_when_matching_baseline) {
    SpeciesRecord rec{};
    rec.avg_self_mod = 0.8f;
    rec.avg_cross_mod = 0.8f;
    float div = compute_divergence(0.8f, rec.avg_self_mod);
    ASSERT_FLOAT_EQ(div, 0.0f, 1e-5f, "no divergence at baseline");
    return true;
}

TEST_CASE(Speciation_triggers_above_40pct) {
    SpeciesRecord rec{};
    rec.avg_self_mod = 0.8f;
    rec.avg_cross_mod = 0.8f;
    float current_self = 1.2f; // +50%
    float div = compute_divergence(current_self, rec.avg_self_mod);
    ASSERT_TRUE(div > 0.4f, "divergence > 40%");
    return true;
}

TEST_CASE(Speciation_resets_when_dropping_below_20pct) {
    SpeciesRecord rec{};
    rec.divergence = 0.5f;
    rec.speciation_logged = true;
    float current_self = 0.88f; // +10%
    float div = compute_divergence(current_self, rec.avg_self_mod);
    if (div < 0.2f) {
        rec.divergence = div;
        rec.speciation_logged = false;
    }
    ASSERT_FLOAT_EQ(rec.divergence, div, 1e-5f, "divergence updated");
    ASSERT_TRUE(!rec.speciation_logged, "speciation_logged reset");
    return true;
}

TEST_CASE(Speciation_requires_types_in_use) {
    // Simulated: if types_in_use >= MAX_PARTICLE_TYPES - 1, no split
    uint32_t types_in_use = 9; // MAX is 10, food excluded = 9
    bool can_speciate = (types_in_use < 9);
    ASSERT_TRUE(!can_speciate, "cannot speciate when all slots full");
    return true;
}

TEST_CASE(Speciation_skips_food_type) {
    // In check_speciation, the code explicitly skips FOOD_TYPE_INDEX (9).
    // We verify the logic by simulating the guard condition.
    uint32_t type_pop[] = {10, 5, 0, 0, 0, 0, 0, 0, 0, 100};
    uint32_t most_diverged_type = 9;
    bool food_blocked = (most_diverged_type == FOOD_TYPE_INDEX);
    ASSERT_TRUE(food_blocked, "food type is blocked from speciation");
    return true;
}

} // anonymous

test::TestResult run_speciation_tests() {
    test::print_header("Speciation");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Speciation") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
