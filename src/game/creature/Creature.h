#pragma once
// ============================================================================
// Creature System - Bond Depth growth, Covenant Taming, creature data
// ============================================================================

#include "core/Types.h"
#include "core/EventBus.h"
#include <string>
#include <array>
#include <vector>

namespace Unsuffered {

// --- Creature Stats ---
struct CreatureStats {
    std::array<float, static_cast<size_t>(StatType::COUNT)> base{};
    std::array<float, static_cast<size_t>(StatType::COUNT)> current{};

    float Get(StatType s) const { return current[static_cast<size_t>(s)]; }
    float GetBase(StatType s) const { return base[static_cast<size_t>(s)]; }
    void Set(StatType s, float v) { current[static_cast<size_t>(s)] = v; }
};

// --- Creature Ability ---
struct Ability {
    AbilityID id;
    std::string name;
    Element element;
    float power;
    float stamina_cost;
    float weight;      // For clash resolution: heavier wins but slower
    float speed;       // Turn order priority
    StatType stat_type; // Which stat scales damage
    BondLevel required_bond; // Minimum bond to unlock
    std::string description;
};

// --- Creature Data (loaded from JSON) ---
struct CreatureData {
    CreatureID id;
    std::string name;
    std::string description;
    CreatureCategory category;
    std::array<Element, 2> types;       // Dual-type for BoundForms
    CreatureStats base_stats;
    std::vector<AbilityID> ability_pool; // Unlocked via bond depth
    float base_taming_rate;              // Base covenant acceptance rate
    std::string lore;
    FallenID origin_domain;              // Which Fallen's domain it originates from

    // [AUDIO HOOK] Each creature has voice profile entries in audio_manifest.json
    // Format: creature_{id}_idle, creature_{id}_combat, creature_{id}_bond_{level}
};

// --- Active Creature Instance ---
class Creature {
public:
    Creature(const CreatureData& data);

    // Identity
    CreatureID GetID() const { return m_data.id; }
    const std::string& GetName() const { return m_data.name; }
    CreatureCategory GetCategory() const { return m_data.category; }
    Element GetPrimaryType() const { return m_data.types[0]; }
    const std::array<Element, 2>& GetTypes() const { return m_data.types; }

    // Stats
    float GetStat(StatType s) const { return m_stats.Get(s); }
    void SetStat(StatType s, float v) { m_stats.Set(s, v); }
    float GetHP() const { return GetStat(StatType::HP); }
    float GetMaxHP() const { return m_stats.GetBase(StatType::HP); }
    bool IsFainted() const { return GetHP() <= 0.0f; }

    // Bond System
    int GetBondDepth() const { return m_bond_depth; }
    BondLevel GetBondLevel() const;

    // [AUDIO HOOK] Bond changes fire BondAudioData via EventBus
    void AddBondDepth(int amount);
    void ReduceBondDepth(int amount);

    // Can this creature depart? (Bond 5+ with betrayal)
    bool WillDepart(bool betrayal_occurred) const;

    // Available abilities based on current bond level
    std::vector<const Ability*> GetAvailableAbilities() const;

    // Covenant Taming compatibility
    float GetWillToPower() const { return m_will_to_power; }
    bool CanAcceptCovenant() const;

    // Neglect timer (creature stored without use)
    void UpdateNeglect(double dt);
    float GetNeglectHours() const { return m_neglect_hours; }

    // Combat state
    void TakeDamage(float amount);
    void Heal(float amount);
    void RestoreStamina(float amount);

private:
    CreatureData m_data;
    CreatureStats m_stats;
    int m_bond_depth = 0;
    float m_will_to_power = 1.0f;
    float m_neglect_hours = 0.0f;
    bool m_in_party = false;
    std::vector<Ability> m_abilities;

    void RecalculateStats(); // Bond level affects stats
};

// --- Bond System ---
class BondSystem {
public:
    // [AUDIO HOOK] All bond changes publish BondAudioData to EventBus
    void OnBattleTogether(Creature& creature, float contribution);
    void OnExploration(Creature& creature, float distance);
    void OnCovenantInteraction(Creature& creature);
    void OnNarrativeEvent(Creature& creature, int depth_bonus);
    void OnNeglect(Creature& creature, double hours_stored);
    void OnBetrayal(Creature& creature); // Creature's nature contradicted

    // Check if any creature should depart
    bool CheckDepartures(std::vector<Creature>& party);

private:
    void EmitBondEvent(CreatureID id, BondLevel old_lv, BondLevel new_lv, bool departure = false);
};

// --- Covenant Taming System ---
class CovenantTaming {
public:
    struct TamingContext {
        float creature_hp_ratio;       // Current/Max HP
        int ally_high_bond_count;      // Allies above Bond 5
        bool philosophical_resonance;  // Story-gated match
        float corruption_level;        // Current corruption (halves rate above 50)
        bool has_covenant_item;        // Type-specific item
    };

    // Calculate taming success probability
    static float CalculateRate(const CreatureData& target, const TamingContext& ctx);

    // [AUDIO HOOK] On success: fires CreatureTamed event
    // AudioManager plays "covenant_formed" SFX + Grak voice line
    static bool AttemptCovenant(const CreatureData& target, const TamingContext& ctx);
};

} // namespace Unsuffered
