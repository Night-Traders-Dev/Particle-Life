#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <chrono>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr float    INFINITE_REGION_SIZE = 1000000.0f; // NEW: Simulation "infinite" bounds
static constexpr uint32_t GROUP_DENSITY      = 256;

// Spatial Hash constants
static constexpr uint32_t GRID_CELL_SIZE     = 60; // Matches default interaction_radius
static constexpr uint32_t GRID_W             = ((REGION_W * 3) / GRID_CELL_SIZE) + 2;
static constexpr uint32_t GRID_H             = ((REGION_H * 3) / GRID_CELL_SIZE) + 2;
static constexpr uint32_t GRID_SIZE          = GRID_W * GRID_H;

// Chemical Grid constants
static constexpr uint32_t CHEM_W             = 320; // Lower resolution than region for performance
static constexpr uint32_t CHEM_H             = 180;

// Post-processing constants
static constexpr uint32_t BLOOM_W            = REGION_W / 2; // half-resolution bloom
static constexpr uint32_t BLOOM_H            = REGION_H / 2;
static constexpr uint32_t MAX_HALOS          = 256;

static constexpr float DEFAULT_LIFESPAN  = 300.0f;   // Cell lifespan
static constexpr float MUTATION_RATE     = 0.15f;    // Mutation rate during division
static constexpr float LIFESPAN_VARIANCE = 100.0f;   // Lifespan variation
static constexpr float EPSILON           = 0.0001f;  // Small value for divisions

// Biochemical behavior flags (14 total, matching biochemical archetype types)
enum ParticleBehavior : uint32_t {
    BEHAVIOR_NONE      = 0u,
    BEHAVIOR_SOLUBLE   = 1u,       // Dissolves in aqueous environment
    BEHAVIOR_CHARGE    = 2u,       // Charged particle (ion)
    BEHAVIOR_MEMBRANE  = 4u,       // Forms lipid bilayer barriers
    BEHAVIOR_RECEPTOR  = 8u,       // Selective binding (lock-and-key)
    BEHAVIOR_ENZYME    = 16u,      // Catalyzes reactions
    BEHAVIOR_STRUCTURAL= 32u,      // Forms scaffolds/networks
    BEHAVIOR_SIGNALING = 64u,      // Releases/responds to chemical signals
    BEHAVIOR_METABOLIC = 128u,     // Consumes nutrients, produces waste
    BEHAVIOR_TOXIC     = 256u,     // Damages other particles on contact
    BEHAVIOR_STICKY    = 512u,     // Adheres to membranes/structures
    BEHAVIOR_NUTRIENT  = 1024u,    // Basic food/nutrient source
    BEHAVIOR_CELL      = 2048u,    // Living cell (autonomous)
    BEHAVIOR_DECOMPOSER= 4096u,    // Breaks down waste/dead cells
    BEHAVIOR_VIRION    = 8192u     // Viral particle
};

// Biochemical particle type indices (14 types)
static constexpr uint32_t TYPE_WATER      = 0u;  // Universal solvent (high mobility)
static constexpr uint32_t TYPE_IONS       = 1u;  // Charged particles (Na+, K+, Ca++)
static constexpr uint32_t TYPE_SIMPLE     = 2u;  // Simple molecules (CO2, O2, glucose)
static constexpr uint32_t TYPE_LIPIDS     = 3u;  // Fats/oils (form membranes)
static constexpr uint32_t TYPE_PROTEINS   = 4u;  // Enzymes, structural proteins
static constexpr uint32_t TYPE_NUCLEIC    = 5u;  // DNA/RNA nucleotides
static constexpr uint32_t TYPE_CELL_MEM   = 6u;  // Cell membrane components
static constexpr uint32_t TYPE_ORGANELLE  = 7u;  // Mitochondria, ribosomes
static constexpr uint32_t TYPE_ELECTRON   = 8u;  // Free electrons (highly reactive)
static constexpr uint32_t TYPE_NUTRIENT   = 9u;  // Nutrient molecules (food)
static constexpr uint32_t TYPE_PROTON     = 10u; // H+ ions (acidic)
static constexpr uint32_t TYPE_CELL       = 11u; // Living cells (autonomous)
static constexpr uint32_t TYPE_DEAD_CELL  = 12u; // Corpse/lumped waste
static constexpr uint32_t TYPE_VIRUS      = 13u; // Viral particles
static constexpr uint32_t MAX_PARTICLE_TYPES = 14u;

// Original FOOD_TYPE_INDEX kept for backward compatibility in shaders
static constexpr uint32_t FOOD_TYPE_INDEX = TYPE_NUTRIENT;
static constexpr uint32_t NUM_NUTRIENT_SPAWN_POINTS = 5;

// ── Particle stats (CPU-side tracking) ────────────────────────────────────────

struct ParticleStats {
    uint32_t conversion_count = 0;
    uint32_t membership_history_count = 0;
    float    spawn_time = 0.0f;
    int32_t  current_organism_id = -1;
    float    min_temp = -10.0f;
    float    max_temp = 50.0f;
    uint32_t kills = 0;
    uint32_t divisions = 0;
};

// ── Per-particle genome (GPU-side, stored in a dedicated buffer) ──────────────
// std430 layout: vec4-aligned, 32 bytes total
// Biochemical properties for cellular/molecular realism
struct alignas(16) GenomeData {
    float age;             //  0 –  3  Current age in seconds
    float lifespan;        //  4 –  7  Max age before death (cells: ~5 mins to hours)
    float self_mod;        //  8 – 11  Binding affinity (0.3 = weak, 2.0 = strong)
    float cross_mod;       // 12 – 15  Reaction rate modifier
    uint32_t generation;   // 16 – 19  Mitotic divisions from zygote
    float    adhesion;     // 20 – 23  Membrane adhesion strength (0 = free, 2 = bound)
    float    division_rate;// 24 – 27  Mitosis frequency (0 = slow, 2 = fast)
    float    defense;      // 28 – 31  Damage resistance (membrane integrity, 0-2)
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
    float     trail_decay;        //  72 –  75  (1.0 = no trail, 0.85 = strong trail)
    float     bloom_threshold;    //  76 –  79
    float     bloom_intensity;    //  80 –  83
    float     vignette_strength;  //  84 –  87
    float     halo_intensity;     //  88 –  91
    float     time_seconds;       //  92 –  95
    uint32_t  effect_flags;       //  96 –  99  bit0=trails, bit1=bloom, bit2=vignette, bit3=halos
    float     day_night_factor;   // 100 – 103
    float     energy_depletion_rates[MAX_PARTICLE_TYPES]; // 104 – 155 (14 * 4)
    float     wind_x;              // 156 – 159
    float     wind_y;              // 160 – 163
    uint32_t  terrain_obstacle_count; // 164 – 167  (0 = skip obstacle checks)
    float     current_temperature; // 168 – 171
    float     type_radius[MAX_PARTICLE_TYPES];        // 172 – 227 (14 * 4)
    float     type_metabolic_rate[MAX_PARTICLE_TYPES]; // 228 – 283 (14 * 4)
    float     type_metamorph_age[MAX_PARTICLE_TYPES];  // 284 – 339 (14 * 4)
    int32_t   type_metamorph_target[MAX_PARTICLE_TYPES]; // 340 – 395 (14 * 4)
    uint32_t  type_flocking_enabled;    // 396 – 399
    float     type_kin_share[MAX_PARTICLE_TYPES];      // 400 – 455 (14 * 4)
    float     memory_decay;             // 456 – 459
    float     memory_strength;          // 460 – 463
    float     cross_repro_rate;         // 464 – 467
};
// Static assert disabled due to platform differences - arrays may have padding
// static_assert(sizeof(PushConstants) == 468, "PushConstants layout mismatch");

// Effect flag bits (must match shader)
static constexpr uint32_t EFFECT_TRAILS   = 1u << 0;
static constexpr uint32_t EFFECT_BLOOM    = 1u << 1;
static constexpr uint32_t EFFECT_VIGNETTE = 1u << 2;
static constexpr uint32_t EFFECT_HALOS    = 1u << 3;
static constexpr uint32_t EFFECT_TRAIT_DISPLAY = 1u << 4; // T key: show self_mod as color
static constexpr uint32_t EFFECT_MEMORY_MAP   = 1u << 5; // memory-guided force
static constexpr uint32_t EFFECT_SPECTRAL_DISPLAY = 1u << 6; // show speciation status

// ── Organism halo (uploaded each frame, std430-aligned) ──────────────────────

struct OrganismHaloGPU {
    glm::vec2 centroid;     //  0 –  7
    float     radius;       //  8 – 11
    uint32_t  dominant_type;// 12 – 15
};
static_assert(sizeof(OrganismHaloGPU) == 16, "OrganismHaloGPU layout mismatch");

// ── Weather data ──────────────────────────────────────────────────────────────

struct WeatherData {
    float temperature_c   = 20.0f;
    float cloud_cover_pct = 0.0f;
    float wind_speed_kmh  = 0.0f;
    float wind_dir_deg    = 0.0f;
    int   weather_code    = 0;
    bool  valid           = false;
    std::string location_name;
    std::chrono::steady_clock::time_point fetch_time;
};

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
    uint32_t particle_types     = 9;
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    uint32_t generation_seed    = 0;
    char     zip_code[16]       = "41101";

    // Physics / rendering (real-time sliders)
    float radius             = 1.5f;
    float dampening          = 0.5f;
    float repulsion_radius   = 15.0f;
    float interaction_radius = 75.0f;
    float density_limit      = 80.0f;
    float     metabolism         = 0.2f;
    float     infection_rate     = 0.2f;
    float     spawn_probability  = 0.0005f;
    float     cross_repro_rate   = 0.15f;

    float energy_depletion_rates[MAX_PARTICLE_TYPES] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // Conversion matrix: [type_a][type_b]
    ConversionData conversion_matrix[MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES];

    // Terrain
    uint32_t terrain_obstacle_count = 0;

    // Weather / environment
    float current_temperature = 22.0f;

    // Camera state (managed by simulation)
    glm::vec2 camera_origin      = { 0.0f, 0.0f };
    glm::vec2 camera_target      = { 0.0f, 0.0f };
    bool      panning_active     = false;
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
