#include "interface.h"
#include "types.h"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <random>
#include <algorithm>
#include <string>
#include <vector>
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include "serialization.h"

void Interface::init() {
    std::random_device rd;
    seed_value = static_cast<int>(rd() % 32768);
}

ImVec4 Interface::force_to_color(float f) {
    glm::vec4 c = calc_force_button_color(f);
    return { c.r, c.g, c.b, 1.0f };
}

void Interface::render_imgui(SimConfig&       cfg,
                              Particles&       particles,
                              OrganismManager& org_manager,
                              bool&            request_reset,
                              double           day_night_time,
                              double           cycle_length)
{
    request_reset = false;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Status Bar ────────────────────────────────────────────────────────────
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float height = ImGui::GetFrameHeight() + 8.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f)); 
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
        
        ImGui::Begin("Status Bar", nullptr, 
                     ImGuiWindowFlags_NoDecoration | 
                     ImGuiWindowFlags_NoInputs     | 
                     ImGuiWindowFlags_NoMove       | 
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        
        double progress = day_night_time / cycle_length;
        double total_hours = progress * 24.0 + 6.0;
        int hours = (int)std::floor(total_hours) % 24;
        int minutes = (int)std::floor((total_hours - std::floor(total_hours)) * 60.0);
        
        float temp_factor = std::sin(static_cast<float>(2.0 * 3.1415926535 * (progress - 0.125)));
        float temp_c = 22.5f + 12.5f * temp_factor;
        float temp_f = temp_c * 1.8f + 32.0f;
        
        const char* day_phase = "DAY";
        ImVec4 phase_col = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);

        if (day_night_time >= 600.0 && day_night_time < 690.0) {
            day_phase = "SUNSET";
            phase_col = ImVec4(1.0f, 0.5f, 0.2f, 1.0f);
        } else if (day_night_time >= 690.0 && day_night_time < 1110.0) {
            day_phase = "NIGHT";
            phase_col = ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
        } else if (day_night_time >= 1110.0) {
            day_phase = "SUNRISE";
            phase_col = ImVec4(1.0f, 0.7f, 0.4f, 1.0f);
        }

        ImGui::Text("Time: %02d:%02d | ", hours, minutes);
        ImGui::SameLine();
        ImGui::TextColored(phase_col, "%s", day_phase);
        ImGui::SameLine();
        ImGui::Text(" | Temp: %.1f°C / %.1f°F", temp_c, temp_f);
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 240);
        
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1,1,1,0.2f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1,1,1,0.4f));
        if (ImGui::Selectable("##stats_click", false, ImGuiSelectableFlags_None, ImVec2(230, 0))) {
            show_metrics_window = !show_metrics_window;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(ImGui::GetWindowWidth() - 240);
        ImGui::Text("Particles: %u | Organisms: %zu", cfg.particle_count, org_manager.organisms.size());

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    if (show_metrics_window) {
        draw_metrics_window(cfg, particles, org_manager);
    }

    if (!settings_visible) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(480, static_cast<float>(io.DisplaySize.y)), ImGuiCond_Always);
    ImGui::Begin("SIMULATION SETTINGS",
                 nullptr,
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove   |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    mouse_within = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    ImGui::SeparatorText("Generation");
    ImGui::SliderFloat("Count Slider", &particle_count_slider, 1.0f, 317.0f, "%.0f");
    int pc = static_cast<int>(std::max(2.0f, std::pow(particle_count_slider, 2.0f)));
    ImGui::Text("Particle Count:  %d", pc);
    cfg.particle_count = static_cast<uint32_t>(pc);
    ImGui::SliderFloat("Types Slider", &particle_types_slider, 1.0f, 10.0f, "%.0f");
    int pt = static_cast<int>(particle_types_slider);
    ImGui::Text("Particle Types:  %d", pt);
    cfg.particle_types = static_cast<uint32_t>(pt);
    ImGui::Checkbox("Reset Colors on Next Run",  &reset_colors_check);
    ImGui::Checkbox("Reset Forces on Next Run",  &reset_forces_check);
    cfg.reset_colors = reset_colors_check;
    cfg.reset_forces = reset_forces_check;
    ImGui::InputInt("Seed", &seed_value);
    seed_value = std::clamp(seed_value, 0, 65535);
    cfg.generation_seed = static_cast<uint32_t>(seed_value);
    if (ImGui::Button("Reset Simulation (F2)", ImVec2(-1, 0))) request_reset = true;

    ImGui::SeparatorText("Real-time Physics");
    ImGui::SliderFloat("Dampening", &dampening_slider, 0.0f, 1.0f);
    cfg.dampening = dampening_slider;
    ImGui::SliderFloat("Repulsion Radius", &repulsion_slider, 1.0f, 400.0f);
    cfg.repulsion_radius = repulsion_slider;
    ImGui::SliderFloat("Interaction Radius", &interaction_slider, 1.0f, 720.0f);
    cfg.interaction_radius = interaction_slider;
    ImGui::SliderFloat("Density Limit", &density_limit_slider, 0.0f, 720.0f);
    cfg.density_limit = density_limit_slider;
    ImGui::SliderFloat("Particle Radius", &particle_radius_slider, 1.0f, 10.0f);
    cfg.radius = particle_radius_slider;
    ImGui::SliderFloat("Metabolism", &metabolism_slider, 0.0f, 1.0f);
    cfg.metabolism = metabolism_slider;
    ImGui::SliderFloat("Infection Rate", &infection_slider, 0.0f, 1.0f);
    cfg.infection_rate = infection_slider;
    ImGui::SliderFloat("Spawn Probability", &spawn_slider, 0.0f, 0.05f, "%.4f");
    cfg.spawn_probability = spawn_slider;
    ImGui::SliderFloat("Time Scale", &time_scale_slider, 0.0f, 10.0f, "%.1fx");
    ImGui::Checkbox("Glow Enabled (visual hint only)", &glow_enabled);

    ImGui::SeparatorText("Presets");
    if (ImGui::Button("Save Config")) Serialization::save_config("config.bin", cfg, particles);
    ImGui::SameLine();
    if (ImGui::Button("Load Config")) if (Serialization::load_config("config.bin", cfg, particles)) request_reset = true;

    ImGui::SeparatorText("Particle Values");
    ImGui::Checkbox("Symmetry", &symmetry_enabled);
    ImGui::SameLine();
    if (ImGui::Button("Randomize")) {
        for (uint32_t i = 0; i < MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES; ++i)
            particles.forces[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
    }
    draw_particle_grid(cfg, particles);
    draw_archetype_panel(particles, cfg);
    draw_conversion_panel(particles, cfg);
    draw_organism_panel(org_manager, particles, cfg);

    ImGui::Separator();
    ImGui::TextDisabled("F1: toggle UI | F2: reset | Space: pause");
    ImGui::TextDisabled("Drag: pan | Scroll: zoom | ESC: quit");
    ImGui::TextDisabled("Night-Traders-Dev 2026");
    ImGui::End();

    // ── Particle / Organism Hover Logic ──────────────────────────────────────
    if (!ImGui::GetIO().WantCaptureMouse) {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        glm::vec2 world_mouse = (glm::vec2(mouse_pos.x, mouse_pos.y) - glm::vec2(1280.0f, 720.0f)) / cfg.current_camera_zoom + cfg.camera_origin;

        float  min_dist_particle = 10.0f / cfg.current_camera_zoom;
        int    closest_particle  = -1;
        
        for (size_t i = 0; i < particles.positions.size(); ++i) {
            float dist = glm::distance(particles.positions[i], world_mouse);
            if (dist < min_dist_particle) {
                min_dist_particle = dist;
                closest_particle = (int)i;
            }
        }
        
        hover_particle_index = closest_particle;
        hover_organism_id = -1;

        if (closest_particle != -1) {
            hover_organism_id = (int64_t)particles.stats[closest_particle].current_organism_id;
        } else {
            float min_dist_org = 30.0f / cfg.current_camera_zoom;
            for (const auto& org : org_manager.organisms) {
                float dist = glm::distance(org.centroid, world_mouse);
                if (dist < min_dist_org) {
                    min_dist_org = dist;
                    hover_organism_id = (int64_t)org.id;
                }
            }
        }

        if (hover_particle_index != -1 || hover_organism_id != -1)
            draw_hover_popup(particles, org_manager);
    }
}

void Interface::draw_hover_popup(const Particles& particles, const OrganismManager& org_manager) {
    ImGui::BeginTooltip();

    if (hover_particle_index != -1) {
        const ParticleStats& stats = particles.stats[hover_particle_index];
        uint32_t type = particles.types[hover_particle_index];
        const glm::vec4& c = particles.colors[type];

        ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "Particle #%d", hover_particle_index);
        ImGui::Separator();
        ImGui::Text("Type: %u", type);
        ImGui::Text("Age: %.1fs", stats.spawn_time);
        ImGui::Text("Conversions: %u", stats.conversion_count);
        if (stats.current_organism_id != -1)
            ImGui::Text("Part of Organism #%d", stats.current_organism_id);
    }

    if (hover_organism_id != -1) {
        if (hover_particle_index != -1) ImGui::Separator();

        const Organism* org = nullptr;
        for (const auto& o : org_manager.organisms) {
            if (o.id == (uint64_t)hover_organism_id) {
                org = &o;
                break;
            }
        }

        if (org) {
            const glm::vec4& c = particles.colors[org->traits.dominant_type];
            ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "Organism #%lu", org->id);
            ImGui::Separator();
            ImGui::Text("Dominant Type: %u", org->traits.dominant_type);
            ImGui::Text("Size: %u particles", org->traits.size);
            ImGui::Text("Avg Speed: %.2f", org->traits.avg_speed);
            ImGui::Text("Generation: %u", org->traits.generation);
            ImGui::Text("Kills: %u | Divisions: %u", org->traits.kills, org->traits.divisions);
            
            if (ImGui::BeginTable("org_types", 2, ImGuiTableFlags_SizingFixedFit)) {
                for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
                    if (org->traits.type_counts[t] > 0) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const glm::vec4& tc = particles.colors[t];
                        ImGui::ColorButton("##t", ImVec4(tc.r, tc.g, tc.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                        ImGui::SameLine();
                        ImGui::Text("Type %u:", t);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%u", org->traits.type_counts[t]);
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndTooltip();
}

void Interface::draw_particle_grid(SimConfig& cfg, Particles& particles) {
    uint32_t pt = cfg.particle_types;
    if (pt == 0) return;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;

    ImGuiIO& io = ImGui::GetIO();
    float available_w = ImGui::GetContentRegionAvail().x;
    float cell_size   = available_w / static_cast<float>(pt + 1);
    cell_size = std::max(cell_size, 12.0f);

    for (uint32_t row = 0; row <= pt; ++row) {
        for (uint32_t col = 0; col <= pt; ++col) {
            ImGui::PushID(static_cast<int>(row * (MAX_PARTICLE_TYPES + 1) + col));
            if (row == 0 && col == 0) ImGui::Dummy(ImVec2(cell_size, cell_size));
            else if (row == 0) {
                uint32_t type_idx = col - 1;
                glm::vec4& c      = particles.colors[type_idx];
                float col_arr[4]  = { c.r, c.g, c.b, c.a };
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::ColorButton("##col", ImVec4(c.r, c.g, c.b, c.a), ImGuiColorEditFlags_NoTooltip, ImVec2(cell_size, cell_size))) {}
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) ImGui::OpenPopup("color_pick");
                if (ImGui::BeginPopup("color_pick")) {
                    if (ImGui::ColorPicker4("##picker", col_arr, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview))
                        c = { col_arr[0], col_arr[1], col_arr[2], col_arr[3] };
                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
            }
            else if (col == 0) {
                uint32_t type_idx = row - 1;
                glm::vec4& c      = particles.colors[type_idx];
                ImGui::ColorButton("##row_col", ImVec4(c.r, c.g, c.b, c.a), ImGuiColorEditFlags_NoTooltip, ImVec2(cell_size, cell_size));
            }
            else {
                uint32_t type_a = col - 1;
                uint32_t type_b = row - 1;
                uint32_t fi     = type_a + type_b * MAX_PARTICLE_TYPES;
                float&   force  = particles.forces[fi];
                ImVec4 fcolor = force_to_color(force);
                ImGui::PushStyleColor(ImGuiCol_Button,        fcolor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(fcolor.x * 1.2f, fcolor.y * 1.2f, fcolor.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(fcolor.x * 0.8f, fcolor.y * 0.8f, fcolor.z * 0.8f, 1.0f));
                char label[16];
                std::snprintf(label, sizeof(label), "##f%u_%u", type_a, type_b);
                ImGui::Button(label, ImVec2(cell_size, cell_size));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Force: %.2f\nScroll to adjust\nRight-click to zero", force);
                    float scroll = io.MouseWheel;
                    if (scroll != 0.0f) {
                        force = std::clamp(force + scroll * 0.1f, -1.0f, 1.0f);
                        if (symmetry_enabled) {
                            uint32_t sym_fi = type_b + type_a * MAX_PARTICLE_TYPES;
                            particles.forces[sym_fi] = force;
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        force = 0.0f;
                        if (symmetry_enabled) {
                            uint32_t sym_fi = type_b + type_a * MAX_PARTICLE_TYPES;
                            particles.forces[sym_fi] = 0.0f;
                        }
                    }
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::PopID();
            if (col < pt) ImGui::SameLine(0, 0);
        }
    }
}

void Interface::draw_archetype_panel(Particles& particles, const SimConfig& cfg) {
    if (!ImGui::CollapsingHeader("Particle Behaviors (Archetypes)")) return;
    static const char* preset_names[] = { "Custom...", "Default", "Repeller", "Polar", "Heavy", "Catalyst", "Membrane", "Viral", "Leech", "Shield", "Proton", "Electron", "Pos Monopole", "Neg Monopole" };
    static const char* flag_names[] = { "REPEL", "POLAR", "HEAVY", "CATALYST", "VIRAL", "LEECH", "SHIELD", "POSITIVE", "NEGATIVE" };
    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;
    ImGui::TextDisabled("Mix behaviors per particle type:");
    for (uint32_t t = 0; t < pt; ++t) {
        ImGui::PushID(static_cast<int>(t));
        const glm::vec4& c = particles.colors[t];
        ImGui::ColorButton("##swatch", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("##preset", &preset_selection[t], preset_names, 14)) {
            switch (preset_selection[t]) {
            case 1:  particles.apply_preset_default(t);              break;
            case 2:  particles.apply_preset_repeller(t);             break;
            case 3:  particles.apply_preset_polar(t, pt);            break;
            case 4:  particles.apply_preset_heavy(t);                break;
            case 5:  particles.apply_preset_catalyst(t);             break;
            case 6:  particles.apply_preset_membrane(t);             break;
            case 7:  particles.apply_preset_viral(t, pt);            break;
            case 8:  particles.apply_preset_leech(t);                break;
            case 9:  particles.apply_preset_shield(t);               break;
            case 10: particles.apply_preset_proton(t);               break;
            case 11: particles.apply_preset_electron(t);             break;
            case 12: particles.apply_preset_pos_monopole(t);         break;
            case 13: particles.apply_preset_neg_monopole(t);         break;
            }
        }
        uint32_t& flags = particles.behavior_flags[t];
        for (int fi = 0; fi < 9; ++fi) {
            ImGui::SameLine();
            bool bit = (flags & (1u << fi)) != 0;
            if (ImGui::Checkbox(flag_names[fi], &bit)) {
                if (bit) flags |= (1u << fi);
                else     flags &= ~(1u << fi);
                preset_selection[t] = 0;
            }
        }
        ImGui::PopID();
    }
}

void Interface::draw_organism_panel(OrganismManager& org_manager, const Particles& particles, const SimConfig& cfg) {
    if (!ImGui::CollapsingHeader("Organisms")) return;
    ImGui::SliderFloat("Cluster Radius", &org_manager.cluster_radius, 10.0f, 200.0f, "%.0f");
    uint32_t org_count = static_cast<uint32_t>(org_manager.organisms.size());
    ImGui::Text("Active Organisms: %u", org_count);
    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;
    ImGui::TextDisabled("Force Scales (trait feedback):");
    for (uint32_t t = 0; t < pt; ++t) {
        ImGui::PushID(static_cast<int>(t));
        const glm::vec4& c = particles.colors[t];
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(c.r, c.g, c.b, 1.0f));
        float frac = (particles.trait_scales[t] - 1.0f) / 0.8f;
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 6.0f), "");
        ImGui::SameLine();
        ImGui::Text("%.2fx", particles.trait_scales[t]);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void Interface::draw_conversion_panel(Particles& particles, const SimConfig& cfg) {
    if (!ImGui::CollapsingHeader("Particle Conversions")) return;
    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;
    static int type_a = 0; static int type_b = 0;
    ImGui::Combo("From Type", &type_a, [](void*, int idx, const char** out) { static char buf[32]; sprintf(buf, "Type %d", idx); *out = buf; return true; }, nullptr, pt);
    ImGui::Combo("To Type", &type_b, [](void*, int idx, const char** out) { static char buf[32]; sprintf(buf, "Type %d", idx); *out = buf; return true; }, nullptr, pt);
    int idx = type_a + type_b * MAX_PARTICLE_TYPES;
    ConversionData& conv = particles.conversion_matrix[idx];
    ImGui::SliderFloat("Probability", &conv.probability, 0.0f, 1.0f, "%.4f");
    if (ImGui::Button("Set Conversion")) conv.target_type = type_b;
    ImGui::SameLine();
    if (ImGui::Button("Clear Conversion")) { conv.target_type = -1; conv.probability = 0.0f; }
}

void Interface::draw_metrics_window(SimConfig& cfg, Particles& particles, OrganismManager& org_manager) {
    (void)cfg;
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Metrics Explorer", &show_metrics_window);

    if (ImGui::BeginTabBar("MetricsTabs")) {
        if (ImGui::BeginTabItem("Particles")) {
            if (ImGui::BeginTable("p_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Energy", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Convs", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Org ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < particles.positions.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%zu", i);
                    ImGui::TableSetColumnIndex(1); 
                    const glm::vec4& c = particles.colors[particles.types[i]];
                    ImGui::ColorButton("##t", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                    ImGui::SameLine(); ImGui::Text("%u", particles.types[i]);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", particles.energy[i]);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.1fs", particles.stats[i].spawn_time);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%u", particles.stats[i].conversion_count);
                    ImGui::TableSetColumnIndex(5); 
                    if (particles.stats[i].current_organism_id != -1)
                        ImGui::Text("%d", particles.stats[i].current_organism_id);
                    else
                        ImGui::TextDisabled("-");
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Organisms")) {
            if (ImGui::BeginTable("o_table", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Gen", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Kills", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Divs", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                for (const auto& org : org_manager.organisms) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%lu", org.id);
                    ImGui::TableSetColumnIndex(1); 
                    const glm::vec4& c = particles.colors[org.traits.dominant_type];
                    ImGui::ColorButton("##ot", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                    ImGui::SameLine(); ImGui::Text("%u", org.traits.dominant_type);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%u", org.traits.size);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", org.traits.avg_speed);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%u", org.traits.generation);
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%u", org.traits.kills);
                    ImGui::TableSetColumnIndex(6); ImGui::Text("%u", org.traits.divisions);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
