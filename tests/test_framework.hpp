#pragma once

#include <iostream>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cstring>
#include <glm/glm.hpp>
#include "core/types.h"

namespace test {

struct TestResult {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    void add_pass() { passed++; }
    void add_fail(const std::string& msg) {
        failed++;
        failures.push_back(msg);
    }

    bool ok() const { return failed == 0; }

    std::string summary() const {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "passed=%d failed=%d score=%d%%",
                       passed, failed,
                       passed > 0 ? int(100.0 * passed / (passed + failed)) : 0);
        return std::string(buf);
    }
};

inline void print_header(const char* name) {
    std::cout << "\n=== " << name << " ===\n";
}

inline void print_result(const TestResult& r) {
    std::cout << "  " << r.summary() << "\n";
    for (const auto& f : r.failures)
        std::cout << "  [FAIL] " << f << "\n";
}

inline void test_fail(const char* file, int line, const std::string& msg) {
    std::cerr << "  [FAIL] " << file << ":" << line << " — " << msg << "\n";
}

#define ASSERT_TRUE(cond, msg_) \
    do { if (!(cond)) { test::test_fail(__FILE__, __LINE__, msg_); return false; } } while(0)

#define ASSERT_FLOAT_EQ(a, b, eps, msg_) \
    do { if (std::abs((a) - (b)) > (eps)) { test::test_fail(__FILE__, __LINE__, msg_); return false; } } while(0)

#define ASSERT_INT_EQ(a, b, msg_) \
    do { if ((a) != (b)) { test::test_fail(__FILE__, __LINE__, msg_); return false; } } while(0)

inline float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

struct TestCase {
    const char* name;
    bool (*func)();
};

#define TEST_CASE(name_) \
    static bool test_##name_(); \
    static const bool reg_##name_ = [](){ \
        test::test_registry().push_back({#name_, test_##name_}); \
        return true; \
    }(); \
    static bool test_##name_()

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

} // namespace test
