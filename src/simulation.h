#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include "compute_pipeline.h"
#include "renderer.h"
#include "interface.h"
#include "organism.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <string>

class Simulation;
// Call once after init() to hook the GLFW scroll callback
void Simulation_RegisterScrollCallback(GLFWwindow* window, Simulation* sim);
// Owns all subsystems, manages the main loop, handles input.

class Simulation {
public:
    bool is_active = true;

    void init(GLFWwindow* window);
    void destroy();

    // Called every frame from main()
    void tick(GLFWwindow* window, double dt);

    // Resets particle data and rebuilds GPU buffers
    void reset();

    SimConfig       cfg{};
    Particles       particles{};
    VulkanContext   vk{};
    ComputePipeline compute{};
    Renderer        renderer{};
    Interface       iface{};
    OrganismManager organism_manager{};

private:
    // Display state
    int      palette_index_       = 0;        // 0=default, 1=viridis, 2=plasma, 3=magma, 4=inferno
    bool     trait_display_mode_  = false;    // T key toggles self_mod coloring

    // Input state
    glm::vec2 last_mouse_pos_      = {};
    glm::vec2 mouse_change_        = {};
    glm::vec2 smooth_mouse_change_ = {};
    bool      lmb_down_            = false;
    float     time_scale_          = 1.0f;
    double    day_night_time_      = 0.0;
    double    fps_                 = 0.0;
    const double DAY_NIGHT_CYCLE_LENGTH = 86400.0; // 24 hours in seconds

    // Weather
    float     latitude_  = 0.0f;
    float     longitude_ = 0.0f;
    std::string location_name_;
    WeatherData weather_{};
    std::chrono::steady_clock::time_point last_weather_fetch_;
    bool      geolocation_fetched_ = false;
    bool      weather_fetched_     = false;

    void fetch_geolocation();
    void fetch_weather();
    void resolve_zip_code(const std::string& zip);
    void generate_terrain();
    void spawn_seasonal_food();
    void save_screenshot();
    bool http_fetch(const std::string& url, std::string& result);
    float extract_json_float(const std::string& json, const std::string& key);
    std::string extract_json_string(const std::string& json, const std::string& key);

    // Organism tracking
    int                    organism_tick_counter_ = 0;
    std::vector<glm::vec2> readback_positions_;
    std::vector<glm::vec2> readback_velocities_;
    std::vector<uint32_t>  readback_types_;
    std::vector<glm::vec2> cached_positions_; // for particle selection (updated on org detection)

    // Shader SPIRV paths (relative to working directory = build dir)
    static constexpr const char* COMPUTE_SPV  = "shaders/compute.spv";
    static constexpr const char* VERT_SPV     = "shaders/fullscreen.vert.spv";
    static constexpr const char* FRAG_SPV     = "shaders/fullscreen.frag.spv";

    void handle_input(GLFWwindow* window, double dt);
};
