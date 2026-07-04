#include "particles.h"
#include <random>
#include <cstring>
#include <algorithm>
#include <cmath>

Particles::Particles() {
    // Pre-allocate force / color arrays at max size
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
    gen_default_colors();
}

void Particles::gen_data(const SimConfig& cfg) {
    rng_.seed(cfg.generation_seed);
    for (float& s : trait_scales) s = 1.0f;
    for (auto& f : behavior_flags) f = BEHAVIOR_NONE;

    gen_empty_conversion_matrix();

    if (cfg.reset_forces)
        gen_biochemical_force_matrix();

    if (cfg.reset_colors)
        gen_biochemical_colors();

    gen_particles(cfg);
}

void Particles::gen_particles(const SimConfig& cfg) {
    positions.clear();
    velocities.clear();
    types.clear();
    energy.clear();
    angles.clear();
    angular_velocities.clear();
    stats.clear();
    organism_ids.clear();

    // Spawn across a region 3× the viewport so particles have room to form
    // natural structures. The camera starts at (0,0) so the centre is always
    // visible immediately; the world just extends beyond the initial view.
    const float rw = static_cast<float>(REGION_W) * 3.0f;
    const float rh = static_cast<float>(REGION_H) * 3.0f;

    // Half a grid-cell used for jitter — breaks alignment with the 60-unit
    // spatial-hash grid that causes the lattice/grid artefact at startup.
    const float jitter = static_cast<float>(GRID_CELL_SIZE) * 0.5f;

    uint32_t count = cfg.particle_count;

    // Biochemical distribution for cellular soup simulation:
    // Mix of molecules that can self-organize into cells
    uint32_t type_weights[MAX_PARTICLE_TYPES] = {};
    uint32_t total_weight = 0;
    
    // Water dominates the environment (60%)
    type_weights[TYPE_WATER] = 60;
    
    // Ions and simple molecules (20% combined)
    type_weights[TYPE_IONS] = 15;
    type_weights[TYPE_SIMPLE] = 5;
    
    // Building blocks for cells (15% combined)
    type_weights[TYPE_LIPIDS] = 5;
    type_weights[TYPE_PROTEINS] = 5;
    type_weights[TYPE_NUCLEIC] = 3;
    type_weights[TYPE_CELL_MEM] = 2;
    
    // Organelles, nutrients, reactive species (4% combined)
    type_weights[TYPE_ORGANELLE] = 2;
    type_weights[TYPE_NUTRIENT] = 2;
    type_weights[TYPE_PROTON] = 1;
    type_weights[TYPE_ELECTRON] = 1;
    
    // Rare infectious/viral particles (<1%)
    type_weights[TYPE_VIRUS] = 1;

    for (uint32_t t = 0; t < cfg.particle_types && t < MAX_PARTICLE_TYPES; ++t) {
        total_weight += type_weights[t];
    }

    for (uint32_t i = 0; i < count; ++i) {
        glm::vec2 pos(rand_range_f(-rw / 2.0f, rw / 2.0f),
                      rand_range_f(-rh / 2.0f, rh / 2.0f));
        // Sub-cell jitter: nudge each particle by a random offset smaller than
        // one grid cell so no two particles share the same cell boundary.
        pos.x += rand_range_f(-jitter, jitter);
        pos.y += rand_range_f(-jitter, jitter);

        // Weighted random type selection
        uint32_t roll = static_cast<uint32_t>(rand_range_i(1, (int)total_weight));
        uint32_t t = 0;
        while (t < cfg.particle_types && roll > type_weights[t]) {
            roll -= type_weights[t];
            t++;
        }
        add_particle(pos, glm::vec2(0.0f), t);
    }
}

void Particles::add_particle(glm::vec2 pos, glm::vec2 vel, uint32_t type) {
    positions.push_back(pos);
    velocities.push_back(vel);
    types.push_back(type);
    energy.push_back(1.0f);
    angles.push_back(0.0f);
    angular_velocities.push_back(0.0f);
    stats.push_back(ParticleStats{});
    organism_ids.push_back(0u);
}

void Particles::gen_biochemical_force_matrix() {
    // Biochemical interaction matrix based on molecular compatibility
    // force[a + b*PT] = effect of neighbor b on particle a (>0 = attract, <0 = repel)
    forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES);

    // Biochemical affinities:
    // - Water attracts most soluble molecules (hydration shell effect)
    // - Ions attract oppositely charged, repel similarly charged
    // - Lipids repel water (hydrophobic effect), attract other lipids/membranes
    // - Proteins bind selectively to targets (receptors/enzymes)
    // - Nutrients are attracted to all metabolic entities
    // - Cells attract internal components, repel foreign cells
    // - Dead cells attract decomposers, repel living cells

    // Water (highly polar, universal solvent)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_WATER + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_WATER) forces[idx] = 0.3f;       // Water-water mild attraction (cohesion)
        else if (b == TYPE_IONS) forces[idx] = 0.8f;   // Hydration
        else if (b == TYPE_SIMPLE) forces[idx] = 0.6f;    // Dissolves gases/nutrients
        else if (b == TYPE_LIPIDS) forces[idx] = -0.5f;  // Hydrophobic exclusion
        else forces[idx] = 0.2f;  // Mild attraction to others
    }

    // Ions (charged, hydrated in solution)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_IONS + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_WATER) forces[idx] = 0.8f;       // Hydration
        else if (b == TYPE_IONS) forces[idx] = -0.3f;   // Like charges repel
        else if (b == TYPE_NUTRIENT) forces[idx] = 0.4f; // Some nutrients ionize
        else forces[idx] = 0.1f;
    }

    // Simple molecules (CO2, O2, glucose - small soluble)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_SIMPLE + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_WATER) forces[idx] = 0.6f;
        else if (b == TYPE_SIMPLE) forces[idx] = 0.2f;
        else if (b == TYPE_NUTRIENT) forces[idx] = 0.7f;  // Similar molecules attract
        else if (b == TYPE_CELL) forces[idx] = 0.9f;     // Cells consume nutrients
        else forces[idx] = 0.1f;
    }

    // Lipids (hydrophobic, form membranes)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_LIPIDS + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_LIPIDS) forces[idx] = 0.7f;       // Lipid-lipid attraction (membrane formation)
        else if (b == TYPE_WATER) forces[idx] = -0.6f;   // Hydrophobic exclusion
        else if (b == TYPE_CELL_MEM) forces[idx] = 0.8f; // Membrane integration
        else forces[idx] = -0.2f;  // Repel most polar molecules
    }

    // Proteins (enzymes, receptors, structural)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_PROTEINS + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_PROTEINS) forces[idx] = 0.5f;      // Protein-protein binding
        else if (b == TYPE_NUCLEIC) forces[idx] = 0.6f;  // Protein-DNA/RNA interaction
        else if (b == TYPE_ORGANELLE) forces[idx] = 0.8f; // Organelle proteins
        else if (b == TYPE_CELL) forces[idx] = 0.7f;     // Cell protein integration
        else if (b == TYPE_LIPIDS) forces[idx] = 0.4f;   // Membrane proteins
        else forces[idx] = 0.2f;
    }

    // Nucleic acids (DNA/RNA - templating, information)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_NUCLEIC + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_NUCLEIC) forces[idx] = 0.4f;       // Strand pairing
        else if (b == TYPE_PROTEINS) forces[idx] = 0.6f; // Transcription/translation
        else if (b == TYPE_CELL) forces[idx] = 0.8f;     // Genetic integration
        else forces[idx] = 0.1f;
    }

    // Cell membrane components
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_CELL_MEM + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_CELL_MEM) forces[idx] = 0.8f;      // Membrane self-assembly
        else if (b == TYPE_LIPIDS) forces[idx] = 0.9f;   // Lipid bilayer formation
        else if (b == TYPE_CELL) forces[idx] = 0.5f;     // Membrane attachment
        else forces[idx] = -0.3f;  // Repel most internal components
    }

    // Organelles (active cellular machinery)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_ORGANELLE + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_ORGANELLE) forces[idx] = 0.3f;      // Organelle clustering
        else if (b == TYPE_CELL) forces[idx] = 0.9f;     // Cell-organelle binding
        else if (b == TYPE_NUTRIENT) forces[idx] = 0.6f; // Nutrient processing
        else forces[idx] = 0.2f;
    }

    // Electrons (extremely reactive radicals)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_ELECTRON + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_ELECTRON) forces[idx] = -0.9f;      // Electron-electron repulsion
        else forces[idx] = -0.3f;                         // Toxic to most
    }

    // Nutrients (food - attracted to metabolic entities)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_NUTRIENT + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_NUTRIENT) forces[idx] = -0.1f;     // Nutrient dispersal
        else if (b == TYPE_CELL) forces[idx] = 0.8f;     // Consumed by cells
        else if (b == TYPE_ORGANELLE) forces[idx] = 0.6f;  // Used by organelles
        else forces[idx] = 0.2f;
    }

    // Protons (H+ - acidic, reactive)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_PROTON + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_PROTON) forces[idx] = -0.5f;        // Proton-proton repulsion
        else if (b == TYPE_ELECTRON) forces[idx] = 0.9f; // H+ combines with electrons
        else forces[idx] = -0.1f;                        // Mildly toxic
    }

    // Living cells (autonomous organisms)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_CELL + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_CELL) forces[idx] = 0.4f;          // Cell-cell adhesion (limited)
        else if (b == TYPE_NUTRIENT) forces[idx] = 0.9f; // Seeks nutrients
        else if (b == TYPE_WATER) forces[idx] = 0.3f;    // Hydration
        else if (b == TYPE_ORGANELLE) forces[idx] = 0.7f; // Internal components
        else if (b == TYPE_VIRUS) forces[idx] = -0.5f;   // Avoid infection
        else if (b == TYPE_DEAD_CELL) forces[idx] = -0.3f; // Avoid decay
        else forces[idx] = 0.1f;
    }

    // Dead cells (decomposing waste)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_DEAD_CELL + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_DEAD_CELL) forces[idx] = 0.3f;     // Corpse clustering
        else if (b == TYPE_CELL) forces[idx] = -0.6f;    // Repel living cells
        else forces[idx] = 0.1f;
    }

    // Viruses (infectious particles)
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t idx = TYPE_VIRUS + b * MAX_PARTICLE_TYPES;
        if (b == TYPE_VIRUS) forces[idx] = 0.2f;         // Virion aggregation
        else if (b == TYPE_CELL) forces[idx] = 0.8f;     // Seeks host cells
        else forces[idx] = -0.5f;                         // Repel most
    }
}

void Particles::gen_random_force_matrix() {
    // Legacy function - calls biochemical version for compatibility
    gen_biochemical_force_matrix();
}



void Particles::gen_empty_force_matrix() {
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
}

void Particles::gen_empty_conversion_matrix() {
    for (auto& c : conversion_matrix) {
        c.target_type = -1;
        c.probability = 0.0f;
    }
}

void Particles::gen_biochemical_colors() {
    // Biochemically-inspired color palette:
    // 0: Water - clear blue
    // 1: Ions - metallic silver
    // 2: Simple molecules - pale yellow (like glucose)
    // 3: Lipids - golden yellow
    // 4: Proteins - light blue (like albumin)
    // 5: Nucleic acids - magenta (DNA)
    // 6: Cell membrane - orange-brown (phospholipid heads)
    // 7: Organelles - bright green
    // 8: Electrons - electric purple
    // 9: Nutrients - bright green (glucose)
    // 10: Protons - red (acidic)
    // 11: Living cells - translucent cyan
    // 12: Dead cells - dark brown/black
    // 13: Viruses - purple-red
    
    colors = {
        glm::vec4(0.6f, 0.8f, 1.0f, 0.7f),  // 0 Water - clear, slightly blue
        glm::vec4(0.8f, 0.8f, 0.9f, 1.0f),  // 1 Ions - metallic silver
        glm::vec4(1.0f, 0.9f, 0.5f, 1.0f),  // 2 Simple molecules - pale yellow
        glm::vec4(1.0f, 0.9f, 0.2f, 1.0f),  // 3 Lipids - golden
        glm::vec4(0.6f, 0.8f, 1.0f, 1.0f),  // 4 Proteins - light blue
        glm::vec4(0.9f, 0.2f, 0.8f, 1.0f),  // 5 Nucleic acids - magenta (DNA)
        glm::vec4(0.9f, 0.5f, 0.2f, 1.0f),  // 6 Cell membrane - orange-brown
        glm::vec4(0.2f, 0.9f, 0.4f, 1.0f),  // 7 Organelles - bright green
        glm::vec4(0.8f, 0.2f, 1.0f, 1.0f),  // 8 Electrons - electric purple
        glm::vec4(0.3f, 1.0f, 0.3f, 1.0f),  // 9 Nutrients - bright green
        glm::vec4(1.0f, 0.2f, 0.2f, 1.0f),  // 10 Protons - red (acidic)
        glm::vec4(0.2f, 1.0f, 0.9f, 0.8f),  // 11 Living cells - cyan, semi-transparent
        glm::vec4(0.3f, 0.2f, 0.1f, 1.0f),  // 12 Dead cells - dark brown
        glm::vec4(0.8f, 0.2f, 0.6f, 1.0f),  // 13 Viruses - purple-red
    };
    colors.resize(MAX_PARTICLE_TYPES, glm::vec4(1.0f));
}

void Particles::gen_default_colors() {
    gen_biochemical_colors();
}

void Particles::set_palette(int index) {
    // Index 0 = default (existing colors)
    if (index == 0) {
        gen_default_colors();
        return;
    }

    // Matplotlib colormaps sampled at 10 evenly-spaced points
    // Each entry: [r, g, b] in [0,1]
    static const float viridis[10][3] = {
        {0.267f, 0.004f, 0.329f}, {0.282f, 0.140f, 0.458f},
        {0.254f, 0.265f, 0.530f}, {0.207f, 0.382f, 0.549f},
        {0.164f, 0.494f, 0.521f}, {0.160f, 0.600f, 0.451f},
        {0.282f, 0.700f, 0.338f}, {0.497f, 0.786f, 0.199f},
        {0.741f, 0.850f, 0.150f}, {0.993f, 0.906f, 0.144f},
    };
    static const float plasma[10][3] = {
        {0.050f, 0.030f, 0.530f}, {0.280f, 0.016f, 0.615f},
        {0.488f, 0.006f, 0.618f}, {0.667f, 0.086f, 0.543f},
        {0.813f, 0.194f, 0.418f}, {0.921f, 0.320f, 0.268f},
        {0.991f, 0.471f, 0.121f}, {0.998f, 0.637f, 0.027f},
        {0.947f, 0.808f, 0.062f}, {0.941f, 0.975f, 0.131f},
    };
    static const float magma[10][3] = {
        {0.001f, 0.000f, 0.014f}, {0.167f, 0.034f, 0.206f},
        {0.384f, 0.035f, 0.323f}, {0.590f, 0.064f, 0.308f},
        {0.768f, 0.159f, 0.203f}, {0.907f, 0.295f, 0.061f},
        {0.981f, 0.462f, 0.000f}, {0.988f, 0.637f, 0.057f},
        {0.979f, 0.808f, 0.201f}, {0.987f, 0.976f, 0.441f},
    };
    static const float inferno[10][3] = {
        {0.001f, 0.000f, 0.014f}, {0.089f, 0.058f, 0.271f},
        {0.247f, 0.100f, 0.482f}, {0.470f, 0.082f, 0.571f},
        {0.695f, 0.125f, 0.524f}, {0.878f, 0.238f, 0.368f},
        {0.994f, 0.391f, 0.142f}, {0.995f, 0.568f, 0.018f},
        {0.947f, 0.750f, 0.005f}, {0.988f, 0.940f, 0.310f},
    };

    const float (*palette)[3] = nullptr;
    switch (index) {
        case 1: palette = viridis; break;
        case 2: palette = plasma;  break;
        case 3: palette = magma;   break;
        case 4: palette = inferno; break;
        default: gen_default_colors(); return;
    }

    colors.resize(MAX_PARTICLE_TYPES);
    for (uint32_t i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        colors[i] = glm::vec4(palette[i][0], palette[i][1], palette[i][2], 1.0f);
    }
    // Food type remains bright green for visibility
    colors[FOOD_TYPE_INDEX] = glm::vec4(0.5f, 1.0f, 0.0f, 1.0f);
}

int Particles::rand_range_i(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng_);
}

float Particles::rand_range_f(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

// ── Archetype presets ─────────────────────────────────────────────────────────
// Each preset sets behavior_flags AND seeds the force-matrix row for `type`.
// These represent biochemical behaviors for cellular/molecular realism.

static void set_row_all(std::vector<float>& forces, uint32_t type, float self_val, float cross_val) {
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t fi = type + b * MAX_PARTICLE_TYPES;
        forces[fi] = (b == type) ? self_val : cross_val;
    }
}

void Particles::apply_preset_default(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;
}

void Particles::apply_preset_repeller(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CHARGE;
    set_row_all(forces, type, -0.5f, -0.3f); // Charged particles repel like charges
}

void Particles::apply_preset_polar(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_SOLUBLE;
    set_row_all(forces, type, 0.4f, 0.1f); // Attract water, mild other attraction
}

void Particles::apply_preset_heavy(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_STRUCTURAL;
    set_row_all(forces, type, 0.8f, 0.3f);
}

void Particles::apply_preset_catalyst(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_ENZYME;
    set_row_all(forces, type, 0.3f, 0.5f);
}

void Particles::apply_preset_membrane(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_MEMBRANE | BEHAVIOR_STICKY;
    set_row_all(forces, type, 0.7f, -0.3f);
}

void Particles::apply_preset_viral(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_VIRION;
    set_row_all(forces, type, 0.2f, 0.8f);
    (void)active_types;
}

void Particles::apply_preset_leech(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_TOXIC;
    set_row_all(forces, type, 0.2f, 0.4f);
}

void Particles::apply_preset_shield(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_RECEPTOR;
    set_row_all(forces, type, 0.4f, 0.0f);
}

// Proton: charged + toxic
void Particles::apply_preset_proton(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CHARGE | BEHAVIOR_TOXIC;
    set_row_all(forces, type, -0.3f, -0.2f);
}

// Electron: highly reactive radical
void Particles::apply_preset_electron(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_TOXIC;
    set_row_all(forces, type, -0.7f, -0.3f);
}

// Positive monopole: attracted to negative charges
void Particles::apply_preset_pos_monopole(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CHARGE;
    set_row_all(forces, type, -0.2f, 0.5f);
}

// Negative monopole
void Particles::apply_preset_neg_monopole(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CHARGE;
    set_row_all(forces, type, -0.2f, 0.5f);
}
