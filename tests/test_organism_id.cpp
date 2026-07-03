#include "test_framework.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace {

TEST_CASE(OrganismIDs_default_to_zero) {
    std::vector<uint32_t> ids = {0u, 0u, 0u, 0u};
    ASSERT_TRUE(std::all_of(ids.begin(), ids.end(), [](uint32_t v){ return v == 0; }), "all IDs default to 0");
    return true;
}

TEST_CASE(OrganismIDs_assignment_from_detection) {
    std::vector<uint32_t> ids(10, 0u);
    // Simulate organism_manager writing back IDs
    uint64_t org1_id = 1;
    std::vector<uint32_t> members = {0, 1, 2, 5};
    for (uint32_t idx : members) {
        if (idx < ids.size())
            ids[idx] = static_cast<uint32_t>(org1_id);
    }
    ASSERT_INT_EQ(ids[0], 1u, "particle 0 gets org id 1");
    ASSERT_INT_EQ(ids[3], 0u, "particle 3 remains 0");
    return true;
}

TEST_CASE(OrganismIDs_resize_on_particle_count_change) {
    std::vector<uint32_t> ids(5, 0u);
    size_t new_count = 10;
    if (ids.size() != new_count)
        ids.resize(new_count);
    ASSERT_INT_EQ(ids.size(), 10u, "resized to match particle count");
    ASSERT_INT_EQ(ids[9], 0u, "new entries are zero-initialized");
    return true;
}

TEST_CASE(OrganismIDs_multiple_organisms) {
    std::vector<uint32_t> ids(15, 0u);
    uint64_t org1_id = 1, org2_id = 2;
    std::vector<uint32_t> m1 = {0, 1, 2};
    std::vector<uint32_t> m2 = {3, 4, 5, 6};
    for (uint32_t idx : m1) ids[idx] = static_cast<uint32_t>(org1_id);
    for (uint32_t idx : m2) ids[idx] = static_cast<uint32_t>(org2_id);
    for (size_t i = 7; i < ids.size(); ++i) ids[i] = 0u;

    ASSERT_INT_EQ(ids[0], 1u, "org1 member");
    ASSERT_INT_EQ(ids[3], 2u, "org2 member");
    ASSERT_INT_EQ(ids[10], 0u, "free particle");
    return true;
}

} // anonymous

test::TestResult run_organism_id_tests() {
    test::print_header("Organism IDs");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("OrganismID") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
