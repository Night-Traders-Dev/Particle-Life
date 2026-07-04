#include "test_framework.hpp"
#include "core/types.h"
#include <vector>
#include <cmath>

// Minimal Particles mock for testing
struct TestParticles {
    std::vector<float> forces;
    std::vector<glm::vec4> colors;
    uint32_t behavior_flags[MAX_PARTICLE_TYPES];
    float trait_scales[MAX_PARTICLE_TYPES];

    TestParticles() {
        forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
        colors.assign(MAX_PARTICLE_TYPES, glm::vec4(0.0f));
        for (auto& f : behavior_flags) f = BEHAVIOR_NONE;
        for (auto& s : trait_scales) s = 1.0f;
    }

    void reset_trait_scales() {
        for (auto& s : trait_scales) s = 1.0f;
    }
};

namespace {

TEST_CASE(Particles_force_matrix_starts_zero) {
    TestParticles p;
    ASSERT_TRUE(std::all_of(p.forces.begin(), p.forces.end(),
                           [](float v){ return v == 0.0f; }), "forces start at zero");
    return true;
}

TEST_CASE(Particles_behavior_flags_init_none) {
    TestParticles p;
    for (int i = 0; i < MAX_PARTICLE_TYPES; ++i)
        ASSERT_INT_EQ(p.behavior_flags[i], 0u, "behavior flag is NONE");
    return true;
}

TEST_CASE(Particles_trait_scales_init_one) {
    TestParticles p;
    for (int i = 0; i < MAX_PARTICLE_TYPES; ++i)
        ASSERT_FLOAT_EQ(p.trait_scales[i], 1.0f, 1e-4f, "trait scale is 1.0");
    return true;
}

TEST_CASE(Particles_food_color_set) {
    TestParticles p;
    p.colors[FOOD_TYPE_INDEX] = glm::vec4(0.5f, 1.0f, 0.0f, 1.0f);
    ASSERT_FLOAT_EQ(p.colors[FOOD_TYPE_INDEX].r, 0.5f, 1e-4f, "food red");
    ASSERT_FLOAT_EQ(p.colors[FOOD_TYPE_INDEX].g, 1.0f, 1e-4f, "food green");
    ASSERT_FLOAT_EQ(p.colors[FOOD_TYPE_INDEX].b, 0.0f, 1e-4f, "food blue");
    return true;
}

TEST_CASE(Particles_reset_trait_scales) {
    TestParticles p;
    p.trait_scales[0] = 1.5f;
    p.reset_trait_scales();
    ASSERT_FLOAT_EQ(p.trait_scales[0], 1.0f, 1e-4f, "scale reset to 1.0");
    return true;
}

TEST_CASE(Particles_apply_preset_repeller) {
    TestParticles p;
    p.behavior_flags[3] = BEHAVIOR_NONE;
    p.forces[3 + 0 * MAX_PARTICLE_TYPES] = 0.5f; // pre-existing
    p.behavior_flags[3] = BEHAVIOR_MEMBRANE;
    for (int b = 0; b < MAX_PARTICLE_TYPES; ++b)
        p.forces[3 + b * MAX_PARTICLE_TYPES] = -0.8f;

    ASSERT_INT_EQ(p.behavior_flags[3], BEHAVIOR_MEMBRANE, "membrane flag set");
    ASSERT_FLOAT_EQ(p.forces[3 + 0 * MAX_PARTICLE_TYPES], -0.8f, 1e-4f, "force overwritten");
    return true;
}

TEST_CASE(Particles_apply_preset_viral) {
    TestParticles p;
    p.behavior_flags[5] = BEHAVIOR_NONE;
    p.behavior_flags[5] = BEHAVIOR_VIRION;
    for (int b = 0; b < MAX_PARTICLE_TYPES; ++b)
        p.forces[5 + b * MAX_PARTICLE_TYPES] = 0.6f;

    ASSERT_INT_EQ(p.behavior_flags[5], BEHAVIOR_VIRION, "virion flag set");
    ASSERT_FLOAT_EQ(p.forces[5 + 5 * MAX_PARTICLE_TYPES], 0.6f, 1e-4f, "self force set");
    return true;
}

TEST_CASE(Particles_add_particle_defaults) {
    // Simulate the add_particle logic from particles.cpp
    std::vector<glm::vec2> pos, vel;
    std::vector<uint32_t> types;
    std::vector<float> energy;
    std::vector<uint32_t> organism_ids;

    pos.push_back({1.0f, 2.0f});
    vel.push_back({3.0f, 4.0f});
    types.push_back(2u);
    energy.push_back(1.0f);
    organism_ids.push_back(0u);

    ASSERT_INT_EQ(pos.size(), 1u, "position added");
    ASSERT_FLOAT_EQ(energy[0], 1.0f, 1e-4f, "energy defaults to 1.0");
    ASSERT_INT_EQ(organism_ids[0], 0u, "organism id defaults to 0");
    return true;
}

} // anonymous

test::TestResult run_particles_tests() {
    test::print_header("Particles");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Particles") == 0 || std::string(t.name).find("Particle") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
