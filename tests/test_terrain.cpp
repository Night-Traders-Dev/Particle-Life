#include "test_framework.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

// Replicate terrain/Perlin logic for testing
static float noise2d(int x, int y, int seed) {
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    return (float)(1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
}

static float smooth_noise(float x, float y, int seed) {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float v00 = noise2d(ix, iy, seed);
    float v10 = noise2d(ix + 1, iy, seed);
    float v01 = noise2d(ix, iy + 1, seed);
    float v11 = noise2d(ix + 1, iy + 1, seed);
    float v0 = v00 + (v10 - v00) * fx;
    float v1 = v01 + (v11 - v01) * fx;
    return v0 + (v1 - v0) * fy;
}

static float fbm(float x, float y, int octaves, int seed) {
    float value = 0.0f, amp = 1.0f, freq = 1.0f, max_val = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        value += amp * smooth_noise(x * freq, y * freq, seed + i * 100);
        max_val += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return value / max_val;
}

static std::vector<float> generate_terrain_grid(int w, int h) {
    std::vector<float> grid(w * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float nx = (float)x / (float)w * 4.0f;
            float ny = (float)y / (float)h * 4.0f;
            float noise = fbm(nx, ny, 4, 42);
            if (noise > 0.65f) {
                grid[x + y * w] = 1.0f;
            } else if (noise > 0.55f) {
                grid[x + y * w] = 0.5f + (noise - 0.55f) * 5.0f;
            }
        }
    }
    return grid;
}

namespace {

TEST_CASE(Terrain_fbm_is_deterministic) {
    float a = fbm(0.5f, 0.5f, 4, 42);
    float b = fbm(0.5f, 0.5f, 4, 42);
    ASSERT_FLOAT_EQ(a, b, 1e-6f, "fbm is deterministic");
    return true;
}

TEST_CASE(Terrain_fbm_in_range) {
    for (int i = 0; i < 20; ++i) {
        float v = fbm(float(i) * 0.1f, float(i) * 0.2f, 4, 42);
        ASSERT_TRUE(v >= -1.0f && v <= 1.0f, "fbm in [-1,1]");
    }
    return true;
}

TEST_CASE(Terrain_grid_has_correct_dimensions) {
    auto grid = generate_terrain_grid(320, 180);
    ASSERT_INT_EQ(grid.size(), 320 * 180, "grid size matches dimensions");
    return true;
}

TEST_CASE(Terrain_grid_contains_obstacles) {
    auto grid = generate_terrain_grid(320, 180);
    uint32_t obstacle_count = 0;
    for (float v : grid)
        if (v > 0.1f) obstacle_count++;
    ASSERT_TRUE(obstacle_count > 0, "some cells are obstacles");
    ASSERT_TRUE(obstacle_count < grid.size(), "not all cells are obstacles");
    return true;
}

TEST_CASE(Terrain_obstacle_count_threshold) {
    auto grid = generate_terrain_grid(320, 180);
    uint32_t count = 0;
    for (auto v : grid)
        if (v > 0.1f) count++;
    ASSERT_TRUE(count > 100, "meaningful obstacle count");
    return true;
}

} // anonymous

test::TestResult run_terrain_tests() {
    test::print_header("Terrain");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Terrain") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
