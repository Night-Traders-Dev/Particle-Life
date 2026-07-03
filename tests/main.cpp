#include "test_framework.hpp"
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

extern test::TestResult run_genome_tests();
extern test::TestResult run_clustering_tests();
extern test::TestResult run_speciation_tests();
extern test::TestResult run_organism_id_tests();
extern test::TestResult run_energy_tests();
extern test::TestResult run_rng_tests();
extern test::TestResult run_terrain_tests();
extern test::TestResult run_behavior_flag_tests();
extern test::TestResult run_config_tests();
extern test::TestResult run_force_matrix_tests();
extern test::TestResult run_particles_tests();

int main() {
    std::cout << "========================================\n";
    std::cout << "  Particle Life — Test Suite\n";
    std::cout << "========================================\n";

    auto results = {
        run_genome_tests(),
        run_clustering_tests(),
        run_speciation_tests(),
        run_organism_id_tests(),
        run_energy_tests(),
        run_rng_tests(),
        run_terrain_tests(),
        run_behavior_flag_tests(),
        run_config_tests(),
        run_force_matrix_tests(),
        run_particles_tests()
    };

    int total_passed = 0, total_failed = 0;
    for (const auto& r : results) {
        total_passed += r.passed;
        total_failed += r.failed;
    }

    std::cout << "\n========================================\n";
    std::cout << "  OVERALL: " << total_passed << " passed, " << total_failed << " failed\n";
    std::cout << "  SCORE:   " << (total_passed > 0 ? int(100.0 * total_passed / (total_passed + total_failed)) : 0) << "%\n";
    std::cout << "========================================\n";

    auto now = std::time(nullptr);
    std::cout << "  TIMESTAMP: " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "\n";
    std::cout << "========================================\n";

    return total_failed > 0 ? 1 : 0;
}
