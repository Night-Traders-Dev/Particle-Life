#include "test_framework.hpp"
#include "core/types.h"

namespace {

TEST_CASE(Config_default_particle_count) {
    SimConfig cfg;
    ASSERT_INT_EQ(cfg.particle_count, 22500u, "default particle count");
    return true;
}

TEST_CASE(Config_default_particle_types) {
    SimConfig cfg;
    ASSERT_INT_EQ(cfg.particle_types, 14u, "default particle types");
    return true;
}

TEST_CASE(Config_default_cross_repro_rate) {
    SimConfig cfg;
    ASSERT_FLOAT_EQ(cfg.cross_repro_rate, 0.15f, 1e-5f, "default cross_repro_rate");
    return true;
}

TEST_CASE(Config_default_radius) {
    SimConfig cfg;
    ASSERT_FLOAT_EQ(cfg.radius, 1.5f, 1e-4f, "default radius");
    return true;
}

TEST_CASE(Config_default_metabolism) {
    SimConfig cfg;
    ASSERT_FLOAT_EQ(cfg.metabolism, 0.2f, 1e-4f, "default metabolism");
    return true;
}

TEST_CASE(Config_default_spawn_probability) {
    SimConfig cfg;
    ASSERT_FLOAT_EQ(cfg.spawn_probability, 0.0005f, 1e-5f, "default spawn probability");
    return true;
}

TEST_CASE(Config_default_terrain_count) {
    SimConfig cfg;
    ASSERT_INT_EQ(cfg.terrain_obstacle_count, 0u, "default terrain count");
    return true;
}

TEST_CASE(Config_zip_code_example) {
    SimConfig cfg;
    std::strncpy(cfg.zip_code, "41101", sizeof(cfg.zip_code) - 1);
    cfg.zip_code[sizeof(cfg.zip_code) - 1] = '\0';
    ASSERT_TRUE(std::string(cfg.zip_code) == "41101", "zip code copied");
    return true;
}

TEST_CASE(Config_energy_depletion_rates_initialized) {
    SimConfig cfg;
    // Default init has all 1.0 values (only first 10 set in initializer)
    // Water (index 0) has default 1.0 until simulation sets it to 0.0
    ASSERT_FLOAT_EQ(cfg.energy_depletion_rates[TYPE_WATER], 1.0f, 1e-4f, "water depletion default");
    // Check that array has correct size
    for (int i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        ASSERT_TRUE(cfg.energy_depletion_rates[i] >= 0.0f && cfg.energy_depletion_rates[i] <= 2.0f, "depletion in valid range");
    }
    return true;
}

} // anonymous

test::TestResult run_config_tests() {
    test::print_header("Config");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Config") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
