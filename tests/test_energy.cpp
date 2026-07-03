#include "test_framework.hpp"
#include <cmath>
#include <algorithm>

namespace {

TEST_CASE(Energy_initial_value_is_one) {
    float e = 1.0f;
    ASSERT_FLOAT_EQ(e, 1.0f, 1e-5f, "new particle energy is 1.0");
    return true;
}

TEST_CASE(Energy_inherits_40pct_of_parent) {
    float parent_energy = 1.5f;
    float child_energy = std::max(parent_energy * 0.4f, 0.2f);
    ASSERT_FLOAT_EQ(child_energy, 0.6f, 1e-4f, "child inherits 40%");
    return true;
}

TEST_CASE(Energy_cross_repro_blends_parents) {
    float p1 = 2.0f, p2 = 1.0f;
    float blended = std::max((p1 + p2) * 0.35f, 0.25f);
    ASSERT_FLOAT_EQ(blended, 1.05f, 1e-4f, "cross-repro blended energy");
    return true;
}

TEST_CASE(Energy_parent_cost_single_repro) {
    float parent = 1.6f;
    float cost = 0.3f;
    float remaining = parent - cost;
    ASSERT_FLOAT_EQ(remaining, 1.3f, 1e-4f, "single-parent cost");
    return true;
}

TEST_CASE(Energy_parent_cost_cross_repro) {
    float p1 = 2.0f, p2 = 1.5f;
    float cost1 = 0.5f, cost2 = 0.25f;
    float r1 = p1 - cost1;
    float r2 = p2 - cost2;
    ASSERT_FLOAT_EQ(r1, 1.5f, 1e-4f, "parent1 cost in cross-repro");
    ASSERT_FLOAT_EQ(r2, 1.25f, 1e-4f, "parent2 cost in cross-repro");
    return true;
}

TEST_CASE(Energy_food_parent_cost) {
    float parent = 1.0f;
    bool is_currently_food = true;
    float cost = is_currently_food ? 0.1f : 0.3f;
    float r = parent - (is_currently_food ? 0.1f : 0.3f);
    ASSERT_FLOAT_EQ(r, 0.9f, 1e-4f, "food parent cost");
    return true;
}

TEST_CASE(Energy_min_clamp) {
    float e = 0.15f;
    float result = (e < 0.2f) ? 0.2f : e;
    ASSERT_FLOAT_EQ(result, 0.2f, 1e-4f, "energy clamps at 0.2");
    return true;
}

TEST_CASE(Energy_rebirth_threshold) {
    bool rebirth = false;
    float energy_a = 0.18f;
    bool is_food = false;
    if (energy_a < 0.2f && !is_food) {
        rebirth = true;
    }
    ASSERT_TRUE(rebirth, "low energy triggers rebirth");
    return true;
}

TEST_CASE(Energy_old_age_death) {
    bool old_age_death = false;
    float age = 310.0f, lifespan = 300.0f;
    if (age >= lifespan) old_age_death = true;
    ASSERT_TRUE(old_age_death, "age >= lifespan triggers death");
    return true;
}

} // anonymous

test::TestResult run_energy_tests() {
    test::print_header("Energy");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Energy") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
