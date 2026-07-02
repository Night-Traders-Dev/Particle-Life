#include "simulation.h"
#include "serialization.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

// ── Init / Destroy ────────────────────────────────────────────────────────────

void Simulation::init(GLFWwindow* window) {
    // Seed the interface (random starting seed)
    iface.init();
    cfg.generation_seed = static_cast<uint32_t>(iface.seed_value);

    // Vulkan setup
    vk.init(window);
    compute.init(vk, COMPUTE_SPV);
    renderer.init(vk, window, compute);

    // Generate first particle set
    reset();

    // Restore persisted zip code
    std::strncpy(iface.zip_code_buf, cfg.zip_code, sizeof(iface.zip_code_buf) - 1);
    iface.zip_code_buf[sizeof(iface.zip_code_buf) - 1] = '\0';
    if (cfg.zip_code[0] != '\0') {
        resolve_zip_code(cfg.zip_code);
        geolocation_fetched_ = true;
    } else {
        fetch_geolocation();
    }
    last_weather_fetch_ = std::chrono::steady_clock::now();
}

void Simulation::destroy() {
    vkDeviceWaitIdle(vk.device);
    compute.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void Simulation::reset() {
    vkDeviceWaitIdle(vk.device);
    particles.gen_data(cfg);
    compute.clear_buffers(vk);
    compute.create_buffers(vk, particles);
    organism_manager.reset();
    organism_tick_counter_ = 0;
}

    // ── Per-frame tick ────────────────────────────────────────────────────────────

void Simulation::tick(GLFWwindow* window, double dt) {
    // ── Input ──────────────────────────────────────────────────────────────────
    handle_input(window, dt);

    // Real-time day/night cycle based on system clock
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&now_t);
    day_night_time_ = tm->tm_hour * 3600.0 + tm->tm_min * 60.0 + tm->tm_sec;

    // Smooth sine-based day/night factor: peak at noon (1.0), trough at midnight (~0.3)
    float t = static_cast<float>(day_night_time_ / DAY_NIGHT_CYCLE_LENGTH);
    float sin_val = std::sin(static_cast<float>(2.0 * 3.1415926535 * (t - 0.25)));
    float day_night_factor = 0.65f + 0.35f * sin_val;

    // Check for zip code change from UI
    if (iface.zip_code_changed) {
        iface.zip_code_changed = false;
        std::string zip(iface.zip_code_buf);
        if (!zip.empty()) {
            std::strncpy(cfg.zip_code, zip.c_str(), sizeof(cfg.zip_code) - 1);
            cfg.zip_code[sizeof(cfg.zip_code) - 1] = '\0';
            resolve_zip_code(zip);
            geolocation_fetched_ = true;
            last_weather_fetch_ = std::chrono::steady_clock::time_point{}; // force immediate fetch
            Serialization::save_config("config.bin", cfg, particles);
        }
    }

    // Weather fetch (every 10 minutes)
    auto now_steady = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_weather_fetch_).count();
    if (elapsed > 600) {
        last_weather_fetch_ = now_steady;
        fetch_weather();
    }

    // Apply weather effects
    if (weather_.valid) {
        float cloud_dim = 1.0f - weather_.cloud_cover_pct / 100.0f * 0.35f;
        day_night_factor *= cloud_dim;
    }

    // ── Upload dynamic GPU data ────────────────────────────────────────────────
    if (is_active)
        compute.upload_dynamic_data(vk, particles);

    // ── ImGui ──────────────────────────────────────────────────────────────────
    bool request_reset = false;
    uint32_t prev_count = cfg.particle_count;
    iface.render_imgui(cfg, particles, organism_manager, request_reset, day_night_time_, DAY_NIGHT_CYCLE_LENGTH, &weather_);
    time_scale_ = iface.time_scale_slider;

    if (cfg.particle_count != prev_count) {
        // Only resize when particle count changes, don't re-gen full data
        compute.resize_buffers(vk, particles);
        organism_tick_counter_ = 0;
    }

    if (request_reset)
        reset();

    // ── Record compute command buffer ─────────────────────────────────────────
    // We encode the compute work into a separate one-shot command buffer
    // that we submit before the render frame so the image is ready.
    if (is_active && compute.is_ready()) {
        // Use a temporary one-time command buffer for the compute pass
        VkCommandBuffer compute_cmd = vk.begin_single_command();

        float scaled_dt = static_cast<float>(dt) * 5.0f * time_scale_ * day_night_factor;
        glm::vec2 wind_force(0.0f);
        if (weather_.valid && weather_.wind_speed_kmh > 0.1f) {
            float wind_rad = glm::radians(weather_.wind_dir_deg);
            float strength = weather_.wind_speed_kmh * 0.002f;
            wind_force = glm::vec2(std::cos(wind_rad), std::sin(wind_rad)) * strength;
        }
        compute.record(compute_cmd, cfg, scaled_dt, 0, (float)glfwGetTime(), day_night_factor, wind_force);
        vk.end_single_command(compute_cmd);

        // Organism detection (every N frames)
        organism_tick_counter_++;
        
        // Procedural food spawning
        if (iface.autospawn_enabled && organism_tick_counter_ % 600 == 0) {
            std::mt19937 rng(std::random_device{}());
            // Spawn within the current camera view
            float view_w = (float)REGION_W / cfg.current_camera_zoom;
            float view_h = (float)REGION_H / cfg.current_camera_zoom;
            std::uniform_real_distribution<float> dist_x(cfg.camera_origin.x - view_w/2.0f, cfg.camera_origin.x + view_w/2.0f);
            std::uniform_real_distribution<float> dist_y(cfg.camera_origin.y - view_h/2.0f, cfg.camera_origin.y + view_h/2.0f);
            
            // Random clump location
            float cx = dist_x(rng);
            float cy = dist_y(rng);
            std::uniform_real_distribution<float> clump_dist(-50.0f, 50.0f);
            
            // Sync current state to CPU before modifying
            const uint32_t n = cfg.particle_count;
            compute.read_current_state(vk, particles.positions, particles.velocities, particles.types);
            // Sync energy/angles/angular_velocities/stats if needed? 
            // Currently they are kept in sync by add_particle and resize calls.
            
            int food_to_spawn = 100;
            int extra_particles = (int)(cfg.particle_count * 0.10f);
            uint32_t new_total = n + food_to_spawn + extra_particles;
            
            particles.positions.resize(new_total);
            particles.velocities.resize(new_total);
            particles.types.resize(new_total);
            particles.energy.resize(new_total, 1.0f);
            particles.angles.resize(new_total, 0.0f);
            particles.angular_velocities.resize(new_total, 0.0f);
            particles.stats.resize(new_total);
            
            // Spawn 100 food particles in a clump
            for(int i=0; i<food_to_spawn; ++i) {
                int idx = n + i;
                particles.positions[idx] = glm::vec2(cx + clump_dist(rng), cy + clump_dist(rng));
                particles.velocities[idx] = glm::vec2(0.0f);
                particles.types[idx] = FOOD_TYPE_INDEX;
                particles.energy[idx] = 1.0f;
                particles.angles[idx] = 0.0f;
                particles.angular_velocities[idx] = 0.0f;
                particles.stats[idx] = ParticleStats{};
            }
            
            // Spawn 10% more non-food particles in view
            std::uniform_int_distribution<uint32_t> type_dist(0, cfg.particle_types - 1);
            for(int i=0; i < extra_particles; ++i) {
                int idx = n + food_to_spawn + i;
                uint32_t t = type_dist(rng);
                particles.positions[idx] = glm::vec2(dist_x(rng), dist_y(rng));
                particles.velocities[idx] = glm::vec2(0.0f);
                particles.types[idx] = t;
                particles.energy[idx] = 1.0f;
                particles.angles[idx] = 0.0f;
                particles.angular_velocities[idx] = 0.0f;
                particles.stats[idx] = ParticleStats{};
            }
            
            cfg.particle_count = new_total;
            compute.resize_buffers(vk, particles);
        }

        // Update particle ages and apply temperature mortality (CPU-side tracking)
        float current_temp = 22.5f + 12.5f * std::sin(static_cast<float>(2.0 * 3.1415926535 * (t - 0.125)));
        if (weather_.valid) {
            // Blend weather temp with diurnal model for smooth transitions
            float diurnal = 5.0f * std::sin(static_cast<float>(2.0 * 3.1415926535 * (t - 0.125)));
            current_temp = weather_.temperature_c + diurnal;
        }
        for (size_t i = 0; i < particles.stats.size(); ++i) {
            auto& s = particles.stats[i];
            s.spawn_time += static_cast<float>(dt) * time_scale_;
            
            // Check mortality
            if (current_temp < s.min_temp || current_temp > s.max_temp) {
                // "Kill" particle by setting energy to 0 and position off-screen
                particles.energy[i] = 0.0f;
                particles.positions[i] = glm::vec2(-100000.0f);
            }
        }

        if (organism_tick_counter_ % ORGANISM_UPDATE_INTERVAL == 0) {
            readback_positions_.resize(cfg.particle_count);
            readback_velocities_.resize(cfg.particle_count);
            readback_types_.resize(cfg.particle_count);
            compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_types_);
            
            // Sync back to CPU particles if we want them to stay in sync
            particles.types = readback_types_;

            organism_manager.update(readback_positions_, readback_velocities_,
                                    particles.types, particles);
        }
    }

    // ── Draw frame (fullscreen quad + ImGui) ──────────────────────────────────
    if (renderer.swapchain_dirty)
        renderer.on_resize(vk, window);

    renderer.draw_frame(vk, compute, is_active, cfg, particles, organism_manager, day_night_factor);
}

// ── Input handling ────────────────────────────────────────────────────────────

void Simulation::handle_input(GLFWwindow* window, double dt) {
    // ── Keyboard ──────────────────────────────────────────────────────────────

    // ESC: quit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    // Space: pause / unpause
    static bool space_prev = false;
    bool space_cur = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    if (space_cur && !space_prev)
        is_active = !is_active;
    space_prev = space_cur;

    // F1: toggle settings panel
    static bool f1_prev = false;
    bool f1_cur = (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS);
    if (f1_cur && !f1_prev)
        iface.settings_visible = !iface.settings_visible;
    f1_prev = f1_cur;

    // F2: reset simulation
    static bool f2_prev = false;
    bool f2_cur = (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS);
    if (f2_cur && !f2_prev)
        reset();
    f2_prev = f2_cur;

    // F11: toggle fullscreen
    static bool f11_prev = false;
    bool f11_cur = (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS);
    if (f11_cur && !f11_prev) {
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
        static bool        is_fs   = false;
        if (!is_fs) {
            glfwSetWindowMonitor(window, monitor, 0, 0,
                                 mode->width, mode->height, mode->refreshRate);
            is_fs = true;
        } else {
            glfwSetWindowMonitor(window, nullptr, 100, 100,
                                 REGION_W / 2, REGION_H / 2, 0);
            is_fs = false;
        }
    }
    f11_prev = f11_cur;

    // ── Mouse: camera pan ─────────────────────────────────────────────────────
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    glm::vec2 mouse_pos = { static_cast<float>(mx), static_cast<float>(my) };
    glm::vec2 raw_change = mouse_pos - last_mouse_pos_;
    last_mouse_pos_ = mouse_pos;

    bool lmb = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

    if (!iface.mouse_within || !iface.settings_visible) {
        if (lmb) {
            smooth_mouse_change_ += raw_change * static_cast<float>(dt);
            cfg.camera_origin    -= smooth_mouse_change_ / cfg.current_camera_zoom;
        }
    }

    if (!lmb) {
        raw_change = {};
        cfg.camera_origin    -= smooth_mouse_change_ / cfg.current_camera_zoom;
        smooth_mouse_change_  = glm::mix(smooth_mouse_change_, glm::vec2(0.0f),
                                          static_cast<float>(dt) * 4.0f);
    }

    // ── Mouse: spawn particle ───────────────────────────────────────────────
    static bool lmb_down_prev = false;
    bool lmb_down_curr = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    if (lmb_down_curr && !lmb_down_prev && !iface.mouse_within && is_active && compute.is_ready()) {
        double mx2, my2;
        glfwGetCursorPos(window, &mx2, &my2);
        glm::vec2 mouse_pos2 = { static_cast<float>(mx2), static_cast<float>(my2) };
        glm::vec2 world_pos  = (mouse_pos2 - glm::vec2(1280.0f, 720.0f)) / cfg.current_camera_zoom + cfg.camera_origin;

        // Sync CPU particle arrays from current GPU state so resize_buffers
        // doesn't snap every particle back to its initial spawn position.
        // (positions / velocities / types are double-buffered on the GPU; the
        // angle/energy buffers are not read back here — losing them on spawn
        // is acceptable and avoids extra readback paths.)
        const uint32_t n = cfg.particle_count;
        particles.positions.resize(n);
        particles.velocities.resize(n);
        particles.types.resize(n);
        compute.read_current_state(vk,
                                   particles.positions,
                                   particles.velocities,
                                   particles.types);

        // Keep per-particle aux arrays in lock-step with positions.
        particles.energy.assign(n, 1.0f);
        particles.angles.resize(n, 0.0f);
        particles.angular_velocities.resize(n, 0.0f);
        particles.stats.resize(n, ParticleStats{});

        // Append the new particle and rebuild GPU buffers.
        particles.add_particle(world_pos, glm::vec2(0.0f), 0);
        cfg.particle_count++;
        iface.particle_count_slider = std::sqrt(static_cast<float>(cfg.particle_count));
        compute.resize_buffers(vk, particles);
    }
    lmb_down_prev = lmb_down_curr;

    // Ensure right click doesn't trigger anything else (e.g. reset)
    // Right button is handled by the force grid/UI specifically
    // Removed the problematic f2 logic previously attached to mouse button.
}

// ── Weather HTTP helpers ──────────────────────────────────────────────────────

bool Simulation::http_fetch(const std::string& url, std::string& result) {
    std::string cmd = "curl -s --max-time 5 \"" + url + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    int ret = pclose(pipe);
    return ret == 0 && !result.empty();
}

float Simulation::extract_json_float(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0.0f;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos) return 0.0f;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return 0.0f;
    bool quoted = (json[pos] == '"');
    if (quoted) pos++;
    auto end = json.find_first_of(quoted ? "\"" : ",}\n\r", pos);
    if (end == std::string::npos) end = json.size();
    try { return std::stof(json.substr(pos, end - pos)); }
    catch (...) { return 0.0f; }
}

std::string Simulation::extract_json_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return "";
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

void Simulation::resolve_zip_code(const std::string& zip) {
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + zip + "&count=1&language=en&format=json";
    std::string json;
    if (!http_fetch(url, json)) return;
    auto results = json.find("\"results\"");
    if (results == std::string::npos) return;
    std::string first = json.substr(results);
    latitude_ = extract_json_float(first, "latitude");
    longitude_ = extract_json_float(first, "longitude");
    location_name_ = extract_json_string(first, "name");
    if (!location_name_.empty()) {
        std::string country = extract_json_string(first, "country");
        if (!country.empty()) location_name_ += ", " + country;
    }
    weather_.location_name = location_name_;
}

void Simulation::fetch_geolocation() {
    if (geolocation_fetched_) return;
    std::string json;
    if (!http_fetch("http://ip-api.com/json/?fields=lat,lon", json)) {
        // Fallback: moderate US location
        latitude_ = 40.7128f; longitude_ = -74.0060f;
        return;
    }
    latitude_ = extract_json_float(json, "lat");
    longitude_ = extract_json_float(json, "lon");
    geolocation_fetched_ = true;
}

void Simulation::fetch_weather() {
    if (!geolocation_fetched_) fetch_geolocation();
    std::string url = "https://api.open-meteo.com/v1/forecast?"
        "latitude=" + std::to_string(latitude_) +
        "&longitude=" + std::to_string(longitude_) +
        "&current=temperature_2m,weather_code,precipitation,wind_speed_10m,wind_direction_10m,cloud_cover";
    std::string json;
    if (!http_fetch(url, json)) {
        weather_.valid = false;
        return;
    }
    // Navigate to the "current" object
    auto cur = json.find("\"current\"");
    if (cur == std::string::npos) { weather_.valid = false; return; }
    std::string current = json.substr(cur);

    WeatherData w{};
    w.temperature_c   = extract_json_float(current, "temperature_2m");
    w.cloud_cover_pct = extract_json_float(current, "cloud_cover");
    w.wind_speed_kmh  = extract_json_float(current, "wind_speed_10m");
    w.wind_dir_deg    = extract_json_float(current, "wind_direction_10m");
    w.weather_code    = static_cast<int>(extract_json_float(current, "weather_code"));
    w.fetch_time      = std::chrono::steady_clock::now();
    w.valid           = true;
    weather_ = w;
    weather_fetched_ = true;
}

// ── Scroll callback (called from main.cpp) ────────────────────────────────────

// Accessed via a global pointer so the GLFW callback can reach it.
static Simulation* g_sim = nullptr;

static void scroll_callback(GLFWwindow*, double, double y_offset) {
    if (!g_sim) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;  // ImGui consumed this

    float& zoom = g_sim->cfg.current_camera_zoom;
    if (y_offset > 0)
        zoom *= 1.25f;
    else if (y_offset < 0)
        zoom *= 0.8f;
    zoom = std::clamp(zoom, 0.02f, 500.0f);
}

void Simulation_RegisterScrollCallback(GLFWwindow* window, Simulation* sim) {
    g_sim = sim;
    glfwSetScrollCallback(window, scroll_callback);
}
