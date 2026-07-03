#pragma once

#include "types.h"
#include "particles.h"
#include <cstdint>
#include <vector>
#include <array>
#include <glm/glm.hpp>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t ORGANISM_UPDATE_INTERVAL = 5;  // frames between updates
static constexpr uint32_t ORGANISM_MIN_SIZE         = 8;  // min particles to form an organism

// ── Speciation record ─────────────────────────────────────────────────────────

struct SpeciesRecord {
    float avg_self_mod   = 0.8f;
    float avg_cross_mod  = 0.8f;
    float avg_lifespan   = 300.0f;
    float divergence     = 0.0f;
    bool  speciation_logged = false;
};

// ── Parasite co-evolution record ──────────────────────────────────────────────

struct ParasiteRecord {
    uint32_t type               = 0;
    float    infectivity        = 0.0f;
    float    resistance         = 0.0f;
    float    coevolution_score  = 0.0f;
};

// ── Traits ───────────────────────────────────────────────────────────────────

struct OrganismTraits {
    uint32_t size           = 0;
    float    avg_speed      = 0.0f;
    uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
    uint32_t dominant_type  = 0;

    // Lineage
    uint32_t generation     = 0;   // how many division/absorption events in ancestry
    uint64_t parent_id      = 0;   // 0 = primordial
    uint32_t kills          = 0;   // organisms this lineage has consumed
    uint32_t divisions      = 0;   // times this lineage has divided

    // Multi-cell
    bool     is_multicell   = false;
};

// ── Organism ──────────────────────────────────────────────────────────────────

struct Organism {
    uint64_t       id       = 0;
    OrganismTraits traits   = {};
    glm::vec2      centroid = {};
    float          spread   = 0.0f;  // RMS distance of particles from centroid
    float          membrane_radius = 0.0f; // New: collision boundary
    float          min_temp = -10.0f; 
    float          max_temp = 50.0f;  
    float          structural_integrity = 1.0f;
    std::vector<uint32_t> particle_indices;
};
// ── OrganismManager ───────────────────────────────────────────────────────────

class OrganismManager {
public:
    std::vector<Organism> organisms;    // current detected organisms
    float cluster_radius = 40.0f;       // proximity threshold for same-organism
    uint32_t avg_generation = 0;        // average generation across all organisms

    // Speciation tracking
    std::array<SpeciesRecord, MAX_PARTICLE_TYPES> species_records;

    // Multi-cell organism tracking
    uint32_t multicell_organism_count = 0;
    float multicell_bond_energy = 0.001f;
    float multicell_spring_k = 0.05f;

    // Parasite co-evolution tracking
    std::vector<ParasiteRecord> parasite_records;

    // Clear all state (call on simulation reset)
    void reset();

    // Detect organisms from current particle state and update traits.
    // Modifies particles.trait_scales to feed traits back into physics.
    void update(const std::vector<glm::vec2>& positions,
                const std::vector<glm::vec2>& velocities,
                const std::vector<uint32_t>&  types,
                Particles& particles,
                SimConfig* cfg = nullptr);

    // Speciation: split a type when genome values diverge >40% from baseline
    void check_speciation(Particles& particles, SimConfig& cfg);

    // Parasite/predator-prey co-evolution tracking
    void update_parasite_records(const std::vector<uint32_t>& types, uint32_t particle_types);

private:
    std::vector<Organism> prev_organisms_;
    uint64_t              next_id_ = 1;
    uint32_t              prev_type_populations_[MAX_PARTICLE_TYPES] = {};

    // Cluster particles using spatial hash + union-find.
    // Returns parent[i] = root cluster index for particle i.
    std::vector<int> build_clusters(const std::vector<glm::vec2>& positions);

    // Write trait_scales into particles based on current organisms.
    void apply_trait_feedback(Particles& particles);
};
