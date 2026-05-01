#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr float    INFINITE_REGION_SIZE = 1000000.0f; // NEW: Simulation "infinite" bounds
static constexpr uint32_t MAX_PARTICLE_TYPES = 10;
static constexpr uint32_t GROUP_DENSITY      = 256;

// Spatial Hash constants
static constexpr uint32_t GRID_CELL_SIZE     = 60; // Matches default interaction_radius
static constexpr uint32_t GRID_W             = (REGION_W / GRID_CELL_SIZE) + 2;
static constexpr uint32_t GRID_H             = (REGION_H / GRID_CELL_SIZE) + 2;
static constexpr uint32_t GRID_SIZE          = GRID_W * GRID_H;

// Chemical Grid constants
static constexpr uint32_t CHEM_W             = 320; // Lower resolution than region for performance
static constexpr uint32_t CHEM_H             = 180;

// Post-processing constants
static constexpr uint32_t BLOOM_W            = REGION_W / 2; // half-resolution bloom
static constexpr uint32_t BLOOM_H            = REGION_H / 2;
static constexpr uint32_t MAX_HALOS          = 256;

// ── Particle behavior flags (bitmask per type, must match shader constants) ──

enum ParticleBehavior : uint32_t {
    BEHAVIOR_NONE     = 0,
    BEHAVIOR_REPEL    = 1u << 0,  // repels all types (shader)
    BEHAVIOR_POLAR    = 1u << 1,  // magnetic dipole orientation (shader + render)
    BEHAVIOR_HEAVY    = 1u << 2,  // high effective mass (shader)
    BEHAVIOR_CATALYST = 1u << 3,  // boosts speed of nearby particles (shader)
    BEHAVIOR_VIRAL    = 1u << 4,  // converts adjacent non-viral particles (shader)
    BEHAVIOR_LEECH    = 1u << 5,  // drains energy from neighbors
    BEHAVIOR_SHIELD   = 1u << 6,  // resistant to viral and leech
    BEHAVIOR_POSITIVE = 1u << 7,  // positive charge
    BEHAVIOR_NEGATIVE = 1u << 8,   // negative charge
    BEHAVIOR_FOOD     = 1u << 9    // NEW: Food particle (is eaten by others)
};

static constexpr uint32_t FOOD_TYPE_INDEX = 9;
static constexpr uint32_t NUM_FOOD_SPAWN_POINTS = 5;

// ── Particle stats (CPU-side tracking) ────────────────────────────────────────

struct ParticleStats {
    uint32_t conversion_count = 0;
    uint32_t membership_history_count = 0;
    float    spawn_time = 0.0f;
    int32_t  current_organism_id = -1;
};

// ── GPU push-constant block (must match GLSL layout exactly) ─────────────────

struct PushConstants {
    glm::vec2 region_size;        //   0 –   7
    glm::vec2 camera_origin;      //   8 –  15
    uint32_t  particle_count;     //  16 –  19
    uint32_t  particle_types;     //  20 –  23
    uint32_t  step;               //  24 –  27
    float     dt;                 //  28 –  31
    float     camera_zoom;        //  32 –  35
    float     radius;             //  36 –  39
    float     dampening;          //  40 –  43
    float     repulsion_radius;   //  44 –  47
    float     interaction_radius; //  48 –  51
    float     density_limit;      //  52 –  55
    float     metabolism;         //  56 –  59
    float     infection_rate;     //  60 –  63
    float     spawn_probability;  //  64 –  67
    uint32_t  halo_count;         //  68 –  71
    // ── Visual / post-processing ────────────────────────────────────────────
    float     trail_decay;        //  72 –  75  (1.0 = no trail, 0.85 = strong trail)
    float     bloom_threshold;    //  76 –  79
    float     bloom_intensity;    //  80 –  83
    float     vignette_strength;  //  84 –  87
    float     halo_intensity;     //  88 –  91
    float     time_seconds;       //  92 –  95
    uint32_t  effect_flags;       //  96 –  99  bit0=trails, bit1=bloom, bit2=vignette, bit3=halos
    float     day_night_factor;   // 100 – 103
};
static_assert(sizeof(PushConstants) == 104, "PushConstants layout mismatch");

// Effect flag bits (must match shader)
static constexpr uint32_t EFFECT_TRAILS   = 1u << 0;
static constexpr uint32_t EFFECT_BLOOM    = 1u << 1;
static constexpr uint32_t EFFECT_VIGNETTE = 1u << 2;
static constexpr uint32_t EFFECT_HALOS    = 1u << 3;

// ── Organism halo (uploaded each frame, std430-aligned) ──────────────────────

struct OrganismHaloGPU {
    glm::vec2 centroid;     //  0 –  7
    float     radius;       //  8 – 11
    uint32_t  dominant_type;// 12 – 15
};
static_assert(sizeof(OrganismHaloGPU) == 16, "OrganismHaloGPU layout mismatch");

// ── Conversion Matrix entry ──────────────────────────────────────────────────

struct ConversionData {
    int32_t  target_type = -1; // -1 = no conversion
    float    probability = 0.0f;
    uint32_t padding[2];       // align to 16 bytes for std430
};

// ── Simulation configuration (mirrors interface slider defaults from .tscn) ──

struct SimConfig {
    // Generation settings
    uint32_t particle_count     = 22500; // pow(150,2)
    uint32_t particle_types     = 5;
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    uint32_t generation_seed    = 0;

    // Physics / rendering (real-time sliders)
    float radius             = 2.0f;
    float dampening          = 0.85f;
    float repulsion_radius   = 20.0f;
    float interaction_radius = 60.0f;
    float density_limit      = 60.0f;
    float metabolism         = 0.15f;
    float infection_rate     = 0.2f;
    float spawn_probability  = 0.001f;

    // Conversion matrix: [type_a][type_b]
    ConversionData conversion_matrix[MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES];

    // Camera state (managed by simulation)
    glm::vec2 camera_origin      = { 0.0f, 0.0f };
    float     camera_zoom        = 1.0f;
    float     current_camera_zoom = 1.0f;

    // Visual / post-processing
    bool  trails_enabled    = true;
    bool  bloom_enabled     = true;
    bool  vignette_enabled  = true;
    bool  halos_enabled     = true;
    float trail_decay       = 0.90f;  // 1.0 = no trail
    float bloom_threshold   = 0.7f;
    float bloom_intensity   = 0.65f;
    float vignette_strength = 0.35f;
    float halo_intensity    = 0.45f;
};

// ── Colour helpers ────────────────────────────────────────────────────────────

inline glm::vec4 color_from_hsv(float h, float s, float v, float a = 1.0f) {
    if (s <= 0.0f) return { v, v, v, a };
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float ff = hh - (float)i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * ff);
    float t  = v * (1.0f - s * (1.0f - ff));
    switch (i % 6) {
        case 0: return { v, t, p, a };
        case 1: return { q, v, p, a };
        case 2: return { p, v, t, a };
        case 3: return { p, q, v, a };
        case 4: return { t, p, v, a };
        default: return { v, p, q, a };
    }
}

inline glm::vec4 calc_force_button_color(float force) {
    float abs_f = std::abs(force);
    if (force < 0.0f)
        return color_from_hsv(0.0f,   abs_f, abs_f); // red spectrum
    else
        return color_from_hsv(0.333f, abs_f, abs_f); // green spectrum
}
