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
#include <fstream>

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
        request_zip_resolve(cfg.zip_code);
        last_weather_fetch_ = std::chrono::steady_clock::time_point{}; // fetch weather asap
    } else {
        // Sync geolocation at startup (fast ip-api call, happens before main loop)
        std::string json;
        if (http_fetch_sync("http://ip-api.com/json/?fields=lat,lon", json)) {
            latitude_ = extract_json_float(json, "lat");
            longitude_ = extract_json_float(json, "lon");
        } else {
            latitude_ = 40.7128f; longitude_ = -74.0060f; // fallback NYC
        }
        geolocation_fetched_ = true;
        last_weather_fetch_ = std::chrono::steady_clock::now();
    }

    // Generate terrain obstacles
    generate_terrain();
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

    // Set per-type properties for diversity
    for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
        cfg.energy_depletion_rates[t] = 0.5f + (float)t / 8.0f * 1.5f;
    }

    // Set migrator flag on every other type (1, 3, 5, 7)
    for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
        if (t % 2 == 1 && t != FOOD_TYPE_INDEX) {
            particles.behavior_flags[t] |= BEHAVIOR_MIGRATOR;
        }
    }

    compute.clear_buffers(vk);
    compute.create_buffers(vk, particles);
    organism_manager.reset();

    // Initialize species records
    for (uint32_t t = 0; t < cfg.particle_types; ++t) {
        organism_manager.species_records[t] = {};
        organism_manager.species_records[t].avg_self_mod = 0.8f + (float)t * 0.1f;
        organism_manager.species_records[t].avg_cross_mod = 0.8f;
        organism_manager.species_records[t].avg_lifespan = 300.0f;
    }

    organism_tick_counter_ = 0;
    niche_tick_counter_ = 0;
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

    // Drain completed async HTTP responses (non-blocking)
    process_http_response();

    // Check for zip code change from UI
    if (iface.zip_code_changed) {
        iface.zip_code_changed = false;
        std::string zip(iface.zip_code_buf);
        if (!zip.empty()) {
            std::strncpy(cfg.zip_code, zip.c_str(), sizeof(cfg.zip_code) - 1);
            cfg.zip_code[sizeof(cfg.zip_code) - 1] = '\0';
            request_zip_resolve(zip);
            last_weather_fetch_ = std::chrono::steady_clock::time_point{}; // force immediate fetch
            Serialization::save_config("config.bin", cfg, particles);
        }
    }

    // Weather fetch (every 60 seconds, non-blocking)
    auto now_steady = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_steady - last_weather_fetch_).count();
    if (elapsed > 60 && http_action_ == HttpAction::NONE) {
        last_weather_fetch_ = now_steady;
        request_weather_fetch();
    }

    // Apply weather effects
    if (weather_.valid) {
        float cloud_dim = 1.0f - weather_.cloud_cover_pct / 100.0f * 0.35f;
        day_night_factor *= cloud_dim;
    }

    // ── Upload dynamic GPU data ────────────────────────────────────────────────
    if (is_active)
        compute.upload_dynamic_data(vk, particles);

    // ── FPS tracking ───────────────────────────────────────────────────────────
    if (dt > 0.0) {
        double smooth = 0.05;
        fps_ = (fps_ > 0.0) ? fps_ * (1.0 - smooth) + (1.0 / dt) * smooth : 1.0 / dt;
    }

    // ── Avg generation from particles.genome (CPU-side, updated on organism readback) ──
    uint32_t avg_generation = 0;
    if (!particles.types.empty()) {
        // Use organism manager's per-type generation data as a proxy
        avg_generation = organism_manager.avg_generation;
    }

    // ── ImGui ──────────────────────────────────────────────────────────────────
    bool request_reset = false;
    uint32_t prev_count = cfg.particle_count;
    iface.render_imgui(cfg, particles, organism_manager, request_reset,
                       day_night_time_, DAY_NIGHT_CYCLE_LENGTH,
                       !is_active, fps_, avg_generation, &weather_);
    time_scale_ = iface.time_scale_slider;

    if (cfg.particle_count != prev_count) {
        // Only resize when particle count changes, don't re-gen full data
        compute.resize_buffers(vk, particles);
        organism_tick_counter_ = 0;
    }

    if (request_reset)
        reset();

    // ── Compute parameters ───────────────────────────────────────────────
    if (is_active && compute.is_ready()) {
        // Compute current temperature for GPU shader
        float current_temp = 22.5f + 12.5f * std::sin(static_cast<float>(2.0 * 3.1415926535 * (t - 0.125)));
        if (weather_.valid) {
            float diurnal = 5.0f * std::sin(static_cast<float>(2.0 * 3.1415926535 * (t - 0.125)));
            current_temp = weather_.temperature_c + diurnal;
        }
        cfg.current_temperature = current_temp;
        organism_tick_counter_++;
    }

    // ── Draw frame (compute + render in one command buffer) ──────────────
    float scaled_dt = static_cast<float>(dt) * 5.0f * time_scale_ * day_night_factor;
    glm::vec2 wind_force(0.0f);
    if (weather_.valid && weather_.wind_speed_kmh > 0.1f) {
        float wind_rad = glm::radians(weather_.wind_dir_deg);
        float strength = weather_.wind_speed_kmh * 0.002f;
        wind_force = glm::vec2(std::cos(wind_rad), std::sin(wind_rad)) * strength;
    }
    if (renderer.swapchain_dirty)
        renderer.on_resize(vk, window);
    uint32_t extra_flags = trait_display_mode_ ? EFFECT_TRAIT_DISPLAY : 0u;
    renderer.draw_frame(vk, compute, is_active, cfg, particles, organism_manager,
                         day_night_factor, scaled_dt, (float)glfwGetTime(), wind_force, extra_flags);
    if (is_active && compute.is_ready()) {
        // Natural food spawning: small food patches grow from existing food clusters
        if (iface.autospawn_enabled && organism_tick_counter_ % 600 == 0) {
            vkQueueWaitIdle(vk.queue);
            compute.read_current_state(vk, particles.positions, particles.velocities, particles.types);
            const uint32_t n = cfg.particle_count;

            // Find existing food clusters to seed new growth around them
            std::mt19937 rng(std::random_device{}());
            int food_to_spawn = 0;
            int food_count = 0;
            for (uint32_t i = 0; i < n; ++i)
                if (particles.types[i] == FOOD_TYPE_INDEX)
                    food_count++;
            // Spawn more food when existing food is abundant (bloom), less when scarce
            food_to_spawn = food_count > 50 ? 40 : (food_count > 10 ? 20 : 10);

            if (food_to_spawn > 0) {
                uint32_t new_total = n + food_to_spawn;
                particles.positions.resize(new_total);
                particles.velocities.resize(new_total);
                particles.types.resize(new_total);
                particles.energy.resize(new_total, 1.0f);
                particles.angles.resize(new_total, 0.0f);
                particles.angular_velocities.resize(new_total, 0.0f);
                particles.stats.resize(new_total);

                // Spawn near existing food when possible, otherwise random
                float half_w = float(REGION_W) * 1.5f;
                float half_h = float(REGION_H) * 1.5f;
                std::uniform_real_distribution<float> world_x(-half_w, half_w);
                std::uniform_real_distribution<float> world_y(-half_h, half_h);
                std::uniform_real_distribution<float> jitter(-100.0f, 100.0f);

                for (int i = 0; i < food_to_spawn; ++i) {
                    int idx = n + i;
                    if (food_count > 0 && (rng() % 3) != 0) {
                        // 2/3 chance: spawn near an existing food particle
                        uint32_t src = uint32_t(rng() % n);
                        int tries = 0;
                        while (particles.types[src] != FOOD_TYPE_INDEX && tries < 20) {
                            src = uint32_t(rng() % n);
                            tries++;
                        }
                        if (particles.types[src] == FOOD_TYPE_INDEX) {
                            particles.positions[idx] = particles.positions[src] + glm::vec2(jitter(rng), jitter(rng));
                            particles.types[idx] = FOOD_TYPE_INDEX;
                            particles.energy[idx] = 0.6f;
                        } else {
                            particles.positions[idx] = glm::vec2(world_x(rng), world_y(rng));
                            particles.types[idx] = FOOD_TYPE_INDEX;
                            particles.energy[idx] = 0.6f;
                        }
                    } else {
                        particles.positions[idx] = glm::vec2(world_x(rng), world_y(rng));
                        particles.types[idx] = FOOD_TYPE_INDEX;
                        particles.energy[idx] = 0.6f;
                    }
                    particles.velocities[idx] = glm::vec2(0.0f);
                    particles.angles[idx] = 0.0f;
                    particles.angular_velocities[idx] = 0.0f;
                    particles.stats[idx] = ParticleStats{};
                }
                cfg.particle_count = new_total;
                if (new_total > compute.capacity())
                    compute.resize_buffers(vk, particles);
                else
                    compute.upload_particle_range(vk, particles, n, food_to_spawn);
            }
        }

        // Seasonal food spawning (every 1200 frames)
        if (iface.autospawn_enabled && organism_tick_counter_ % 1200 == 0)
            spawn_seasonal_food();

        // Niche construction (every ~1200 frames)
        if (readback_positions_.size() == cfg.particle_count)
            update_niche_construction();

        // Organism detection (every N frames)
        if (organism_tick_counter_ % ORGANISM_UPDATE_INTERVAL == 0) {
            vkQueueWaitIdle(vk.queue);
            readback_positions_.resize(cfg.particle_count);
            readback_velocities_.resize(cfg.particle_count);
            readback_types_.resize(cfg.particle_count);
            compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_types_);
            particles.types = readback_types_;
            cached_positions_ = readback_positions_;
            organism_manager.update(readback_positions_, readback_velocities_,
                                    particles.types, particles, &cfg);

            if (readback_positions_.size() == readback_types_.size())
                update_seasonal_migration();

            // Compute ecosystem health metrics from readback data
            {
                uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
                uint32_t total = 0;
                for (uint32_t i = 0; i < readback_types_.size(); ++i) {
                    uint32_t t = readback_types_[i];
                    if (t < MAX_PARTICLE_TYPES) { type_counts[t]++; total++; }
                }
                iface.current_diversity = iface.simpson_diversity(type_counts, cfg.particle_types, total);

                // Energy flux
                float total_energy = 0.0f;
                for (auto& p : particles.energy) total_energy += p;
                int head = iface.history_head;
                iface.total_energy_history[head] = total_energy;

                int prev_head = (head == 0) ? iface.HISTORY_LEN - 1 : head - 1;
                iface.current_energy_flux = total_energy - iface.total_energy_history[prev_head];

                // Trophic efficiency: (prey deaths) / (predator energy)
                uint32_t current_deaths = 0;
                for (uint32_t i = 0; i < readback_types_.size(); ++i) {
                    if (readback_types_[i] == FOOD_TYPE_INDEX && particles.types[i] != FOOD_TYPE_INDEX)
                        current_deaths++;
                }
                uint32_t deaths_delta = (current_deaths >= iface.prev_deaths) ? current_deaths - iface.prev_deaths : 0;
                iface.prev_deaths = current_deaths;
                float pred_energy = 0.0f;
                for (uint32_t i = 0; i < readback_types_.size(); ++i) {
                    if (readback_types_[i] < MAX_PARTICLE_TYPES &&
                        (particles.behavior_flags[readback_types_[i]] & BEHAVIOR_PREDATOR) != 0u)
                        pred_energy += particles.energy[i];
                }
                iface.current_trophic_efficiency = (deaths_delta > 0 && pred_energy > 0.01f)
                    ? (float)deaths_delta / pred_energy : 0.0f;
            }
        }
    }
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

    // F3: toggle HUD overlay
    static bool f3_prev = false;
    bool f3_cur = (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS);
    if (f3_cur && !f3_prev)
        iface.hud_visible = !iface.hud_visible;
    f3_prev = f3_cur;

    // P: cycle palette forward (default → viridis → plasma → magma → inferno → default)
    static bool p_prev = false;
    bool p_cur = (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS);
    if (p_cur && !p_prev) {
        palette_index_ = (palette_index_ + 1) % 5;
        particles.set_palette(palette_index_);
        compute.upload_colors(vk, particles);
    }
    p_prev = p_cur;

    // B: cycle palette backward
    static bool b_prev = false;
    bool b_cur = (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS);
    if (b_cur && !b_prev) {
        palette_index_ = (palette_index_ + 4) % 5;
        particles.set_palette(palette_index_);
        compute.upload_colors(vk, particles);
    }
    b_prev = b_cur;

    // N: reset to default palette
    static bool n_prev = false;
    bool n_cur = (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS);
    if (n_cur && !n_prev) {
        palette_index_ = 0;
        particles.set_palette(0);
        compute.upload_colors(vk, particles);
    }
    n_prev = n_cur;

    // M: switch to magma palette
    static bool m_prev = false;
    bool m_cur = (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS);
    if (m_cur && !m_prev) {
        palette_index_ = 3;
        particles.set_palette(3);
        compute.upload_colors(vk, particles);
    }
    m_prev = m_cur;

    // T: toggle trait display mode
    static bool t_prev = false;
    bool t_cur = (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS);
    if (t_cur && !t_prev)
        trait_display_mode_ = !trait_display_mode_;
    t_prev = t_cur;

    // F12: save screenshot
    static bool f12_prev = false;
    bool f12_cur = (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS);
    if (f12_cur && !f12_prev) {
        save_screenshot();
    }
    f12_prev = f12_cur;

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
        // Wait for previous frame to finish so GPU buffers are ready for readback
        vkQueueWaitIdle(vk.queue);
        compute.read_current_state(vk,
                                   particles.positions,
                                   particles.velocities,
                                   particles.types);

        // Keep per-particle aux arrays in lock-step with positions.
        particles.energy.assign(n, 1.0f);
        particles.angles.resize(n, 0.0f);
        particles.angular_velocities.resize(n, 0.0f);
        particles.stats.resize(n, ParticleStats{});

        // Append the new particle and update GPU buffers.
        particles.add_particle(world_pos, glm::vec2(0.0f), iface.spawn_type_index);
        cfg.particle_count++;
        iface.particle_count_slider = std::sqrt(static_cast<float>(cfg.particle_count));
        if (cfg.particle_count > compute.capacity())
            compute.resize_buffers(vk, particles);
        else
            compute.upload_particle_range(vk, particles, n, 1);
    }
    lmb_down_prev = lmb_down_curr;

    // Right-click: select nearest particle for inspection
    static bool rmb_prev = false;
    bool rmb_cur = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    if (rmb_cur && !rmb_prev && !iface.mouse_within) {
        double mx2, my2;
        glfwGetCursorPos(window, &mx2, &my2);
        glm::vec2 mouse_pos2 = { static_cast<float>(mx2), static_cast<float>(my2) };
        glm::vec2 world_pos  = (mouse_pos2 - glm::vec2(1280.0f, 720.0f)) / cfg.current_camera_zoom + cfg.camera_origin;

        // Use cached positions if available, otherwise do a one-shot readback
        if (cached_positions_.size() == cfg.particle_count) {
            // Find nearest particle within 50 world units
            float best_dist = 50.0f * 50.0f;
            int best_idx = -1;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                float dx = cached_positions_[i].x - world_pos.x;
                float dy = cached_positions_[i].y - world_pos.y;
                float d = dx * dx + dy * dy;
                if (d < best_dist) {
                    best_dist = d;
                    best_idx = int(i);
                }
            }
            iface.selected_particle = best_idx;
            if (best_idx >= 0) {
                iface.selected_particle_pos = cached_positions_[best_idx];
                iface.selected_particle_type = particles.types[best_idx];
                iface.selected_particle_energy = particles.energy[best_idx];
                iface.selected_particle_age = particles.stats[best_idx].spawn_time;
                iface.selected_particle_organism = particles.stats[best_idx].current_organism_id;
            }
        }
    }
    rmb_prev = rmb_cur;
}

// ── Async HTTP helpers (non-blocking via std::async) ──────────────────────────

bool Simulation::http_fetch_sync(const std::string& url, std::string& result) {
    std::string cmd = "curl -s --max-time 5 \"" + url + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    int ret = pclose(pipe);
    return ret == 0 && !result.empty();
}

void Simulation::request_weather_fetch() {
    if (!geolocation_fetched_) return; // wait for ZIP/geo resolve
    std::string url = "https://api.open-meteo.com/v1/forecast?"
        "latitude=" + std::to_string(latitude_) +
        "&longitude=" + std::to_string(longitude_) +
        "&current=temperature_2m,weather_code,precipitation,wind_speed_10m,wind_direction_10m,cloud_cover";
    http_action_ = HttpAction::WEATHER;
    http_future_ = std::async(std::launch::async, [url]() {
        std::string result;
        http_fetch_sync(url, result);
        return result;
    });
}

void Simulation::request_zip_resolve(const std::string& zip) {
    pending_zip_ = zip;
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + zip + "&count=1&language=en&format=json";
    http_action_ = HttpAction::ZIP_RESOLVE;
    http_future_ = std::async(std::launch::async, [url]() {
        std::string result;
        http_fetch_sync(url, result);
        return result;
    });
}

void Simulation::process_http_response() {
    if (!http_future_.valid()) return;
    if (http_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    std::string json = http_future_.get();
    HttpAction done_action = http_action_;
    http_action_ = HttpAction::NONE;

    if (json.empty()) return;

    if (done_action == HttpAction::WEATHER) {
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
        weather_.location_name = location_name_;
        weather_fetched_ = true;
    } else if (done_action == HttpAction::ZIP_RESOLVE) {
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
        geolocation_fetched_ = true;
    }
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

// ── Perlin noise helpers ───────────────────────────────────────────────────────

static float noise2d(int x, int y, int seed) {
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    return (float)(1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
}

static float smooth_noise(float x, float y, int seed) {
    int ix = (int)floor(x);
    int iy = (int)floor(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float v00 = noise2d(ix, iy, seed);
    float v10 = noise2d(ix + 1, iy, seed);
    float v01 = noise2d(ix, iy + 1, seed);
    float v11 = noise2d(ix + 1, iy + 1, seed);
    float v0 = v00 + (v10 - v00) * fx;
    float v1 = v01 + (v11 - v01) * fx;
    return v0 + (v1 - v0) * fy;
}

static float fbm(float x, float y, int octaves, int seed) {
    float value = 0.0f, amp = 1.0f, freq = 1.0f, max_val = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        value += amp * smooth_noise(x * freq, y * freq, seed + i * 100);
        max_val += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return value / max_val;
}

void Simulation::generate_terrain() {
    std::vector<float> grid(CHEM_W * CHEM_H, 0.0f);

    // Perlin noise obstacles: threshold-based terrain
    for (uint32_t y = 0; y < CHEM_H; ++y) {
        for (uint32_t x = 0; x < CHEM_W; ++x) {
            float nx = (float)x / (float)CHEM_W * 4.0f;
            float ny = (float)y / (float)CHEM_H * 4.0f;
            float noise = fbm(nx, ny, 4, 42);

            // Create terrain features at different noise thresholds
            if (noise > 0.65f) {
                grid[x + y * CHEM_W] = 1.0f; // solid obstacle
            } else if (noise > 0.55f) {
                grid[x + y * CHEM_W] = 0.5f + (noise - 0.55f) * 5.0f; // gradient obstacle
            }
        }
    }

    // Add some circular clearings for visual interest
    std::mt19937 rng(123);
    for (int o = 0; o < 8; ++o) {
        float cx = (float)(rng() % CHEM_W);
        float cy = (float)(rng() % CHEM_H);
        float r = 15.0f + (float)(rng() % 15);
        for (uint32_t y = 0; y < CHEM_H; ++y)
            for (uint32_t x = 0; x < CHEM_W; ++x) {
                float d = sqrt((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (d < r) grid[x + y * CHEM_W] = 0.0f; // carve clearing
            }
    }

    compute.upload_terrain(vk, grid.data());

    // Count obstacles for shader early-out
    cfg.terrain_obstacle_count = 0;
    for (auto v : grid)
        if (v > 0.1f) cfg.terrain_obstacle_count++;
}

void Simulation::spawn_seasonal_food() {
    if (!compute.is_ready()) return;
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&now_t);
    float season_t = (float)(tm->tm_yday % 365) / 365.0f;
    float angle = season_t * 2.0f * 3.14159f;
    float zone_cx = 5000.0f * cos(angle);
    float zone_cy = 3000.0f * sin(angle);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> clump_x(zone_cx - 1000.0f, zone_cx + 1000.0f);
    std::uniform_real_distribution<float> clump_y(zone_cy - 1000.0f, zone_cy + 1000.0f);
    std::uniform_real_distribution<float> jitter(-100.0f, 100.0f);

    const uint32_t n = cfg.particle_count;
    vkQueueWaitIdle(vk.queue);
    compute.read_current_state(vk, particles.positions, particles.velocities, particles.types);
    int food_count = 30;
    uint32_t new_total = n + food_count;
    particles.positions.resize(new_total);
    particles.velocities.resize(new_total);
    particles.types.resize(new_total);
    particles.energy.resize(new_total, 1.0f);
    particles.angles.resize(new_total, 0.0f);
    particles.angular_velocities.resize(new_total, 0.0f);
    particles.stats.resize(new_total);
    for (int i = 0; i < food_count; ++i) {
        int idx = n + i;
        particles.positions[idx] = glm::vec2(clump_x(rng) + jitter(rng), clump_y(rng) + jitter(rng));
        particles.velocities[idx] = glm::vec2(0.0f);
        particles.types[idx] = FOOD_TYPE_INDEX;
        particles.energy[idx] = 1.0f;
    }
    cfg.particle_count = new_total;
    if (new_total > compute.capacity())
        compute.resize_buffers(vk, particles);
    else
        compute.upload_particle_range(vk, particles, n, food_count);
}

void Simulation::update_seasonal_migration() {
    // Migrator types seek their preferred temperature band by adjusting
    // their movement bias toward warmer/colder regions.
    // Temperature roughly maps to latitude (y-axis).
    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        if (i >= readback_types_.size()) break;
        uint32_t t = readback_types_[i];
        if (t >= MAX_PARTICLE_TYPES) continue;
        uint32_t flags = particles.behavior_flags[t];
        if ((flags & BEHAVIOR_MIGRATOR) == 0) continue;

        float world_y = readback_positions_[i].y;
        float half_h = float(REGION_H) * 1.5f;

        // Preferred temperature maps to preferred y-position
        // Type 0 = cold-loving (north), Type 8 = heat-loving (south)
        float preferred_band = ((float)t / 8.0f) * 2.0f - 1.0f; // -1 to 1
        float target_y = preferred_band * half_h;

        // Apply gentle nudging force proportional to distance from preferred band
        float diff = target_y - world_y;
        float force = diff * 0.0001f;

        // Only apply if actual temperature differs
        float temp = cfg.current_temperature;
        if (temp > 25.0f && preferred_band < -0.3f) force *= 2.0f; // hot -> go north
        if (temp < 10.0f && preferred_band > 0.3f) force *= 2.0f;  // cold -> go south

        readback_positions_[i].y += force;
    }
}

void Simulation::update_niche_construction() {
    niche_tick_counter_++;
    if (niche_tick_counter_ % 1200 != 0) return;

    std::vector<float> terrain(CHEM_W * CHEM_H, 0.0f);

    // Track particle density per chem cell
    std::vector<uint32_t> cell_count(CHEM_W * CHEM_H, 0);
    for (auto& pos : readback_positions_) {
        float tx = (pos.x / (float(REGION_W) * 3.0f) + 0.5f) * float(CHEM_W);
        float ty = (pos.y / (float(REGION_H) * 3.0f) + 0.5f) * float(CHEM_H);
        uint32_t ux = std::min((uint32_t)std::max(0.0f, std::floor(tx)), CHEM_W - 1u);
        uint32_t uy = std::min((uint32_t)std::max(0.0f, std::floor(ty)), CHEM_H - 1u);
        cell_count[ux + uy * CHEM_W]++;
    }

    for (uint32_t i = 0; i < CHEM_W * CHEM_H; ++i) {
        if (cell_count[i] > 50) {
            terrain[i] = std::min(terrain[i] + 0.1f, 1.0f);
        } else if (cell_count[i] > 20 && cell_count[i] < 50) {
            terrain[i] = std::max(terrain[i] - 0.05f, 0.0f);
        }
    }

    compute.upload_terrain(vk, terrain.data());
}

void Simulation::save_screenshot() {
    if (!compute.is_ready()) return;
    vkQueueWaitIdle(vk.queue);

    std::vector<float> pixels;
    compute.readback_particle_texture(vk, pixels);
    if (pixels.empty()) return;

    // Convert RGBA32F → RGB8 and write PPM
    static int shot_count = 0;
    shot_count++;
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char fname[64];
    std::strftime(fname, sizeof(fname), "screenshot_%Y%m%d_%H%M%S.ppm", std::localtime(&t));

    std::ofstream f(fname, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to open " << fname << " for writing\n";
        return;
    }

    f << "P6\n" << REGION_W << " " << REGION_H << "\n255\n";
    const float* src = pixels.data();
    size_t num_pixels = REGION_W * REGION_H;
    for (size_t i = 0; i < num_pixels; ++i) {
        // RGBA32F → RGB8 with gamma correction
        auto to_byte = [](float v) -> uint8_t {
            v = std::clamp(v, 0.0f, 1.0f);
            v = std::pow(v, 1.0f / 2.2f); // gamma
            return uint8_t(v * 255.0f + 0.5f);
        };
        uint8_t rgb[3] = { to_byte(src[0]), to_byte(src[1]), to_byte(src[2]) };
        f.write(reinterpret_cast<const char*>(rgb), 3);
        src += 4;
    }

    std::cout << "Screenshot saved: " << fname << " (" << (num_pixels * 3) / (1024*1024) << " MB)\n";
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
