#include "test_framework.hpp"
#include <random>
#include <cmath>
#include <cstdint>

namespace {

class DeterministicRNG {
public:
    DeterministicRNG(uint32_t seed) : rng_(seed) {}
    float range(float lo, float hi) {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng_);
    }
    uint32_t range(uint32_t lo, uint32_t hi) {
        std::uniform_int_distribution<uint32_t> d(lo, hi);
        return d(rng_);
    }
private:
    std::mt19937 rng_;
};

TEST_CASE(RNG_determinism_same_seed) {
    DeterministicRNG rng1(42), rng2(42);
    float a = rng1.range(0.0f, 1.0f);
    float b = rng2.range(0.0f, 1.0f);
    ASSERT_FLOAT_EQ(a, b, 1e-6f, "same seed -> same value");
    return true;
}

TEST_CASE(RNG_different_seeds_differ) {
    DeterministicRNG rng1(1), rng2(2);
    float a = rng1.range(0.0f, 1.0f);
    float b = rng2.range(0.0f, 1.0f);
    ASSERT_TRUE(a != b, "different seeds produce different values");
    return true;
}

TEST_CASE(RNG_range_within_bounds) {
    DeterministicRNG rng(12345);
    for (int i = 0; i < 100; ++i) {
        float v = rng.range(0.0f, 10.0f);
        ASSERT_TRUE(v >= 0.0f && v <= 10.0f, "float in range");
        uint32_t u = rng.range(5u, 15u);
        ASSERT_TRUE(u >= 5u && u <= 15u, "uint in range");
    }
    return true;
}

TEST_CASE(RNG_integer_distribution_span) {
    DeterministicRNG rng(99);
    std::uniform_int_distribution<int> dist(0, 9);
    // Use the public range method instead of accessing private member
    uint32_t v = rng.range(0u, 9u);
    ASSERT_TRUE(v >= 0u && v <= 9u, "int in [0,9]");
    return true;
}

} // anonymous

test::TestResult run_rng_tests() {
    test::print_header("RNG");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("RNG") == 0 || std::string(t.name).find("Rng") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
