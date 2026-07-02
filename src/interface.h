#pragma once

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include "types.h"
#include "particles.h"
#include "organism.h"
#include <vector>
#include <array>
#include <glm/glm.hpp>

// Mirrors interface.gd.
// All state lives here; Simulation calls render_imgui() once per frame.

class Interface {
public:
    bool settings_visible = true;
    bool mouse_within     = false; // true when cursor is over the settings panel
    bool glow_enabled     = false;
    bool autospawn_enabled = false;

    // ── Sliders (raw slider values, converted to actual params by render_imgui) ─
    float particle_count_slider  = 150.0f;  // particle_count = pow(value, 2)
    float particle_types_slider  = 6.0f;
    float particle_radius_slider = 1.5f;
    float dampening_slider       = 0.5f;
    float repulsion_slider       = 15.0f;
    float interaction_slider     = 75.0f;
    float density_limit_slider   = 80.0f;
    float metabolism_slider      = 0.2f;
    float infection_slider       = 0.2f;
    float spawn_slider           = 0.003f;
    float time_scale_slider      = 1.5f;
    int   seed_value             = 0;
    bool  reset_colors_check     = false;
    bool  reset_forces_check     = true;

    // Archetype preset selection
    int      preset_selection[MAX_PARTICLE_TYPES] = {};
    bool symmetry_enabled = false;
    int  hover_particle_index = -1;
    int64_t  hover_organism_id = -1; // NEW: Track hovered organism

    bool show_metrics_window = false; // NEW
    int  metrics_tab = 0;             // NEW

    // Zip code for weather
    char  zip_code_buf[16]   = "";
    bool  zip_code_changed   = false;

    // ── Analytics rolling buffers ─────────────────────────────────────────────
    static constexpr int HISTORY_LEN = 300;
    int   history_head = 0;
    float population_history[MAX_PARTICLE_TYPES][300] = {};
    float total_energy_history[300] = {};
    float avg_speed_history[300] = {};
    float organism_count_history[300] = {};
    float birth_count_history[300] = {};
    float death_count_history[300] = {};
    int   frame_counter = 0;
    uint32_t prev_particle_count = 0;

    // Initialise with a random seed
    void init();

    void draw_hover_popup(const Particles& particles, const OrganismManager& org_manager); // NEW
    void draw_metrics_window(SimConfig& cfg, Particles& particles, OrganismManager& org_manager); // NEW

    // Draw all ImGui windows and return updated config.

    // Call once per frame BEFORE ImGui::Render().
    // `request_reset` is set to true if the user clicks the Reset button.
    void render_imgui(SimConfig&       cfg,
                      Particles&       particles,
                      OrganismManager& org_manager,
                      bool&            request_reset,
                      double           day_night_time,
                      double           cycle_length,
                      const WeatherData* weather = nullptr);

private:
    void draw_particle_grid(SimConfig& cfg, Particles& particles);
    void draw_organism_panel(OrganismManager& org_manager,
                             const Particles& particles,
                             const SimConfig& cfg);

    void draw_archetype_panel(Particles& particles, const SimConfig& cfg);
    void draw_conversion_panel(Particles& particles, const SimConfig& cfg);

    static ImVec4 force_to_color(float f);
};
