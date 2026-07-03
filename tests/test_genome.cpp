#include "test_framework.hpp"
#include <glm/glm.hpp>

namespace {

TEST_CASE(GenomeData_has_correct_size_and_alignment) {
    static_assert(sizeof(GenomeData) == 32, "GenomeData must be 32 bytes");
    static_assert(alignof(GenomeData) == 16, "GenomeData must be 16-byte aligned");
    return true;
}

TEST_CASE(GenomeData_defaults_are_valid) {
    GenomeData g = {};
    ASSERT_FLOAT_EQ(g.age, 0.0f, 1e-6f, "default age should be 0");
    ASSERT_FLOAT_EQ(g.lifespan, 0.0f, 1e-6f, "default lifespan is 0 (will be set later)");
    ASSERT_FLOAT_EQ(g.self_mod, 0.0f, 1e-6f, "default self_mod is 0");
    ASSERT_FLOAT_EQ(g.cross_mod, 0.0f, 1e-6f, "default cross_mod is 0");
    ASSERT_INT_EQ(g.generation, 0u, "default generation is 0");
    ASSERT_FLOAT_EQ(g.adhesion, 0.0f, 1e-6f, "default adhesion is 0");
    ASSERT_FLOAT_EQ(g.division_rate, 0.0f, 1e-6f, "default division_rate is 0");
    ASSERT_FLOAT_EQ(g.defense, 0.0f, 1e-6f, "default defense is 0");
    return true;
}

TEST_CASE(Genome_blending_mix_correct_average) {
    GenomeData p1{}, p2{};
    p1.self_mod   = 1.0f; p1.cross_mod  = 0.5f; p1.lifespan  = 300.0f;
    p1.adhesion   = 0.8f; p1.division_rate = 1.0f; p1.defense = 0.5f;
    p2.self_mod   = 0.2f; p2.cross_mod  = 1.5f; p2.lifespan  = 500.0f;
    p2.adhesion   = 1.2f; p2.division_rate = 0.5f; p2.defense = 1.5f;

    float blend_r = 0.5f;
    GenomeData child{};
    child.self_mod    = std::clamp(test::mix(p1.self_mod,    p2.self_mod,    blend_r), 0.3f, 2.0f);
    child.cross_mod   = std::clamp(test::mix(p1.cross_mod,   p2.cross_mod,   blend_r), 0.3f, 2.0f);
    child.lifespan    = std::clamp(test::mix(p1.lifespan,    p2.lifespan,    blend_r), 20.0f, 800.0f);
    child.adhesion    = std::clamp(test::mix(p1.adhesion,    p2.adhesion,    blend_r), 0.0f, 2.0f);
    child.division_rate = std::clamp(test::mix(p1.division_rate, p2.division_rate, blend_r), 0.0f, 2.0f);
    child.defense     = std::clamp(test::mix(p1.defense,     p2.defense,     blend_r), 0.0f, 2.0f);

    ASSERT_FLOAT_EQ(child.self_mod,    0.6f, 1e-4f, "blended self_mod");
    ASSERT_FLOAT_EQ(child.cross_mod,   1.0f, 1e-4f, "blended cross_mod");
    ASSERT_FLOAT_EQ(child.lifespan,    400.0f, 1e-4f, "blended lifespan");
    ASSERT_FLOAT_EQ(child.adhesion,    1.0f, 1e-4f, "blended adhesion");
    ASSERT_FLOAT_EQ(child.division_rate, 0.75f, 1e-4f, "blended division_rate");
    ASSERT_FLOAT_EQ(child.defense,     1.0f, 1e-4f, "blended defense");
    return true;
}

TEST_CASE(Genome_blending_clamping_works) {
    GenomeData p1{}, p2{};
    p1.self_mod = -5.0f;
    p2.self_mod = 10.0f;
    float blend = test::mix(p1.self_mod, p2.self_mod, 0.5f);
    float clamped = std::clamp(blend, 0.3f, 2.0f);
    ASSERT_FLOAT_EQ(clamped, 2.0f, 1e-4f, "clamp upper bound");
    return true;
}

TEST_CASE(Genome_generation_inheritance) {
    GenomeData p1{}, p2{};
    p1.generation = 5u;
    p2.generation = 3u;
    uint32_t child_gen = std::max(p1.generation, p2.generation) + 1u;
    ASSERT_INT_EQ(child_gen, 6u, "child generation = max + 1");
    return true;
}

} // anonymous

test::TestResult run_genome_tests() {
    test::print_header("Genome");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Genome") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
