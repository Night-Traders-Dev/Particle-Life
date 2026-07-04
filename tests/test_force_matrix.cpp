#include "test_framework.hpp"
#include "core/types.h"

namespace {

TEST_CASE(ForceMatrix_size_is_196) {
    ASSERT_INT_EQ(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 196, "14x14 matrix");
    return true;
}

TEST_CASE(ForceMatrix_index_convention) {
    // forces[a + b * MAX_PARTICLE_TYPES] = effect of b on a
    uint32_t a = 3, b = 5;
    uint32_t idx = a + b * MAX_PARTICLE_TYPES;
    ASSERT_INT_EQ(idx, 3 + 5 * 14, "force matrix index convention");
    return true;
}

TEST_CASE(ForceMatrix_food_self_repels) {
    float forces[196] = {};
    forces[FOOD_TYPE_INDEX + FOOD_TYPE_INDEX * MAX_PARTICLE_TYPES] = -0.1f;
    float f = forces[9 + 9 * 14];
    ASSERT_FLOAT_EQ(f, -0.1f, 1e-4f, "food self-repulsion");
    return true;
}

TEST_CASE(ForceMatrix_food_attraction_positive) {
    float forces[196] = {};
    // type 3 attracted to food
    forces[3 + FOOD_TYPE_INDEX * MAX_PARTICLE_TYPES] = 0.5f;
    float f = forces[3 + 9 * 14];
    ASSERT_FLOAT_EQ(f, 0.5f, 1e-4f, "herbivore attracted to food");
    return true;
}

TEST_CASE(ForceMatrix_symmetric_index) {
    float forces[196] = {};
    forces[0 + 1 * 14] = 0.8f;
    forces[1 + 0 * 14] = 0.8f;
    ASSERT_FLOAT_EQ(forces[0 + 1 * 14], forces[1 + 0 * 14], 1e-4f, "symmetric forces");
    return true;
}

} // anonymous

test::TestResult run_force_matrix_tests() {
    test::print_header("Force Matrix");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("ForceMatrix") == 0 || std::string(t.name).find("Force") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
