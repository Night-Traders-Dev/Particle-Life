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
    ImGui::Begin("Metrics Explorer", &show_metrics_window);

    if (ImGui::BeginTabBar("MetricsTabs")) {
        if (ImGui::BeginTabItem("Particles")) {
            if (ImGui::BeginTable("particle_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
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
            if (ImGui::BeginTable("org_metrics_table", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, 400))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
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
