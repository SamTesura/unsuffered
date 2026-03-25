// ============================================================================
// World System Implementation
// Regions, Corruption, Fallen Territories
// ============================================================================

#include "game/world/World.h"
#include "core/Logger.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>

namespace Unsuffered {

// ============================================================================
// Corruption System
// ============================================================================

void CorruptionSystem::Initialize() {
    m_level = 0.0f;
    m_last_threshold = 0;
    Logger::Info("CorruptionSystem initialized");
}

void CorruptionSystem::Update(double dt, RegionID current_region) {
    // Corruption increases based on region's corruption rate
    // Safe zones (RegionID 0 = Ashwalker Warrens) don't generate corruption
    if (current_region == 0) {
        // In safe zone: corruption decays slowly
        m_level = std::max(0.0f, m_level - static_cast<float>(dt * 0.5));
        return;
    }

    // In Fallen territory: corruption increases
    // Rate varies by region (loaded from region data)
    // For now, use a base rate of 1.0 per minute
    float rate = 1.0f; // This would come from RegionData::corruption_rate
    m_level += rate * static_cast<float>(dt / 60.0);
    m_level = std::min(m_level, m_max_corruption);

    // [AUDIO HOOK] Continuous corruption level update
    AudioEventData event{AudioEvent::CorruptionIncrease};
    event.intensity = GetNormalized();
    EventBus::Instance().Publish(event);

    // Check threshold crossings
    CheckThreshold();
}

void CorruptionSystem::Purge(float amount) {
    float old_level = m_level;
    m_level = std::max(0.0f, m_level - amount);

    // Recalculate threshold after purge
    int new_threshold = 0;
    if (GetNormalized() >= 0.75f) new_threshold = 75;
    else if (GetNormalized() >= 0.50f) new_threshold = 50;
    else if (GetNormalized() >= 0.25f) new_threshold = 25;
    m_last_threshold = new_threshold;

    Logger::Info("Corruption purged: {:.1f} -> {:.1f}", old_level, m_level);
}

void CorruptionSystem::CheckThreshold() {
    float normalized = GetNormalized();
    int current_threshold = 0;
    if (normalized >= 0.75f) current_threshold = 75;
    else if (normalized >= 0.50f) current_threshold = 50;
    else if (normalized >= 0.25f) current_threshold = 25;

    if (current_threshold > m_last_threshold) {
        m_last_threshold = current_threshold;

        // [AUDIO HOOK] Threshold crossed - step up distortion
        AudioEventData event{AudioEvent::CorruptionThreshold};
        event.intensity = normalized;
        EventBus::Instance().Publish(event);

        Logger::Warn("Corruption threshold crossed: {}% | Effects: {}",
                     current_threshold,
                     current_threshold == 25 ? "Abilities unreliable" :
                     current_threshold == 50 ? "Bond integrity degrading" :
                     "Creatures may refuse commands");
    }
}

// ============================================================================
// World Map
// ============================================================================

bool WorldMap::LoadRegions(const std::string& json_path) {
    try {
        std::ifstream file(json_path);
        if (!file.is_open()) {
            Logger::Error("Failed to open regions file: {}", json_path);
            return false;
        }

        nlohmann::json j;
        file >> j;

        for (const auto& region_json : j["regions"]) {
            RegionData region;
            region.id = region_json["id"].get<RegionID>();
            region.name = region_json["name"].get<std::string>();
            region.fallen = static_cast<FallenID>(region_json["fallen"].get<int>());
            region.domain_element = static_cast<Element>(region_json["element"].get<int>());
            region.corruption_effect = region_json["corruption_effect"].get<std::string>();

            // Visual profile
            auto& hue = region_json["dominant_hue"];
            region.dominant_hue = {hue[0].get<float>(), hue[1].get<float>(), hue[2].get<float>()};
            region.cel_steps = region_json["cel_steps"].get<int>();
            auto& outline = region_json["outline_color"];
            region.outline_color = {outline[0].get<float>(), outline[1].get<float>(), outline[2].get<float>()};
            region.distortion_shader = region_json.value("distortion_shader", "corruption");
            region.particle_density = region_json.value("particle_density", 1.0f);

            // Gameplay
            region.corruption_rate = region_json.value("corruption_rate", 1.0f);
            auto& bounds_min = region_json["bounds_min"];
            region.world_min = {bounds_min[0].get<float>(), bounds_min[1].get<float>()};
            auto& bounds_max = region_json["bounds_max"];
            region.world_max = {bounds_max[0].get<float>(), bounds_max[1].get<float>()};

            if (region_json.contains("native_creatures")) {
                for (auto& c : region_json["native_creatures"]) {
                    region.native_creatures.push_back(c.get<CreatureID>());
                }
            }

            size_t index = m_regions.size();
            m_region_index[region.id] = index;
            m_regions.push_back(std::move(region));
        }

        Logger::Info("Loaded {} regions from {}", m_regions.size(), json_path);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse regions: {}", e.what());
        return false;
    }
}

RegionID WorldMap::GetRegionAt(const glm::vec2& world_pos) const {
    for (const auto& region : m_regions) {
        if (world_pos.x >= region.world_min.x && world_pos.x <= region.world_max.x &&
            world_pos.y >= region.world_min.y && world_pos.y <= region.world_max.y) {
            return region.id;
        }
    }
    return 0; // Default: Ashwalker Warrens (safe zone)
}

const RegionData& WorldMap::GetRegion(RegionID id) const {
    auto it = m_region_index.find(id);
    if (it != m_region_index.end()) {
        return m_regions[it->second];
    }
    static RegionData empty{};
    Logger::Warn("Region {} not found, returning empty", id);
    return empty;
}

void WorldMap::OnRegionTransition(RegionID from, RegionID to) {
    // [AUDIO HOOK] Fire region transition event for 3-second crossfade
    RegionAudioData audio{from, to, 3.0f};
    EventBus::Instance().Publish(audio);

    Logger::Info("Region transition: {} -> {}",
                 GetRegion(from).name, GetRegion(to).name);
}

FallenID WorldMap::GetFallenAt(RegionID id) const {
    return GetRegion(id).fallen;
}

} // namespace Unsuffered
