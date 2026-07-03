#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include <vulkan/vulkan.h>
#include <string>

// Mirrors pipeline.gd.
// Owns the compute pipeline, double-buffered particle storage buffers,
// the particle render texture, and the descriptor sets.

class ComputePipeline {
public:
    // Particle splat target (HDR, full resolution). Read by post chain.
    Image         particle_texture{};
    VkImageView   particle_texture_view = VK_NULL_HANDLE; // same as particle_texture.view
    VkSampler     sampler               = VK_NULL_HANDLE; // linear, used by quad pipeline

    // Post-processing intermediate images.
    Image         bloom_lo{};       // ½ res bright-pass + h-blur target
    Image         bloom_blur{};     // ½ res v-blur target
    Image         composite_tex{};  // full-res final image (read by fullscreen quad)

    int tick = 0;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    void init(VulkanContext& ctx, const std::string& shader_spv_path);
    void destroy(VulkanContext& ctx);

    // Create / destroy particle buffers (called on reset)
    void create_buffers(VulkanContext& ctx, const Particles& particles);
    void clear_buffers(VulkanContext& ctx);

    // Record all compute dispatches for one simulation frame:
    // grid → physics → particle splat → bright-pass → blur-h → blur-v → composite
    void record(VkCommandBuffer cmd,
                const SimConfig& cfg,
                float dt,
                uint32_t halo_count,
                float    time_seconds,
                float    day_night_factor,
                glm::vec2 wind = glm::vec2(0.0f),
                uint32_t extra_effect_flags = 0u);

    // Upload force + color arrays (called each frame before record())
    void upload_dynamic_data(VulkanContext& ctx,
                             const Particles& particles);

    // Upload organism halo array (called each frame after OrganismManager update)
    void upload_halos(VulkanContext& ctx,
                      const OrganismHaloGPU* halos,
                      uint32_t count);

    void upload_energy_modifiers(VulkanContext& ctx,
                                 const std::vector<float>& modifiers);

    void upload_terrain(VulkanContext& ctx, const float* data);

    void upload_colors(VulkanContext& ctx, const Particles& particles);

    // Read back the particle texture to CPU-visible memory.
    // Call after vkQueueWaitIdle. Returns RGBA32F pixel data.
    void readback_particle_texture(VulkanContext& ctx, std::vector<float>& out_pixels);

    bool is_ready() const { return pos_buffer_a_.handle != VK_NULL_HANDLE; }

    // Read current particle positions and velocities back to CPU.
    // Safe to call after end_single_command() (queue is idle).
    void read_current_state(VulkanContext& ctx,
                            std::vector<glm::vec2>& out_positions,
                            std::vector<glm::vec2>& out_velocities,
                            std::vector<uint32_t>& out_types) const;

    // Resizes buffers and descriptor sets when particle count changes
    void resize_buffers(VulkanContext& ctx, const Particles& particles);

    // Upload a range of new particle data to existing GPU buffers (within capacity).
    // start = first index, count = number of particles to upload.
    void upload_particle_range(VulkanContext& ctx, const Particles& particles,
                                uint32_t start, uint32_t count);

    // Returns the per-element capacity of GPU buffers
    uint32_t capacity() const { return buffer_capacity_; }

private:
    VkPipeline            pipeline_             = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_      = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_      = VK_NULL_HANDLE;
    VkDescriptorPool      desc_pool_            = VK_NULL_HANDLE;

    // Double-buffered particle position + velocity buffers
    Buffer pos_buffer_a_{};
    Buffer pos_buffer_b_{};
    Buffer vel_buffer_a_{};
    Buffer vel_buffer_b_{};
    Buffer type_buffer_{};
    Buffer force_buffer_{};
    Buffer color_buffer_{};

    // Behavior flags (shared, static per reset)
    Buffer behavior_buffer_{};

    // Double-buffered polar orientation (angle + angular velocity)
    Buffer angle_buffer_a_{};
    Buffer angle_buffer_b_{};
    Buffer angular_vel_buffer_a_{};
    Buffer angular_vel_buffer_b_{};

    Buffer energy_buffer_a_{};
    Buffer energy_buffer_b_{};
    Buffer energy_mod_buffer_{};

    // Genome buffers (double-buffered, per-particle age/lifespan/traits)
    Buffer genome_buffer_a_{};
    Buffer genome_buffer_b_{};

    // Chemical signal grid + terrain grid
    Buffer signal_grid_buffer_{};
    Buffer terrain_grid_buffer_{};

    // Memory map (per-particle persistent signal memory, double-buffered)
    Buffer memory_map_buffer_a_{};
    Buffer memory_map_buffer_b_{};

    // Spatial Grid buffers
    Buffer grid_offsets_buffer_{};
    Buffer grid_counts_buffer_{};
    Buffer sorted_indices_buffer_{};
    Buffer conversion_buffer_{};

    // Organism halo data (uploaded each frame from OrganismManager).
    Buffer halo_buffer_{};

    // Screenshot readback buffer
    Buffer screenshot_readback_buffer_{};
    bool   screenshot_readback_created_ = false;

    // Descriptor sets: set_a uses (a→in, b→out), set_b uses (b→in, a→out)
    VkDescriptorSet desc_set_a_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_b_ = VK_NULL_HANDLE;

    // Per-element capacity of GPU buffers (may exceed cfg.particle_count)
    uint32_t buffer_capacity_ = 0;

    // Bindings:
    // 0: in pos, 1: in vel, 2: types, 3: forces, 4: colors,
    // 5: out pos, 6: out vel, 7: particle render texture, 8: behaviors,
    // 9: in angle, 10: in avel, 11: out angle, 12: out avel,
    // 13: in energy, 14: out energy,
    // 15: grid offsets, 16: sorted indices, 17: conversion matrix,
    // 18: bloom_lo (storage image), 19: bloom_blur (storage image),
    // 20: composite_tex (storage image), 21: halo buffer, 22: energy modifiers,
    // 23: genome in, 24: genome out, 25: signal grid, 26: terrain grid,
    // 27: memory map in, 28: memory map out
    static constexpr uint32_t NUM_BINDINGS = 29;

    void create_descriptor_set_layout(VkDevice device);
    void create_pipeline_layout(VkDevice device);
    void create_compute_pipeline(VulkanContext& ctx,
                                 const std::string& shader_spv_path);
    void create_descriptor_pool(VkDevice device);
    void allocate_and_write_descriptor_sets(VulkanContext& ctx);

    void dispatch(VkCommandBuffer cmd,
                  VkDescriptorSet dset,
                  const PushConstants& pc,
                  uint32_t particle_count);
};
