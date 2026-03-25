#pragma once
// ============================================================================
// World System - Regions, Corruption, Fallen Territories
// ============================================================================

#include "core/Types.h"
#include "core/EventBus.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Unsuffered {

// --- Region Data (loaded from regions.json) ---
struct RegionData {
    RegionID id;
    std::string name;
    FallenID fallen;
    Element domain_element;
    std::string corruption_effect;

    // Visual profile
    glm::vec3 dominant_hue;
    int cel_steps;         // 3-5
    glm::vec3 outline_color;
    std::string distortion_shader;
    float particle_density;

    // Gameplay
    float corruption_rate;  // Corruption gain per minute in this territory
    std::vector<CreatureID> native_creatures;
    std::vector<CreatureID> rare_creatures;
    glm::vec2 world_min;    // AABB bounds
    glm::vec2 world_max;

    // [AUDIO HOOK] Audio profile keys (resolved via audio_manifest.json)
    // region_{id}_ambient, region_{id}_exploration,
    // region_{id}_tension, region_{id}_combat
};

// --- Corruption System ---
// Extended time in Fallen territories fills corruption meter.
// Effects scale with level:
//   0-25%:  Visual distortion increases
//   25-50%: Creature abilities become unreliable
//   50-75%: Bond integrity degrades, creatures may refuse commands
//   75-100%: Creatures may attack Grak
//
// [AUDIO HOOK] Corruption level is shared with AudioManager::SetCorruptionLevel()
// Audio distortion mirrors visual distortion at each threshold.
class CorruptionSystem {
public:
    void Initialize();

    // Update corruption based on current region and time spent
    // [AUDIO HOOK] Fires CorruptionIncrease/CorruptionThreshold events
    void Update(double dt, RegionID current_region);

    // Reduce corruption (safe zones, items)
    void Purge(float amount);

    float GetLevel() const { return m_level; }
    float GetNormalized() const { return m_level / m_max_corruption; }

    // Gameplay effects at current level
    bool AbilityUnreliable() const { return m_level > m_max_corruption * 0.25f; }
    bool BondDegrading() const { return m_level > m_max_corruption * 0.50f; }
    bool CreaturesMayRefuse() const { return m_level > m_max_corruption * 0.75f; }
    float TamingPenalty() const { return m_level > m_max_corruption * 0.50f ? 0.5f : 1.0f; }

private:
    float m_level = 0.0f;
    float m_max_corruption = 100.0f;

    // Track thresholds for audio events
    int m_last_threshold = 0;  // 0, 25, 50, 75
    void CheckThreshold();
};

// --- World Map ---
class WorldMap {
public:
    bool LoadRegions(const std::string& json_path);

    // Determine which region contains a world position
    RegionID GetRegionAt(const glm::vec2& world_pos) const;

    // Get region data
    const RegionData& GetRegion(RegionID id) const;

    // [AUDIO HOOK] Called when player crosses territory boundary
    // Fires RegionAudioData event -> AudioManager::CrossfadeRegion()
    void OnRegionTransition(RegionID from, RegionID to);

    // Get all regions
    const std::vector<RegionData>& GetAllRegions() const { return m_regions; }

    // Get region's Fallen controller
    FallenID GetFallenAt(RegionID id) const;

private:
    std::vector<RegionData> m_regions;
    std::unordered_map<RegionID, size_t> m_region_index;
};

} // namespace Unsuffered
