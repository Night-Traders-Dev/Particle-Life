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

    // Set food behavior
    behavior_flags[FOOD_TYPE_INDEX] = BEHAVIOR_FOOD;

    gen_empty_conversion_matrix();

    if (cfg.reset_forces)
        gen_random_force_matrix();

    if (cfg.reset_colors)
        gen_default_colors();
    
    // Food color: Bright lime green
    colors[FOOD_TYPE_INDEX] = glm::vec4(0.5f, 1.0f, 0.0f, 1.0f);

    // Make all types slightly attracted to food by default
    for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
        if (t == FOOD_TYPE_INDEX) {
            forces[t + t * MAX_PARTICLE_TYPES] = -0.1f; // Food repels itself slightly
        } else {
            forces[t + FOOD_TYPE_INDEX * MAX_PARTICLE_TYPES] = 0.5f; // Attraction towards food
        }
    }

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

    // Weighted trophic distribution: 50% plants, 30% herbivores, 20% predators
    uint32_t type_weights[MAX_PARTICLE_TYPES] = {};
    uint32_t total_weight = 0;
    for (uint32_t t = 0; t < cfg.particle_types && t < MAX_PARTICLE_TYPES; ++t) {
        if (t < 3)       type_weights[t] = 5;  // plants
        else if (t < 6)  type_weights[t] = 3;  // herbivores
        else             type_weights[t] = 2;  // predators
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

    // Random initial orientations for all particles (used by POLAR types)
    for (uint32_t i = 0; i < positions.size(); ++i) {
        angles[i] = rand_range_f(0.0f, 6.28318f);
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

void Particles::gen_random_force_matrix() {
    // Trophic-level force matrix for realistic ecosystem dynamics:
    //   Types 0-2: Producers (plants)     — self-attract, mildly avoid others
    //   Types 3-5: Herbivores             — seek plants, flee predators
    //   Types 6-8: Predators              — hunt herbivores, ignore plants
    //   Type   9:  Food particles
    //
    // force[a + b*PT] = effect of neighbor b on particle a (>0 = attract, <0 = repel)
    forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES);

    auto set_row = [&](uint32_t a, float self_val,
                       float to_plants, float to_herbivores,
                       float to_predators, float to_food) {
        for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
            uint32_t idx = a + b * MAX_PARTICLE_TYPES;
            if (b == a) { forces[idx] = self_val; continue; }
            if (b == FOOD_TYPE_INDEX) { forces[idx] = to_food; continue; }
            if (b < 3)       forces[idx] = to_plants;
            else if (b < 6)  forces[idx] = to_herbivores;
            else             forces[idx] = to_predators;
        }
    };

    // Producers (0-2): clump together, weakly avoid being eaten
    set_row(0,  0.6f,  0.4f, -0.2f, -0.3f,  0.0f);
    set_row(1,  0.6f,  0.4f, -0.2f, -0.3f,  0.0f);
    set_row(2,  0.6f,  0.4f, -0.2f, -0.3f,  0.0f);

    // Herbivores (3-5): seek plants (food), social grazing, flee predators
    set_row(3,  0.3f,  0.8f,  0.2f, -0.6f,  0.0f);
    set_row(4,  0.3f,  0.8f,  0.2f, -0.6f,  0.0f);
    set_row(5,  0.3f,  0.8f,  0.2f, -0.6f,  0.0f);

    // Predators (6-8): hunt herbivores, ignore plants, loose pack
    set_row(6,  0.2f,  0.0f,  0.9f,  0.1f,  0.0f);
    set_row(7,  0.2f,  0.0f,  0.9f,  0.1f,  0.0f);
    set_row(8,  0.2f,  0.0f,  0.9f,  0.1f,  0.0f);

    // Food repels itself slightly; all types attracted to food (set in gen_data)
    forces[FOOD_TYPE_INDEX + FOOD_TYPE_INDEX * MAX_PARTICLE_TYPES] = -0.1f;
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

void Particles::gen_default_colors() {
    colors = {
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),  //  1 cyan
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),  //  2 red
        glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),  //  3 green
        glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),  //  4 magenta
        glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),  //  5 yellow
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),  //  6 blue
        glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),  //  7 orange
        glm::vec4(0.5f, 0.0f, 1.0f, 1.0f),  //  8 violet
        glm::vec4(0.0f, 1.0f, 0.5f, 1.0f),  //  9 spring green
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),  // 10 white
    };
    colors.resize(MAX_PARTICLE_TYPES, glm::vec4(1.0f));
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
// Each preset clears then sets behavior_flags for `type` and seeds the
// corresponding row of the force matrix.  The UI can still hand-edit forces.

static void set_row(std::vector<float>& forces, uint32_t type, float self_val, float cross_val) {
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t fi = type + b * MAX_PARTICLE_TYPES;
        forces[fi]  = (b == type) ? self_val : cross_val;
    }
}

void Particles::apply_preset_default(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;
    // Leave force matrix as-is (user-controlled)
}

void Particles::apply_preset_repeller(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_REPEL;
    set_row(forces, type, -0.8f, -0.8f);
}

void Particles::apply_preset_polar(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_POLAR;
    // Strong self-attraction to form chains; random-ish cross forces
    set_row(forces, type, 0.4f, 0.0f);
    // Give slight positive force toward all active types to mix into the soup
    for (uint32_t b = 0; b < active_types; ++b) {
        if (b == type) continue;
        forces[type + b * MAX_PARTICLE_TYPES] = 0.15f;
    }
}

void Particles::apply_preset_heavy(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_HEAVY;
    set_row(forces, type, -0.2f, -0.2f);
}

void Particles::apply_preset_catalyst(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CATALYST;
    set_row(forces, type, 0.1f, 0.2f);
}

void Particles::apply_preset_membrane(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;  // pure force-matrix behaviour
    set_row(forces, type, 0.7f, -0.4f);
}

void Particles::apply_preset_viral(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_VIRAL;
    // Strong attraction toward all types to get close enough to infect
    set_row(forces, type, 0.6f, 0.6f);
    (void)active_types;  // unused, kept for API symmetry
}

void Particles::apply_preset_leech(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_LEECH;
    // Moderate attraction to drain energy
    set_row(forces, type, 0.2f, 0.4f);
}

void Particles::apply_preset_shield(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_SHIELD;
    // Defensive repeller
    set_row(forces, type, 0.1f, -0.3f);
}

void Particles::apply_preset_proton(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_HEAVY | BEHAVIOR_POSITIVE;
    set_row(forces, type, 0.1f, 0.0f);
}

void Particles::apply_preset_electron(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NEGATIVE;
    set_row(forces, type, 0.1f, 0.0f);
}

void Particles::apply_preset_pos_monopole(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_POSITIVE;
    set_row(forces, type, 0.0f, 0.0f);
}

void Particles::apply_preset_neg_monopole(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NEGATIVE;
    set_row(forces, type, 0.0f, 0.0f);
}
