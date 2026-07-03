#include "compute_pipeline.h"
#include <stdexcept>
#include <cstring>
#include <array>

// ── Init / Destroy ────────────────────────────────────────────────────────────

void ComputePipeline::init(VulkanContext& ctx, const std::string& shader_spv_path) {
    const VkFormat HDR_FMT = VK_FORMAT_R32G32B32A32_SFLOAT;

    auto make_storage_img = [&](Image& img, uint32_t w, uint32_t h, VkImageUsageFlags extra = 0) {
        img = ctx.create_image(
            w, h, HDR_FMT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | extra,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        img.view = ctx.create_image_view(img.handle, HDR_FMT, VK_IMAGE_ASPECT_COLOR_BIT);
        ctx.transition_image_layout(img.handle,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    };

    make_storage_img(particle_texture, REGION_W, REGION_H, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    make_storage_img(bloom_lo,         BLOOM_W,  BLOOM_H);
    make_storage_img(bloom_blur,       BLOOM_W,  BLOOM_H);
    make_storage_img(composite_tex,    REGION_W, REGION_H);

    particle_texture_view = particle_texture.view;
    sampler = ctx.create_sampler_linear();

    create_descriptor_set_layout(ctx.device);
    create_pipeline_layout(ctx.device);
    create_compute_pipeline(ctx, shader_spv_path);
    create_descriptor_pool(ctx.device);
}

void ComputePipeline::destroy(VulkanContext& ctx) {
    clear_buffers(ctx);

    if (desc_pool_       != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, desc_pool_, nullptr);
    if (pipeline_        != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, pipeline_layout_, nullptr);
    if (desc_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, desc_set_layout_, nullptr);
    if (sampler          != VK_NULL_HANDLE) vkDestroySampler(ctx.device, sampler, nullptr);
    ctx.destroy_image(particle_texture);
    ctx.destroy_image(bloom_lo);
    ctx.destroy_image(bloom_blur);
    ctx.destroy_image(composite_tex);
}

// ── Descriptor set layout ─────────────────────────────────────────────────────
// Bindings 0-6: particle storage buffers
// Binding  7:   storage image (render texture)
// Binding  8:   behavior flags (shared, static per type)
// Bindings 9-12: polar angle/angular-velocity (double-buffered)
// Bindings 13-14: energy (double-buffered)
// Binding 15: grid offsets (prefix sum start indices)
// Binding 16: sorted particle indices

void ComputePipeline::create_descriptor_set_layout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, NUM_BINDINGS> bindings{};

    // Default everything to STORAGE_BUFFER.
    for (uint32_t i = 0; i < NUM_BINDINGS; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    // Storage-image bindings: 7 (particle splat), 18 (bloom_lo), 19 (bloom_blur), 20 (composite).
    for (uint32_t i : { 7u, 18u, 19u, 20u }) {
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    // 21 is the halo SSBO — already STORAGE_BUFFER by default.

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &desc_set_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor set layout");
}

// ── Pipeline layout (includes push constants) ─────────────────────────────────

void ComputePipeline::create_pipeline_layout(VkDevice device) {
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo ci{};
    ci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount         = 1;
    ci.pSetLayouts            = &desc_set_layout_;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges    = &pc_range;

    if (vkCreatePipelineLayout(device, &ci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline layout");
}

// ── Compute pipeline ──────────────────────────────────────────────────────────

void ComputePipeline::create_compute_pipeline(VulkanContext& ctx,
                                              const std::string& shader_spv_path)
{
    VkShaderModule module = ctx.create_shader_module(shader_spv_path);

    VkPipelineShaderStageCreateInfo stage_ci{};
    stage_ci.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_ci.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = module;
    stage_ci.pName  = "main";

    VkComputePipelineCreateInfo ci{};
    ci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage  = stage_ci;
    ci.layout = pipeline_layout_;

    if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute pipeline");

    vkDestroyShaderModule(ctx.device, module, nullptr);
}

// ── Descriptor pool ───────────────────────────────────────────────────────────

void ComputePipeline::create_descriptor_pool(VkDevice device) {
    // 30 bindings total per set, 2 sets:
    //   storage buffers: bindings 0-6, 8-17, 21-29 -> 25 per set
    //   storage images : bindings 7, 18, 19, 20  ->  4 per set
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 25 * 2 };
    pool_sizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   4 * 2 };

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = 2;
    ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    ci.pPoolSizes    = pool_sizes.data();

    if (vkCreateDescriptorPool(device, &ci, nullptr, &desc_pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create compute descriptor pool");
}

// ── Buffer creation ───────────────────────────────────────────────────────────

void ComputePipeline::create_buffers(VulkanContext& ctx, const Particles& particles) {
    const VkBufferUsageFlags    BUF_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags MEM_PROPS =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Compute per-element capacity (power of 2, minimum 32768) so small
    // particle additions don't trigger a full buffer recreation.
    uint32_t count = static_cast<uint32_t>(particles.positions.size());
    uint32_t cap = 32768;
    while (cap < count) cap *= 2;
    buffer_capacity_ = cap;

    VkDeviceSize elem_capacity = static_cast<VkDeviceSize>(cap);
    VkDeviceSize pos_size      = elem_capacity * sizeof(glm::vec2);
    VkDeviceSize vel_size      = elem_capacity * sizeof(glm::vec2);
    VkDeviceSize type_size     = elem_capacity * sizeof(uint32_t);
    VkDeviceSize index_size    = elem_capacity * sizeof(uint32_t);
    VkDeviceSize energy_mod_size = elem_capacity * sizeof(float);
    VkDeviceSize genome_size     = elem_capacity * sizeof(GenomeData);

    VkDeviceSize actual_count  = static_cast<VkDeviceSize>(count);
    VkDeviceSize pos_upload    = actual_count * sizeof(glm::vec2);
    VkDeviceSize vel_upload    = actual_count * sizeof(glm::vec2);
    VkDeviceSize type_upload   = actual_count * sizeof(uint32_t);
    VkDeviceSize emod_upload   = actual_count * sizeof(float);
    VkDeviceSize genome_upload = actual_count * sizeof(GenomeData);

    VkDeviceSize force_size    = particles.forces.size()      * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()      * sizeof(glm::vec4);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES            * sizeof(uint32_t);
    VkDeviceSize angle_size    = elem_capacity * sizeof(float);
    VkDeviceSize energy_size   = elem_capacity * sizeof(float);

    VkDeviceSize angle_upload  = actual_count * sizeof(float);
    VkDeviceSize energy_upload = actual_count * sizeof(float);
    VkDeviceSize grid_size     = GRID_SIZE                     * sizeof(uint32_t);
    VkDeviceSize conv_size     = MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(ConversionData);
    VkDeviceSize halo_size     = MAX_HALOS * sizeof(OrganismHaloGPU);
    VkDeviceSize chem_grid_size  = CHEM_W * CHEM_H * sizeof(float);
    VkDeviceSize oid_size      = elem_capacity * sizeof(uint32_t);

    pos_buffer_a_          = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);
    pos_buffer_b_          = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);
    vel_buffer_a_          = ctx.create_buffer(vel_size,      BUF_USAGE, MEM_PROPS);
    vel_buffer_b_          = ctx.create_buffer(vel_size,      BUF_USAGE, MEM_PROPS);
    type_buffer_           = ctx.create_buffer(type_size,     BUF_USAGE, MEM_PROPS);
    force_buffer_          = ctx.create_buffer(force_size,    BUF_USAGE, MEM_PROPS);
    color_buffer_          = ctx.create_buffer(color_size,    BUF_USAGE, MEM_PROPS);
    behavior_buffer_       = ctx.create_buffer(behavior_size, BUF_USAGE, MEM_PROPS);
    angle_buffer_a_        = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angle_buffer_b_        = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angular_vel_buffer_a_  = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    angular_vel_buffer_b_  = ctx.create_buffer(angle_size,    BUF_USAGE, MEM_PROPS);
    energy_buffer_a_       = ctx.create_buffer(energy_size,   BUF_USAGE, MEM_PROPS);
    energy_buffer_b_       = ctx.create_buffer(energy_size,   BUF_USAGE, MEM_PROPS);
    energy_mod_buffer_     = ctx.create_buffer(energy_mod_size, BUF_USAGE, MEM_PROPS);
    grid_offsets_buffer_   = ctx.create_buffer(grid_size,     BUF_USAGE, MEM_PROPS);
    grid_counts_buffer_    = ctx.create_buffer(grid_size,     BUF_USAGE, MEM_PROPS);
    sorted_indices_buffer_ = ctx.create_buffer(index_size,    BUF_USAGE, MEM_PROPS);
    conversion_buffer_     = ctx.create_buffer(conv_size,     BUF_USAGE, MEM_PROPS);
    halo_buffer_           = ctx.create_buffer(halo_size,     BUF_USAGE, MEM_PROPS);
    genome_buffer_a_       = ctx.create_buffer(genome_size,  BUF_USAGE, MEM_PROPS);
    genome_buffer_b_       = ctx.create_buffer(genome_size,  BUF_USAGE, MEM_PROPS);
    signal_grid_buffer_    = ctx.create_buffer(chem_grid_size, BUF_USAGE, MEM_PROPS);
    terrain_grid_buffer_   = ctx.create_buffer(chem_grid_size, BUF_USAGE, MEM_PROPS);
    organism_id_buffer_    = ctx.create_buffer(oid_size,      BUF_USAGE, MEM_PROPS);
    memory_map_buffer_a_   = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);
    memory_map_buffer_b_   = ctx.create_buffer(pos_size,      BUF_USAGE, MEM_PROPS);

    // Screenshot readback buffer (RGBA32F → 4 bytes per channel × 4 channels)
    {
        VkDeviceSize sbuf_size = REGION_W * REGION_H * 4 * sizeof(float);
        screenshot_readback_buffer_ = ctx.create_buffer(sbuf_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        screenshot_readback_created_ = true;
    }

    // Upload initial data (only the actual count, not the full capacity)
    ctx.update_buffer(pos_buffer_a_,  particles.positions.data(),        pos_upload);
    ctx.update_buffer(pos_buffer_b_,  particles.positions.data(),        pos_upload);
    ctx.update_buffer(vel_buffer_a_,  particles.velocities.data(),       vel_upload);
    ctx.update_buffer(vel_buffer_b_,  particles.velocities.data(),       vel_upload);
    ctx.update_buffer(type_buffer_,   particles.types.data(),            type_upload);
    ctx.update_buffer(force_buffer_,  particles.forces.data(),           force_size);
    ctx.update_buffer(color_buffer_,  particles.colors.data(),           color_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,        behavior_size);
    ctx.update_buffer(angle_buffer_a_,  particles.angles.data(),         angle_upload);
    ctx.update_buffer(angle_buffer_b_,  particles.angles.data(),         angle_upload);
    ctx.update_buffer(angular_vel_buffer_a_, particles.angular_velocities.data(), angle_upload);
    ctx.update_buffer(angular_vel_buffer_b_, particles.angular_velocities.data(), angle_upload);
    ctx.update_buffer(energy_buffer_a_, particles.energy.data(),         energy_upload);
    ctx.update_buffer(energy_buffer_b_, particles.energy.data(),         energy_upload);
    ctx.update_buffer(conversion_buffer_, particles.conversion_matrix,   conv_size);

    std::vector<float> energy_mods(static_cast<size_t>(actual_count), 1.0f);
    ctx.update_buffer(energy_mod_buffer_, energy_mods.data(), emod_upload);

    // Halo buffer initialised to zeros (no halos until OrganismManager populates it).
    std::vector<OrganismHaloGPU> empty_halos(MAX_HALOS, OrganismHaloGPU{});
    ctx.update_buffer(halo_buffer_, empty_halos.data(), halo_size);

    // Genome buffers (initialised with default values)
    std::vector<GenomeData> genomes(static_cast<size_t>(actual_count));
    for (size_t i = 0; i < static_cast<size_t>(actual_count); ++i) {
        genomes[i].age       = 0.0f;
        genomes[i].lifespan  = DEFAULT_LIFESPAN;
        genomes[i].self_mod  = 1.0f;
        genomes[i].cross_mod = 1.0f;
        genomes[i].generation = 0u;
    }
    ctx.update_buffer(genome_buffer_a_, genomes.data(), genome_upload);
    ctx.update_buffer(genome_buffer_b_, genomes.data(), genome_upload);

    // Signal grid: zeroed
    std::vector<float> zero_chem(CHEM_W * CHEM_H, 0.0f);
    ctx.update_buffer(signal_grid_buffer_,  zero_chem.data(), chem_grid_size);
    ctx.update_buffer(terrain_grid_buffer_, zero_chem.data(), chem_grid_size);

    // Organism IDs: zeroed (all free particles)
    std::vector<uint32_t> zero_oid(static_cast<size_t>(actual_count), 0u);
    ctx.update_buffer(organism_id_buffer_, zero_oid.data(), oid_size);

    // Memory map: zeroed (vec2 per particle)
    std::vector<float> zero_mem(static_cast<size_t>(elem_capacity * 2), 0.0f);
    ctx.update_buffer(memory_map_buffer_a_, zero_mem.data(), elem_capacity * 2 * sizeof(float));
    ctx.update_buffer(memory_map_buffer_b_, zero_mem.data(), elem_capacity * 2 * sizeof(float));

    allocate_and_write_descriptor_sets(ctx);

    tick = 0;
}

void ComputePipeline::clear_buffers(VulkanContext& ctx) {
    vkDeviceWaitIdle(ctx.device);

    buffer_capacity_ = 0;

    // Free descriptor sets by resetting the pool
    if (desc_pool_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(ctx.device, desc_pool_, 0);
        desc_set_a_ = VK_NULL_HANDLE;
        desc_set_b_ = VK_NULL_HANDLE;
    }

    ctx.destroy_buffer(pos_buffer_a_);
    ctx.destroy_buffer(pos_buffer_b_);
    ctx.destroy_buffer(vel_buffer_a_);
    ctx.destroy_buffer(vel_buffer_b_);
    ctx.destroy_buffer(type_buffer_);
    ctx.destroy_buffer(force_buffer_);
    ctx.destroy_buffer(color_buffer_);
    ctx.destroy_buffer(behavior_buffer_);
    ctx.destroy_buffer(angle_buffer_a_);
    ctx.destroy_buffer(angle_buffer_b_);
    ctx.destroy_buffer(angular_vel_buffer_a_);
    ctx.destroy_buffer(angular_vel_buffer_b_);
    ctx.destroy_buffer(energy_buffer_a_);
    ctx.destroy_buffer(energy_buffer_b_);
    ctx.destroy_buffer(energy_mod_buffer_);
    ctx.destroy_buffer(grid_offsets_buffer_);
    ctx.destroy_buffer(grid_counts_buffer_);
    ctx.destroy_buffer(sorted_indices_buffer_);
    ctx.destroy_buffer(conversion_buffer_);
    ctx.destroy_buffer(halo_buffer_);
    ctx.destroy_buffer(genome_buffer_a_);
    ctx.destroy_buffer(genome_buffer_b_);
    ctx.destroy_buffer(signal_grid_buffer_);
    ctx.destroy_buffer(terrain_grid_buffer_);
    ctx.destroy_buffer(organism_id_buffer_);
    ctx.destroy_buffer(memory_map_buffer_a_);
    ctx.destroy_buffer(memory_map_buffer_b_);
    ctx.destroy_buffer(screenshot_readback_buffer_);
    screenshot_readback_created_ = false;
}

// ── Write descriptor sets ─────────────────────────────────────────────────────

static void write_storage_buffer(std::vector<VkWriteDescriptorSet>& writes,
                                  std::vector<VkDescriptorBufferInfo>& buf_infos,
                                  VkDescriptorSet set, uint32_t binding,
                                  VkBuffer buf, VkDeviceSize size)
{
    buf_infos.push_back({ buf, 0, size });
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo     = &buf_infos.back();
    writes.push_back(w);
}

static void write_storage_image(std::vector<VkWriteDescriptorSet>& writes,
                                 std::vector<VkDescriptorImageInfo>& img_infos,
                                 VkDescriptorSet set, uint32_t binding,
                                 VkImageView view)
{
    img_infos.push_back({ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL });
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo      = &img_infos.back();
    writes.push_back(w);
}

void ComputePipeline::allocate_and_write_descriptor_sets(VulkanContext& ctx) {
    std::array<VkDescriptorSetLayout, 2> layouts = { desc_set_layout_, desc_set_layout_ };
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = desc_pool_;
    alloc.descriptorSetCount = 2;
    alloc.pSetLayouts        = layouts.data();

    std::array<VkDescriptorSet, 2> sets{};
    if (vkAllocateDescriptorSets(ctx.device, &alloc, sets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate compute descriptor sets");

    desc_set_a_ = sets[0];
    desc_set_b_ = sets[1];

    // 2 sets × 22 bindings = 44 writes; 2 sets × 18 buffers = 36 buf infos;
    // 2 sets × 4 storage images = 8 img infos.
    std::vector<VkWriteDescriptorSet>    writes;
    std::vector<VkDescriptorBufferInfo>  buf_infos;
    std::vector<VkDescriptorImageInfo>   img_infos;
    writes.reserve(64);
    buf_infos.reserve(64);   // generous; vectors must NOT reallocate (pointer stability)
    img_infos.reserve(16);

    auto pos_sz  = pos_buffer_a_.size;
    auto vel_sz  = vel_buffer_a_.size;
    auto type_sz = type_buffer_.size;
    auto frc_sz  = force_buffer_.size;
    auto col_sz  = color_buffer_.size;
    auto beh_sz  = behavior_buffer_.size;
    auto ang_sz  = angle_buffer_a_.size;
    auto nrg_sz  = energy_buffer_a_.size;
    auto grd_sz  = grid_offsets_buffer_.size;
    auto idx_sz  = sorted_indices_buffer_.size;
    auto cnv_sz  = conversion_buffer_.size;
    auto halo_sz = halo_buffer_.size;
    auto emod_sz = energy_mod_buffer_.size;
    auto genome_sz = genome_buffer_a_.size;
    auto signal_sz = signal_grid_buffer_.size;
    auto terrain_sz = terrain_grid_buffer_.size;
    auto mem_sz = memory_map_buffer_a_.size;
    auto oid_sz = organism_id_buffer_.size;

    // ── Set A: in=a, out=b ────────────────────────────────────────────────────
    write_storage_buffer(writes, buf_infos, desc_set_a_,  0, pos_buffer_a_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  1, vel_buffer_a_.handle,         vel_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  2, type_buffer_.handle,          type_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  3, force_buffer_.handle,         frc_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  4, color_buffer_.handle,         col_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  5, pos_buffer_b_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  6, vel_buffer_b_.handle,         vel_sz);
    write_storage_image (writes, img_infos, desc_set_a_,  7, particle_texture.view);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  8, behavior_buffer_.handle,      beh_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_,  9, angle_buffer_a_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 10, angular_vel_buffer_a_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 11, angle_buffer_b_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 12, angular_vel_buffer_b_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 13, energy_buffer_a_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 14, energy_buffer_b_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 15, grid_offsets_buffer_.handle,  grd_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 16, sorted_indices_buffer_.handle, idx_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 17, conversion_buffer_.handle,     cnv_sz);
    write_storage_image (writes, img_infos, desc_set_a_, 18, bloom_lo.view);
    write_storage_image (writes, img_infos, desc_set_a_, 19, bloom_blur.view);
    write_storage_image (writes, img_infos, desc_set_a_, 20, composite_tex.view);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 21, halo_buffer_.handle,           halo_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 22, energy_mod_buffer_.handle,     emod_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 23, genome_buffer_a_.handle,       genome_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 24, genome_buffer_b_.handle,       genome_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 25, signal_grid_buffer_.handle,    signal_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 26, terrain_grid_buffer_.handle,   terrain_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 27, memory_map_buffer_a_.handle,   mem_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 28, memory_map_buffer_b_.handle,   mem_sz);
    write_storage_buffer(writes, buf_infos, desc_set_a_, 29, organism_id_buffer_.handle,    oid_sz);

    // ── Set B: in=b, out=a ────────────────────────────────────────────────────
    write_storage_buffer(writes, buf_infos, desc_set_b_,  0, pos_buffer_b_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  1, vel_buffer_b_.handle,         vel_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  2, type_buffer_.handle,          type_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  3, force_buffer_.handle,         frc_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  4, color_buffer_.handle,         col_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  5, pos_buffer_a_.handle,         pos_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  6, vel_buffer_a_.handle,         vel_sz);
    write_storage_image (writes, img_infos, desc_set_b_,  7, particle_texture.view);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  8, behavior_buffer_.handle,      beh_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_,  9, angle_buffer_b_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 10, angular_vel_buffer_b_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 11, angle_buffer_a_.handle,       ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 12, angular_vel_buffer_a_.handle, ang_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 13, energy_buffer_b_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 14, energy_buffer_a_.handle,      nrg_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 15, grid_offsets_buffer_.handle,  grd_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 16, sorted_indices_buffer_.handle, idx_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 17, conversion_buffer_.handle,     cnv_sz);
    write_storage_image (writes, img_infos, desc_set_b_, 18, bloom_lo.view);
    write_storage_image (writes, img_infos, desc_set_b_, 19, bloom_blur.view);
    write_storage_image (writes, img_infos, desc_set_b_, 20, composite_tex.view);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 21, halo_buffer_.handle,           halo_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 22, energy_mod_buffer_.handle,     emod_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 23, genome_buffer_b_.handle,       genome_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 24, genome_buffer_a_.handle,       genome_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 25, signal_grid_buffer_.handle,    signal_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 26, terrain_grid_buffer_.handle,   terrain_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 27, memory_map_buffer_b_.handle,   mem_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 28, memory_map_buffer_a_.handle,   mem_sz);
    write_storage_buffer(writes, buf_infos, desc_set_b_, 29, organism_id_buffer_.handle,    oid_sz);

    vkUpdateDescriptorSets(ctx.device,
                           static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

// ── Per-frame dynamic uploads ─────────────────────────────────────────────────

void ComputePipeline::upload_dynamic_data(VulkanContext& ctx, const Particles& particles) {
    if (force_buffer_.handle == VK_NULL_HANDLE) return;

    // Apply per-type trait scales before uploading forces.
    // The UI edits particles.forces (base values); GPU receives the scaled version.
    float effective_forces[MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES];
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b)
        for (uint32_t a = 0; a < MAX_PARTICLE_TYPES; ++a) {
            uint32_t fi = a + b * MAX_PARTICLE_TYPES;
            effective_forces[fi] = particles.forces[fi] * particles.trait_scales[a];
        }

    VkDeviceSize force_size    = particles.forces.size()     * sizeof(float);
    VkDeviceSize color_size    = particles.colors.size()     * sizeof(glm::vec4);
    VkDeviceSize behavior_size = MAX_PARTICLE_TYPES           * sizeof(uint32_t);
    VkDeviceSize conv_size     = MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(ConversionData);
    VkDeviceSize oid_size      = static_cast<VkDeviceSize>(particles.organism_ids.size()) * sizeof(uint32_t);

    ctx.update_buffer(force_buffer_,    effective_forces,              force_size);
    ctx.update_buffer(color_buffer_,    particles.colors.data(),       color_size);
    ctx.update_buffer(behavior_buffer_, particles.behavior_flags,      behavior_size);
    ctx.update_buffer(conversion_buffer_, particles.conversion_matrix, conv_size);
    if (!particles.organism_ids.empty() && organism_id_buffer_.handle != VK_NULL_HANDLE)
        ctx.update_buffer(organism_id_buffer_, particles.organism_ids.data(), oid_size);
}

void ComputePipeline::upload_halos(VulkanContext& ctx,
                                   const OrganismHaloGPU* halos,
                                   uint32_t count)
{
    if (halo_buffer_.handle == VK_NULL_HANDLE) return;
    if (count > MAX_HALOS) count = MAX_HALOS;
    if (count == 0) return;
    ctx.update_buffer(halo_buffer_, halos, count * sizeof(OrganismHaloGPU));
}

void ComputePipeline::upload_energy_modifiers(VulkanContext& ctx,
                                             const std::vector<float>& modifiers)
{
    if (energy_mod_buffer_.handle == VK_NULL_HANDLE) return;
    VkDeviceSize sz = std::min(modifiers.size() * sizeof(float), energy_mod_buffer_.size);
    ctx.update_buffer(energy_mod_buffer_, modifiers.data(), sz);
}

void ComputePipeline::upload_terrain(VulkanContext& ctx, const float* data) {
    if (terrain_grid_buffer_.handle == VK_NULL_HANDLE) return;
    ctx.update_buffer(terrain_grid_buffer_, data, CHEM_W * CHEM_H * sizeof(float));
}

void ComputePipeline::upload_colors(VulkanContext& ctx, const Particles& particles) {
    if (color_buffer_.handle == VK_NULL_HANDLE) return;
    VkDeviceSize color_size = particles.colors.size() * sizeof(glm::vec4);
    ctx.update_buffer(color_buffer_, particles.colors.data(), color_size);
}

void ComputePipeline::read_current_state(VulkanContext& ctx,
                                          std::vector<glm::vec2>& out_positions,
                                          std::vector<glm::vec2>& out_velocities,
                                          std::vector<uint32_t>& out_types) const
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    // After tick is incremented in record(), the output buffer is:
    //   tick odd  → desc_set_a was used (input=a, output=b) → read pos_buffer_b_
    //   tick even → desc_set_b was used (input=b, output=a) → read pos_buffer_a_
    const Buffer& cur_pos = (tick % 2 == 1) ? pos_buffer_b_ : pos_buffer_a_;
    const Buffer& cur_vel = (tick % 2 == 1) ? vel_buffer_b_ : vel_buffer_a_;

    uint32_t n = static_cast<uint32_t>(out_positions.size());
    VkDeviceSize bytes_vec2 = n * sizeof(glm::vec2);
    VkDeviceSize bytes_uint = n * sizeof(uint32_t);

    void* mapped = nullptr;
    vkMapMemory(ctx.device, cur_pos.memory, 0, bytes_vec2, 0, &mapped);
    std::memcpy(out_positions.data(), mapped, bytes_vec2);
    vkUnmapMemory(ctx.device, cur_pos.memory);

    vkMapMemory(ctx.device, cur_vel.memory, 0, bytes_vec2, 0, &mapped);
    std::memcpy(out_velocities.data(), mapped, bytes_vec2);
    vkUnmapMemory(ctx.device, cur_vel.memory);

    vkMapMemory(ctx.device, type_buffer_.memory, 0, bytes_uint, 0, &mapped);
    std::memcpy(out_types.data(), mapped, bytes_uint);
    vkUnmapMemory(ctx.device, type_buffer_.memory);
}

void ComputePipeline::readback_particle_texture(VulkanContext& ctx, std::vector<float>& out_pixels) {
    if (!screenshot_readback_created_) return;
    VkDeviceSize pixel_count = REGION_W * REGION_H * 4;
    out_pixels.resize(static_cast<size_t>(pixel_count));

    // One-shot command buffer for the copy
    VkCommandBuffer cmd = ctx.begin_single_command();

    // Transition particle_texture to TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier img_barrier{};
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    img_barrier.image = particle_texture.handle;
    img_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    img_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &img_barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { REGION_W, REGION_H, 1 };
    vkCmdCopyImageToBuffer(cmd, particle_texture.handle,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshot_readback_buffer_.handle, 1, &region);

    // Transition back to GENERAL
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &img_barrier);

    ctx.end_single_command(cmd);

    // Map and copy to output
    void* mapped;
    VkDeviceSize buf_size = pixel_count * sizeof(float);
    vkMapMemory(ctx.device, screenshot_readback_buffer_.memory, 0, buf_size, 0, &mapped);
    std::memcpy(out_pixels.data(), mapped, static_cast<size_t>(buf_size));
    vkUnmapMemory(ctx.device, screenshot_readback_buffer_.memory);
}

// ── Record (called per frame while simulation is active) ──────────────────────

void ComputePipeline::record(VkCommandBuffer cmd,
                             const SimConfig& cfg,
                             float dt,
                             uint32_t halo_count,
                             float time_seconds,
                             float day_night_factor,
                             glm::vec2 wind,
                             uint32_t extra_effect_flags)
{
    if (pos_buffer_a_.handle == VK_NULL_HANDLE) return;

    VkDescriptorSet active_set = (tick % 2 == 0) ? desc_set_a_ : desc_set_b_;
    uint32_t particle_count    = static_cast<uint32_t>(cfg.particle_count);

    PushConstants pc{};
    pc.region_size = { static_cast<float>(REGION_W) * 3.0f, static_cast<float>(REGION_H) * 3.0f };
    pc.camera_origin      = cfg.camera_origin;
    pc.particle_count     = particle_count;
    pc.particle_types     = static_cast<uint32_t>(cfg.particle_types);
    pc.dt                 = dt;
    pc.halo_count         = halo_count;
    pc.time_seconds       = time_seconds;
    pc.camera_zoom        = cfg.current_camera_zoom;
    pc.radius             = cfg.radius;
    pc.dampening          = cfg.dampening;
    pc.trail_decay        = cfg.trail_decay;
    pc.bloom_threshold    = cfg.bloom_threshold;
    pc.bloom_intensity    = cfg.bloom_intensity;
    pc.vignette_strength  = cfg.vignette_strength;
    pc.halo_intensity     = cfg.halo_intensity;
    pc.effect_flags       = (cfg.trails_enabled   ? EFFECT_TRAILS   : 0u)
                          | (cfg.bloom_enabled    ? EFFECT_BLOOM    : 0u)
                          | (cfg.vignette_enabled ? EFFECT_VIGNETTE : 0u)
                          | (cfg.halos_enabled    ? EFFECT_HALOS    : 0u)
                          | extra_effect_flags;
    pc.repulsion_radius   = cfg.repulsion_radius;
    pc.interaction_radius = cfg.interaction_radius;
    pc.density_limit      = cfg.density_limit;
    pc.metabolism         = cfg.metabolism;
    pc.infection_rate     = cfg.infection_rate;
    pc.spawn_probability  = cfg.spawn_probability;
    pc.cross_repro_rate   = cfg.cross_repro_rate;
    pc.day_night_factor   = day_night_factor;
    pc.wind_x             = wind.x;
    pc.wind_y             = wind.y;
    for (uint32_t i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        pc.energy_depletion_rates[i] = cfg.energy_depletion_rates[i];
    }
    pc.terrain_obstacle_count = cfg.terrain_obstacle_count;
    pc.current_temperature    = cfg.current_temperature;

    for (uint32_t i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        pc.type_radius[i] = cfg.radius * 1.0f;
        pc.type_metabolic_rate[i] = cfg.energy_depletion_rates[i] >= 0.0f ? cfg.energy_depletion_rates[i] * cfg.metabolism : cfg.metabolism;
        pc.type_metamorph_age[i] = 0.0f;
        pc.type_metamorph_target[i] = -1;
        pc.type_kin_share[i] = 0.0f;
    }
    pc.type_flocking_enabled = 0u;
    pc.memory_decay = 0.97f;
    pc.memory_strength = 5.0f;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &active_set, 0, nullptr);

// ── Grid Pass ─────────────────────────────────────────────────────────────
// 1. Clear grid
pc.step = 2;
uint32_t grid_groups = (GRID_SIZE / GROUP_DENSITY) + 1;
vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
vkCmdDispatch(cmd, grid_groups, 1, 1);

VkMemoryBarrier grid_barrier{};
grid_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
grid_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
grid_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &grid_barrier, 0, nullptr, 0, nullptr);

// 2. Count particles per cell
pc.step = 3;
vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
vkCmdDispatch(cmd, (particle_count / GROUP_DENSITY) + 1, 1, 1);

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &grid_barrier, 0, nullptr, 0, nullptr);

// 3. Prefix sum (single workgroup)
pc.step = 4;
vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
vkCmdDispatch(cmd, 1, 1, 1);

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &grid_barrier, 0, nullptr, 0, nullptr);

// 4. Sort indices
pc.step = 5;
vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
vkCmdDispatch(cmd, (particle_count / GROUP_DENSITY) + 1, 1, 1);

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &grid_barrier, 0, nullptr, 0, nullptr);

// 5. Signal diffusion step (parallel: each thread handles one cell)
pc.step = 6;
vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
{   uint32_t chem_groups = (CHEM_W * CHEM_H / GROUP_DENSITY) + 1;
    vkCmdDispatch(cmd, chem_groups, 1, 1); }

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0, 1, &grid_barrier, 0, nullptr, 0, nullptr);

// ── Step 0: physics ───────────────────────────────────────────────────────
pc.step = 0;
dispatch(cmd, active_set, pc, particle_count);

// Memory barrier: physics writes → render-to-texture reads
VkMemoryBarrier mem_barrier{};

    mem_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &mem_barrier, 0, nullptr, 0, nullptr);

    // ── Clear render texture ──────────────────────────────────────────────────
    // Manual transition to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = particle_texture.handle;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkClearColorValue clear_color{};
    clear_color.float32[0] = 0.0f;
    clear_color.float32[1] = 0.0f;
    clear_color.float32[2] = 0.0f;
    clear_color.float32[3] = 1.0f;
    VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd,
        particle_texture.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear_color, 1, &range);

    // Transition back to GENERAL for shader access
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // ── Step 1: render particles to texture ───────────────────────────────────
    // Physics wrote to the output buffer (opposite set). Render must READ those
    // updated positions, so we use the flipped descriptor set here.
    VkDescriptorSet render_set = (tick % 2 == 0) ? desc_set_b_ : desc_set_a_;
    pc.step = 1;
    dispatch(cmd, render_set, pc, particle_count);

    // Memory barrier: compute image write → fragment shader read
    VkImageMemoryBarrier img_barrier{};
    img_barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    img_barrier.image        = particle_texture.handle;
    img_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    img_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &img_barrier);

    tick++;
}

void ComputePipeline::dispatch(VkCommandBuffer cmd,
                               VkDescriptorSet dset,
                               const PushConstants& pc,
                               uint32_t particle_count)
{
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout_, 0, 1, &dset, 0, nullptr);
    vkCmdPushConstants(cmd, pipeline_layout_,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(PushConstants), &pc);
    uint32_t groups = particle_count / GROUP_DENSITY + 1;
    vkCmdDispatch(cmd, groups, 1, 1);
}

void ComputePipeline::resize_buffers(VulkanContext& ctx, const Particles& particles) {
    uint32_t needed = static_cast<uint32_t>(particles.positions.size());
    if (needed <= buffer_capacity_) {
        // Within existing GPU capacity — update particle count only.
        // New particle data was already written to CPU arrays; the next
        // compute dispatch reads cfg.particle_count so it's fine.
        return;
    }

    vkDeviceWaitIdle(ctx.device);
    clear_buffers(ctx);
    if (desc_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.device, desc_pool_, nullptr);
        desc_pool_ = VK_NULL_HANDLE;
    }
    create_descriptor_pool(ctx.device);
    create_buffers(ctx, particles);
}

void ComputePipeline::upload_particle_range(VulkanContext& ctx, const Particles& particles,
                                             uint32_t start, uint32_t count) {
    if (count == 0) return;
    VkDeviceSize byte_off   = static_cast<VkDeviceSize>(start) * sizeof(glm::vec2);
    VkDeviceSize byte_cnt   = static_cast<VkDeviceSize>(count) * sizeof(glm::vec2);
    VkDeviceSize type_off   = static_cast<VkDeviceSize>(start) * sizeof(uint32_t);
    VkDeviceSize type_cnt   = static_cast<VkDeviceSize>(count) * sizeof(uint32_t);
    VkDeviceSize flt_off    = static_cast<VkDeviceSize>(start) * sizeof(float);
    VkDeviceSize flt_cnt    = static_cast<VkDeviceSize>(count) * sizeof(float);

    ctx.update_buffer_range(pos_buffer_a_,  &particles.positions[start],         byte_off, byte_cnt);
    ctx.update_buffer_range(pos_buffer_b_,  &particles.positions[start],         byte_off, byte_cnt);
    ctx.update_buffer_range(vel_buffer_a_,  &particles.velocities[start],        byte_off, byte_cnt);
    ctx.update_buffer_range(vel_buffer_b_,  &particles.velocities[start],        byte_off, byte_cnt);
    ctx.update_buffer_range(type_buffer_,   &particles.types[start],             type_off, type_cnt);
    ctx.update_buffer_range(angle_buffer_a_, &particles.angles[start],           flt_off,  flt_cnt);
    ctx.update_buffer_range(angle_buffer_b_, &particles.angles[start],           flt_off,  flt_cnt);
    ctx.update_buffer_range(angular_vel_buffer_a_, &particles.angular_velocities[start], flt_off, flt_cnt);
    ctx.update_buffer_range(angular_vel_buffer_b_, &particles.angular_velocities[start], flt_off, flt_cnt);
    ctx.update_buffer_range(energy_buffer_a_, &particles.energy[start],          flt_off,  flt_cnt);
    ctx.update_buffer_range(energy_buffer_b_, &particles.energy[start],          flt_off,  flt_cnt);
    // Organism IDs for new particles
    if (!particles.organism_ids.empty() && organism_id_buffer_.handle != VK_NULL_HANDLE) {
        ctx.update_buffer_range(organism_id_buffer_, &particles.organism_ids[start], type_off, type_cnt);
    }
    // Energy modifiers & genomes — initialize to defaults for new particles
    std::vector<float> new_emods(count, 1.0f);
    ctx.update_buffer_range(energy_mod_buffer_, new_emods.data(), flt_off, flt_cnt);
    std::vector<GenomeData> new_genomes(count);
    for (size_t i = 0; i < new_genomes.size(); ++i) {
        new_genomes[i].age       = 0.0f;
        new_genomes[i].lifespan  = DEFAULT_LIFESPAN;
        new_genomes[i].self_mod  = 1.0f;
        new_genomes[i].cross_mod = 1.0f;
        new_genomes[i].generation = 0u;
    }
    VkDeviceSize genome_off = static_cast<VkDeviceSize>(start) * sizeof(GenomeData);
    VkDeviceSize genome_cnt = static_cast<VkDeviceSize>(count) * sizeof(GenomeData);
    ctx.update_buffer_range(genome_buffer_a_, new_genomes.data(), genome_off, genome_cnt);
    ctx.update_buffer_range(genome_buffer_b_, new_genomes.data(), genome_off, genome_cnt);
}
