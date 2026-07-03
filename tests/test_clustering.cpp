#include "test_framework.hpp"
#include <vector>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <glm/glm.hpp>

namespace {

// Replicate the clustering logic from organism.cpp for testing

static inline int64_t cell_key(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
}

std::vector<int> build_clusters(const std::vector<glm::vec2>& positions, float cluster_radius) {
    uint32_t n = static_cast<uint32_t>(positions.size());
    std::vector<int> parent(n);
    std::vector<int> rank_arr(n, 0);
    std::iota(parent.begin(), parent.end(), 0);

    auto find = [&](int x) {
        int root = x;
        while (parent[root] != root) root = parent[root];
        while (parent[x] != root) {
            int next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    };

    auto unite = [&](int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank_arr[a] < rank_arr[b]) std::swap(a, b);
        parent[b] = a;
        if (rank_arr[a] == rank_arr[b]) rank_arr[a]++;
    };

    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);
    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / cluster_radius);
        int cy = static_cast<int>(positions[i].y / cluster_radius);
        grid[cell_key(cx, cy)].push_back(i);
    }

    float r2 = cluster_radius * cluster_radius;
    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / cluster_radius);
        int cy = static_cast<int>(positions[i].y / cluster_radius);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.find(cell_key(cx + dx, cy + dy));
                if (it == grid.end()) continue;
                for (uint32_t j : it->second) {
                    if (j <= i) continue;
                    glm::vec2 d = positions[j] - positions[i];
                    if (glm::dot(d, d) < r2)
                        unite(static_cast<int>(i), static_cast<int>(j));
                }
            }
        }
    }

    for (uint32_t i = 0; i < n; ++i)
        parent[i] = find(i);

    return parent;
}

TEST_CASE(Clustering_single_particle_is_own_cluster) {
    std::vector<glm::vec2> pos = {{0.0f, 0.0f}};
    auto clusters = build_clusters(pos, 40.0f);
    ASSERT_INT_EQ(clusters.size(), 1u, "one particle -> one cluster");
    ASSERT_INT_EQ(clusters[0], 0, "cluster root is 0");
    return true;
}

TEST_CASE(Clustering_neighbors_merge) {
    std::vector<glm::vec2> pos = {
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {100.0f, 0.0f}
    };
    auto clusters = build_clusters(pos, 50.0f);
    ASSERT_INT_EQ(clusters[0], clusters[1], "nearby particles merge");
    ASSERT_INT_EQ(clusters[2], 2, "distant particle stays separate");
    return true;
}

TEST_CASE(Clustering_far_particles_do_not_merge) {
    std::vector<glm::vec2> pos = {
        {0.0f, 0.0f},
        {200.0f, 0.0f}
    };
    auto clusters = build_clusters(pos, 40.0f);
    ASSERT_TRUE(clusters[0] != clusters[1], "far particles stay separate");
    return true;
}

TEST_CASE(Clustering_diagonal_proximity) {
    std::vector<glm::vec2> pos = {
        {0.0f, 0.0f},
        {30.0f, 30.0f} // dist = sqrt(1800) ≈ 42.4
    };
    auto clusters = build_clusters(pos, 40.0f);
    ASSERT_TRUE(clusters[0] != clusters[1], "diagonal beyond radius stays separate");

    clusters = build_clusters(pos, 45.0f);
    ASSERT_INT_EQ(clusters[0], clusters[1], "diagonal within radius merges");
    return true;
}

TEST_CASE(Clustering_transitive_merging) {
    std::vector<glm::vec2> pos = {
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {20.0f, 0.0f}
    };
    auto clusters = build_clusters(pos, 50.0f);
    ASSERT_INT_EQ(clusters[0], clusters[1], "0 and 1 merge");
    ASSERT_INT_EQ(clusters[1], clusters[2], "1 and 2 merge -> transitive");
    ASSERT_INT_EQ(clusters[0], clusters[2], "0 and 2 also merge");
    return true;
}

TEST_CASE(Clustering_large_grid_stress) {
    std::vector<glm::vec2> pos;
    // Pairs of particles merging (spacing 30 < radius 50),
    // but pairs are isolated from each other (gap 70 > radius 50)
    for (int i = 0; i < 100; ++i) {
        int pair = i / 2;
        int offset = i % 2;
        pos.push_back({float(pair * 100 + offset * 30), 0.0f});
    }
    auto clusters = build_clusters(pos, 50.0f);
    int roots = 0;
    for (int i = 0; i < 100; ++i)
        if (clusters[i] == i) roots++;
    ASSERT_TRUE(roots > 1, "large grid has multiple clusters");
    ASSERT_TRUE(roots < 200, "clusters actually merge some particles");
    return true;
}

} // anonymous

test::TestResult run_clustering_tests() {
    test::print_header("Clustering");
    test::TestResult r;
    for (const auto& t : test::test_registry()) {
        if (std::string(t.name).find("Clustering") == 0) {
            if (t.func()) r.add_pass(); else r.add_fail(t.name);
        }
    }
    test::print_result(r);
    return r;
}
