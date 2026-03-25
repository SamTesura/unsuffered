// ============================================================================
// Creature System Implementation
// Bond Depth growth, Covenant Taming, creature lifecycle
// ============================================================================

#include "game/creature/Creature.h"
#include "core/Logger.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Unsuffered {

// ============================================================================
// Creature
// ============================================================================

Creature::Creature(const CreatureData& data)
    : m_data(data), m_stats(data.base_stats)
{
    // Copy base stats to current
    for (size_t i = 0; i < static_cast<size_t>(StatType::COUNT); ++i) {
        m_stats.current[i] = m_stats.base[i];
    }
    m_will_to_power = 1.0f;
    Logger::Info("Creature created: {} (ID: {}, Category: {})",
                 m_data.name, m_data.id, static_cast<int>(m_data.category));
}

BondLevel Creature::GetBondLevel() const {
    if (m_bond_depth >= BondDepthRequired(BondLevel::Transcendence)) return BondLevel::Transcendence;
    if (m_bond_depth >= BondDepthRequired(BondLevel::Covenant))      return BondLevel::Covenant;
    if (m_bond_depth >= BondDepthRequired(BondLevel::Kinship))       return BondLevel::Kinship;
    if (m_bond_depth >= BondDepthRequired(BondLevel::Partnership))   return BondLevel::Partnership;
    if (m_bond_depth >= BondDepthRequired(BondLevel::Trust))         return BondLevel::Trust;
    return BondLevel::Recognition;
}

void Creature::AddBondDepth(int amount) {
    BondLevel old_level = GetBondLevel();
    m_bond_depth += amount;
    if (m_bond_depth < 0) m_bond_depth = 0;
    BondLevel new_level = GetBondLevel();

    if (new_level != old_level) {
        RecalculateStats();

        // [AUDIO HOOK] Fire bond change event for audio system
        BondAudioData audio_data{m_data.id, old_level, new_level, false};
        EventBus::Instance().Publish(audio_data);

        Logger::Info("{} bond level changed: {} -> {}",
                     m_data.name, static_cast<int>(old_level), static_cast<int>(new_level));
    }

    // Reset neglect timer on positive interaction
    if (amount > 0) m_neglect_hours = 0.0f;
}

void Creature::ReduceBondDepth(int amount) {
    AddBondDepth(-amount);
}

bool Creature::WillDepart(bool betrayal_occurred) const {
    // Only Bond 5+ creatures can depart, and only on betrayal
    if (GetBondLevel() < BondLevel::Covenant) return false;
    if (!betrayal_occurred) return false;

    // Hollow creatures never depart (they have no will to leave)
    if (m_data.category == CreatureCategory::Hollow) return false;

    // Higher will-to-power creatures are more likely to leave
    return m_will_to_power > 0.7f;
}

std::vector<const Ability*> Creature::GetAvailableAbilities() const {
    std::vector<const Ability*> available;
    BondLevel current = GetBondLevel();

    for (const auto& ability : m_abilities) {
        if (static_cast<uint8_t>(ability.required_bond) <= static_cast<uint8_t>(current)) {
            available.push_back(&ability);
        }
    }
    return available;
}

bool Creature::CanAcceptCovenant() const {
    // Creatures already tamed cannot be re-tamed
    // Hollow creatures can be tamed but will never reach high bond
    // Deep Ones are very resistant (low will to accept)
    if (m_data.category == CreatureCategory::DeepOne) {
        return m_will_to_power < 0.3f; // Only when significantly weakened
    }
    return true;
}

void Creature::UpdateNeglect(double dt) {
    if (!m_in_party) {
        m_neglect_hours += static_cast<float>(dt / 3600.0);

        // Trust-level creatures leave after 10 hours of neglect
        if (GetBondLevel() == BondLevel::Trust && m_neglect_hours > 10.0f) {
            // [AUDIO HOOK] CreatureDeparture event
            BondAudioData audio_data{m_data.id, GetBondLevel(), BondLevel::Recognition, true};
            EventBus::Instance().Publish(audio_data);

            Logger::Warn("{} departed due to neglect ({:.1f} hours stored)",
                         m_data.name, m_neglect_hours);
            m_bond_depth = 0;
        }
    }
}

void Creature::TakeDamage(float amount) {
    float hp = GetHP() - amount;
    SetStat(StatType::HP, std::max(0.0f, hp));

    if (IsFainted()) {
        // [AUDIO HOOK] CreatureFaint event
        AudioEventData faint_event{AudioEvent::CreatureFaint, m_data.id};
        EventBus::Instance().Publish(faint_event);
        Logger::Info("{} fainted!", m_data.name);
    }
}

void Creature::Heal(float amount) {
    float hp = GetHP() + amount;
    SetStat(StatType::HP, std::min(hp, GetMaxHP()));
}

void Creature::RestoreStamina(float amount) {
    float stamina = GetStat(StatType::Stamina) + amount;
    float max_stamina = m_stats.GetBase(StatType::Stamina);
    SetStat(StatType::Stamina, std::min(stamina, max_stamina));
}

void Creature::RecalculateStats() {
    // Bond level provides stat multipliers
    // Recognition: 1.0x, Trust: 1.05x, Partnership: 1.1x,
    // Kinship: 1.2x, Covenant: 1.35x, Transcendence: 1.5x
    float multipliers[] = {1.0f, 1.0f, 1.05f, 1.1f, 1.2f, 1.35f, 1.5f};
    float mult = multipliers[static_cast<int>(GetBondLevel())];

    for (size_t i = static_cast<size_t>(StatType::ATK);
         i <= static_cast<size_t>(StatType::WILL); ++i) {
        m_stats.current[i] = m_stats.base[i] * mult;
    }

    Logger::Debug("{} stats recalculated (bond mult: {:.2f})", m_data.name, mult);
}

// ============================================================================
// Bond System
// ============================================================================

void BondSystem::OnBattleTogether(Creature& creature, float contribution) {
    // Fighting together builds bond. Contribution = damage dealt / taken ratio
    int depth = static_cast<int>(5.0f + contribution * 3.0f);
    creature.AddBondDepth(depth);
}

void BondSystem::OnExploration(Creature& creature, float distance) {
    // Walking together in the world builds bond slowly
    int depth = static_cast<int>(distance * 0.1f);
    if (depth > 0) creature.AddBondDepth(depth);
}

void BondSystem::OnCovenantInteraction(Creature& creature) {
    // Direct covenant interactions (feeding, grooming, talking) build bond
    creature.AddBondDepth(15);
}

void BondSystem::OnNarrativeEvent(Creature& creature, int depth_bonus) {
    // Story events can grant large bond bonuses
    creature.AddBondDepth(depth_bonus);
    Logger::Info("Narrative bond event: {} gained {} depth", creature.GetName(), depth_bonus);
}

void BondSystem::OnNeglect(Creature& creature, double hours_stored) {
    // Bond decays when creature is stored and unused
    if (hours_stored > 5.0) {
        int loss = static_cast<int>((hours_stored - 5.0) * 2.0);
        creature.ReduceBondDepth(loss);
    }
}

void BondSystem::OnBetrayal(Creature& creature) {
    // Acting against creature's nature causes severe bond damage
    creature.ReduceBondDepth(200);
    Logger::Warn("Betrayal event! {} lost 200 bond depth", creature.GetName());

    if (creature.WillDepart(true)) {
        EmitBondEvent(creature.GetID(), creature.GetBondLevel(),
                      BondLevel::Recognition, true);
    }
}

bool BondSystem::CheckDepartures(std::vector<Creature>& party) {
    bool any_departed = false;
    for (auto it = party.begin(); it != party.end(); ) {
        if (it->GetBondLevel() == BondLevel::Recognition && it->GetBondDepth() == 0) {
            EmitBondEvent(it->GetID(), BondLevel::Recognition, BondLevel::Recognition, true);
            it = party.erase(it);
            any_departed = true;
        } else {
            ++it;
        }
    }
    return any_departed;
}

void BondSystem::EmitBondEvent(CreatureID id, BondLevel old_lv, BondLevel new_lv, bool departure) {
    // [AUDIO HOOK] Bond audio events
    BondAudioData data{id, old_lv, new_lv, departure};
    EventBus::Instance().Publish(data);

    if (departure) {
        Logger::Info("Creature {} departed permanently.", id);
    }
}

// ============================================================================
// Covenant Taming
// ============================================================================

float CovenantTaming::CalculateRate(const CreatureData& target, const TamingContext& ctx) {
    float base_rate = target.base_taming_rate;

    // HP factor: lower HP = higher acceptance
    // Linear: 100% HP = 1x, 10% HP = 3x
    float hp_mult = 1.0f + (1.0f - ctx.creature_hp_ratio) * 2.0f;

    // Bond showcase: each ally above Bond 5 adds +10%
    float bond_mult = 1.0f + ctx.ally_high_bond_count * 0.10f;

    // Philosophical resonance (story-gated bonus)
    float resonance_mult = ctx.philosophical_resonance ? 2.0f : 1.0f;

    // Corruption penalty: halves rate above 50%
    float corruption_mult = ctx.corruption_level > 0.5f ? 0.5f : 1.0f;

    // Covenant item bonus
    float item_mult = ctx.has_covenant_item ? 1.5f : 1.0f;

    float final_rate = base_rate * hp_mult * bond_mult * resonance_mult
                       * corruption_mult * item_mult;

    // Category-specific caps
    if (target.category == CreatureCategory::DeepOne) {
        final_rate = std::min(final_rate, 0.15f); // Deep Ones max at 15%
    }
    if (target.category == CreatureCategory::Hollow) {
        final_rate *= 1.5f; // Hollow are easier to tame (but can't bond deeply)
    }

    return std::clamp(final_rate, 0.01f, 0.95f);
}

bool CovenantTaming::AttemptCovenant(const CreatureData& target, const TamingContext& ctx) {
    float rate = CalculateRate(target, ctx);

    static std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    bool success = dist(rng) < rate;

    if (success) {
        // [AUDIO HOOK] Fire CreatureTamed event
        AudioEventData tame_event{AudioEvent::CreatureTamed, target.id};
        EventBus::Instance().Publish(tame_event);

        Logger::Info("Covenant formed with {} (rate was {:.1f}%)", target.name, rate * 100.0f);
    } else {
        Logger::Info("Covenant rejected by {} (rate was {:.1f}%)", target.name, rate * 100.0f);
    }

    return success;
}

} // namespace Unsuffered
