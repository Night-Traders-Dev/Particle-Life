#include "simulation.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
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


    // ── Upload dynamic GPU data ────────────────────────────────────────────────
    if (is_active)
        compute.upload_dynamic_data(vk, particles);

    // ── ImGui ──────────────────────────────────────────────────────────────────
    bool request_reset = false;
    uint32_t prev_count = cfg.particle_count;
    iface.render_imgui(cfg, particles, organism_manager, request_reset);
    time_scale_ = iface.time_scale_slider;

    if (cfg.particle_count != prev_count) {
        particles.gen_data(cfg);
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

        float scaled_dt = static_cast<float>(dt) * 5.0f * time_scale_;
compute.record(compute_cmd, cfg, scaled_dt, 0, 0.0f);
        vk.end_single_command(compute_cmd);

        // Organism detection (every N frames)
        organism_tick_counter_++;
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

    renderer.draw_frame(vk, compute, is_active, cfg, particles, organism_manager);
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
        glm::vec2 world_pos  = (mouse_pos2 - glm::vec2(REGION_W/2, REGION_H/2)) / cfg.current_camera_zoom + cfg.camera_origin;

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
