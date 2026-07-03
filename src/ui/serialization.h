#pragma once
#include "types.h"
#include "particles.h"
#include <fstream>
#include <iostream>

// Minimal manual serialization since we cannot rely on external libraries
namespace Serialization {
    inline bool save_config(const std::string& path, const SimConfig& cfg, const Particles& p) {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs.write(reinterpret_cast<const char*>(&cfg), sizeof(SimConfig));
        ofs.write(reinterpret_cast<const char*>(p.forces.data()), p.forces.size() * sizeof(float));
        ofs.write(reinterpret_cast<const char*>(p.colors.data()), p.colors.size() * sizeof(glm::vec4));
        ofs.write(reinterpret_cast<const char*>(p.behavior_flags), sizeof(p.behavior_flags));
        return true;
    }

    inline bool load_config(const std::string& path, SimConfig& cfg, Particles& p) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        ifs.read(reinterpret_cast<char*>(&cfg), sizeof(SimConfig));
        ifs.read(reinterpret_cast<char*>(p.forces.data()), p.forces.size() * sizeof(float));
        ifs.read(reinterpret_cast<char*>(p.colors.data()), p.colors.size() * sizeof(glm::vec4));
        ifs.read(reinterpret_cast<char*>(p.behavior_flags), sizeof(p.behavior_flags));
        return true;
    }

    inline bool save_snapshot(const std::string& path, const Particles& p) {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        size_t count = p.positions.size();
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
        ofs.write(reinterpret_cast<const char*>(p.positions.data()), count * sizeof(glm::vec2));
        ofs.write(reinterpret_cast<const char*>(p.velocities.data()), count * sizeof(glm::vec2));
        return true;
    }
}
