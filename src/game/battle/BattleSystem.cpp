// ============================================================================
// Battle System Implementation
// 2v2 turn-based combat with Clash mechanics, Covenant Boosts
// ============================================================================

#include "game/battle/BattleSystem.h"
#include "core/Logger.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Unsuffered {

static std::mt19937 s_rng{std::random_device{}()};

// ============================================================================
// Clash Resolver
// ============================================================================

ClashResolver::ClashData ClashResolver::Resolve(
    const Ability& a, const Creature& creature_a,
    const Ability& b, const Creature& creature_b)
{
    ClashData result;
    result.attack_a = &a;
    result.attack_b = &b;
    result.result = DetermineWinner(a.weight, b.weight);

    // [AUDIO HOOK] 200ms silence before clash impact
    CombatAudioData audio{AudioEvent::ClashOccur, CovenantBoost::AffirmWill, result.result, 0.8f};
    EventBus::Instance().Publish(audio);

    float base_damage_a = a.power * (creature_a.GetStat(a.stat_type) / 50.0f);
    float base_damage_b = b.power * (creature_b.GetStat(b.stat_type) / 50.0f);

    switch (result.result) {
        case ClashResult::AttackerWins:
            result.damage_to_a = base_damage_b * 0.3f; // Reduced damage to winner
            result.damage_to_b = base_damage_a * 1.2f; // Bonus damage to loser
            break;
        case ClashResult::DefenderWins:
            result.damage_to_a = base_damage_b * 1.2f;
            result.damage_to_b = base_damage_a * 0.3f;
            break;
        case ClashResult::Draw:
            result.damage_to_a = base_damage_b * 0.5f;
            result.damage_to_b = base_damage_a * 0.5f;
            break;
    }

    Logger::Debug("Clash resolved: {} (w:{:.1f}) vs {} (w:{:.1f}) -> {}",
                  a.name, a.weight, b.name, b.weight,
                  result.result == ClashResult::Draw ? "Draw" :
                  result.result == ClashResult::AttackerWins ? "A wins" : "B wins");
    return result;
}

ClashResult ClashResolver::DetermineWinner(float weight_a, float weight_b) {
    float diff = weight_a - weight_b;
    if (std::abs(diff) < 0.5f) return ClashResult::Draw;
    return diff > 0 ? ClashResult::AttackerWins : ClashResult::DefenderWins;
}

// ============================================================================
// Battle System
// ============================================================================

void BattleSystem::Initialize() {
    m_state = BattleState{};
    Logger::Info("BattleSystem initialized");
}

void BattleSystem::StartBattle(
    std::vector<Creature*>& player_party,
    std::vector<Creature*>& enemies,
    float corruption, RegionID region)
{
    m_state = BattleState{};
    m_state.corruption_level = corruption;
    m_state.region = region;
    m_state.grak_cp = 5;

    // Fill player slots (up to 2)
    for (size_t i = 0; i < std::min(player_party.size(), size_t(2)); ++i) {
        m_state.player_slots[i].creature = player_party[i];
        m_state.player_slots[i].is_active = true;
    }

    // Fill enemy slots (up to 2)
    for (size_t i = 0; i < std::min(enemies.size(), size_t(2)); ++i) {
        m_state.enemy_slots[i].creature = enemies[i];
        m_state.enemy_slots[i].is_active = true;
    }

    m_state.phase = BattlePhase::Input;

    // [AUDIO HOOK] Fire BattleStart -> CombatAudioManager::StartBattle(region)
    CombatAudioData audio{AudioEvent::BattleStart, CovenantBoost::AffirmWill,
                          ClashResult::Draw, 0.5f};
    EventBus::Instance().Publish(audio);

    Logger::Info("Battle started in region {} | Corruption: {:.1f}%",
                 region, corruption * 100.0f);
}

void BattleSystem::EndBattle(bool victory) {
    AudioEvent event = victory ? AudioEvent::BattleVictory : AudioEvent::BattleDefeat;
    CombatAudioData audio{event, CovenantBoost::AffirmWill, ClashResult::Draw, 0.0f};
    EventBus::Instance().Publish(audio);

    Logger::Info("Battle ended: {}", victory ? "VICTORY" : "DEFEAT");
}

void BattleSystem::SubmitPlayerAction(int slot, const BattleAction& action) {
    // Store action for resolution phase
    // In full implementation, this queues the action
    Logger::Debug("Player action submitted for slot {}: type {}",
                  slot, static_cast<int>(action.type));
}

void BattleSystem::SubmitGrakAction(const BattleAction& action) {
    if (action.type == BattleAction::Type::CovenantBoost) {
        ExecuteCovenantBoost(action.boost_type, action.target_slot);
    }
}

float BattleSystem::CalculateDamage(
    const Creature& attacker, const Creature& defender, const Ability& ability)
{
    float base = ability.power * (attacker.GetStat(ability.stat_type) / 50.0f);
    float defense = defender.GetStat(StatType::DEF) / 100.0f;
    float type_mult = GetTypeMultiplier(ability.element, defender.GetTypes());
    float bond_mult = 1.0f + (attacker.GetBondDepth() * 0.02f);
    float corruption_penalty = 1.0f - (m_state.corruption_level * 0.3f);

    // Random variance +/- 10%
    std::uniform_real_distribution<float> dist(0.9f, 1.1f);
    float variance = dist(s_rng);

    float damage = base * (1.0f - defense) * type_mult * bond_mult
                   * corruption_penalty * variance;

    // Critical hit check (5% base, +1% per bond depth above 500)
    float crit_chance = 0.05f + std::max(0, attacker.GetBondDepth() - 500) * 0.01f;
    std::uniform_real_distribution<float> crit_dist(0.0f, 1.0f);
    if (crit_dist(s_rng) < crit_chance) {
        damage *= 1.5f;
        // [AUDIO HOOK] Critical hit: 400ms music pause + intensity spike
        CombatAudioData audio{AudioEvent::CriticalHit, CovenantBoost::AffirmWill,
                              ClashResult::Draw, 1.0f};
        EventBus::Instance().Publish(audio);
        Logger::Debug("CRITICAL HIT! Damage: {:.1f}", damage);
    }

    return std::max(1.0f, damage);
}

bool BattleSystem::ExecuteCovenantBoost(CovenantBoost boost, int target_slot) {
    int cost = CovenantBoostCost(boost);
    if (m_state.grak_cp < cost) {
        Logger::Warn("Not enough CP for {} (need {}, have {})",
                     static_cast<int>(boost), cost, m_state.grak_cp);
        return false;
    }

    // Check EternalReturn once-per-battle restriction
    if (boost == CovenantBoost::EternalReturn && m_state.eternal_return_used) {
        Logger::Warn("Eternal Return already used this battle");
        return false;
    }

    m_state.grak_cp -= cost;
    auto& slot = m_state.player_slots[target_slot];

    switch (boost) {
        case CovenantBoost::AffirmWill:
            // +25% damage for 2 turns (implemented via status effect)
            slot.status_effects.push_back("affirm_will_2");
            Logger::Info("Affirm Will applied to slot {}", target_slot);
            break;

        case CovenantBoost::SharedSuffering:
            // Grak absorbs 30% of ally damage (tracked in damage resolution)
            slot.status_effects.push_back("shared_suffering");
            Logger::Info("Shared Suffering active on slot {}", target_slot);
            break;

        case CovenantBoost::CovenantSurge:
            if (slot.creature) {
                float max_stam = slot.creature->GetStat(StatType::Stamina);
                slot.creature->RestoreStamina(max_stam);
                Logger::Info("Covenant Surge: full stamina restored for {}",
                             slot.creature->GetName());
            }
            break;

        case CovenantBoost::WillToOvercome:
            slot.status_effects.clear();
            slot.stun_turns = 0;
            Logger::Info("Will to Overcome: all debuffs cleared from slot {}", target_slot);
            break;

        case CovenantBoost::BondResonance:
            // Both allies get synced attacks for 1 turn
            m_state.player_slots[0].status_effects.push_back("bond_resonance_1");
            m_state.player_slots[1].status_effects.push_back("bond_resonance_1");
            Logger::Info("Bond Resonance: synchronized attacks active");
            break;

        case CovenantBoost::EternalReturn:
            if (slot.creature && slot.creature->IsFainted()) {
                float max_hp = slot.creature->GetMaxHP();
                slot.creature->Heal(max_hp * 0.25f);
                slot.is_active = true;
                m_state.eternal_return_used = true;
                Logger::Info("Eternal Return: {} revived at 25% HP",
                             slot.creature->GetName());
            }
            break;
    }

    // [AUDIO HOOK] Covenant Boost SFX + Grak voice line
    CombatAudioData audio{AudioEvent::CovenantBoostUsed, boost,
                          ClashResult::Draw, CalculateBattleIntensity()};
    EventBus::Instance().Publish(audio);

    return true;
}

void BattleSystem::RegenerateCP(bool ally_took_critical) {
    m_state.grak_cp = std::min(m_state.grak_max_cp,
                                m_state.grak_cp + 1 + (ally_took_critical ? 3 : 0));
}

bool BattleSystem::IsPlayerDefeated() const {
    return (!m_state.player_slots[0].is_active || 
            (m_state.player_slots[0].creature && m_state.player_slots[0].creature->IsFainted()))
        && (!m_state.player_slots[1].is_active ||
            (m_state.player_slots[1].creature && m_state.player_slots[1].creature->IsFainted()));
}

bool BattleSystem::IsEnemyDefeated() const {
    return (!m_state.enemy_slots[0].is_active ||
            (m_state.enemy_slots[0].creature && m_state.enemy_slots[0].creature->IsFainted()))
        && (!m_state.enemy_slots[1].is_active ||
            (m_state.enemy_slots[1].creature && m_state.enemy_slots[1].creature->IsFainted()));
}

void BattleSystem::ProcessResolution(
    const std::vector<BattleAction>& player_actions,
    const std::vector<BattleAction>& enemy_actions)
{
    m_state.phase = BattlePhase::Resolution;
    m_state.turn_number++;

    // Sort all actions by speed for turn order
    // Check for clashes (simultaneous attacks on same target)
    // Apply damage, status effects, poise/posture changes
    // This is the core combat loop - simplified here

    for (size_t i = 0; i < player_actions.size() && i < 2; ++i) {
        const auto& action = player_actions[i];
        if (action.type == BattleAction::Type::Attack) {
            // [AUDIO HOOK] AttackHit event fires after damage calc
            AudioEventData hit_event{AudioEvent::AttackHit};
            EventBus::Instance().Publish(hit_event);
        }
    }

    // Update battle intensity for dynamic audio
    float intensity = CalculateBattleIntensity();
    CombatAudioData intensity_update{AudioEvent::BattleStart, CovenantBoost::AffirmWill,
                                     ClashResult::Draw, intensity};
    EventBus::Instance().Publish(intensity_update);

    // Regenerate CP
    RegenerateCP(false);

    m_state.phase = BattlePhase::Animation;
}

float BattleSystem::CalculateBattleIntensity() const {
    // Intensity based on:
    // - Lowest HP ratio across all active creatures
    // - Turn number (ramps up over time)
    // - Whether anyone has fainted
    float min_hp_ratio = 1.0f;
    int faint_count = 0;

    for (const auto& slot : m_state.player_slots) {
        if (slot.creature && slot.is_active) {
            float ratio = slot.creature->GetHP() / slot.creature->GetMaxHP();
            min_hp_ratio = std::min(min_hp_ratio, ratio);
        }
        if (slot.creature && slot.creature->IsFainted()) faint_count++;
    }
    for (const auto& slot : m_state.enemy_slots) {
        if (slot.creature && slot.is_active) {
            float ratio = slot.creature->GetHP() / slot.creature->GetMaxHP();
            min_hp_ratio = std::min(min_hp_ratio, ratio);
        }
        if (slot.creature && slot.creature->IsFainted()) faint_count++;
    }

    float hp_intensity = 1.0f - min_hp_ratio; // Lower HP = higher intensity
    float turn_intensity = std::min(1.0f, m_state.turn_number / 20.0f);
    float faint_intensity = faint_count * 0.2f;

    return std::clamp(hp_intensity * 0.5f + turn_intensity * 0.3f + faint_intensity, 0.0f, 1.0f);
}

void BattleSystem::CheckPoiseBreak(BattleSlot& slot) {
    if (slot.creature && slot.creature->GetStat(StatType::Poise) <= 0.0f) {
        slot.stun_turns = 2;
        Logger::Info("{} poise broken! Stunned for 2 turns", slot.creature->GetName());
    }
}

void BattleSystem::CheckPostureBreak(BattleSlot& slot) {
    if (slot.posture_buildup >= 100.0f) {
        slot.posture_buildup = 0.0f;
        // Critical window: next attack deals 2x damage
        slot.status_effects.push_back("posture_broken_1");
        Logger::Info("{} posture broken! Critical window open", slot.creature->GetName());
    }
}

float BattleSystem::GetTypeMultiplier(Element attack,
                                       const std::array<Element, 2>& defense)
{
    // Simplified type chart based on Fallen domain relationships
    // Each Fallen domain is weak to its philosophical opposite:
    //   Memory <-> Identity, Death <-> Hope, Love <-> Connection (severing)
    //   Faith <-> Art, Wealth <-> Justice, Nature <-> Fire
    //   Time <-> Knowledge
    auto is_super_effective = [](Element atk, Element def) -> bool {
        switch (atk) {
            case Element::Memory:     return def == Element::Identity;
            case Element::Identity:   return def == Element::Memory;
            case Element::Death:      return def == Element::Hope;
            case Element::Hope:       return def == Element::Death;
            case Element::Love:       return def == Element::Connection;
            case Element::Connection: return def == Element::Love;
            case Element::Faith:      return def == Element::Art;
            case Element::Art:        return def == Element::Faith;
            case Element::Wealth:     return def == Element::Justice;
            case Element::Justice:    return def == Element::Wealth;
            case Element::Nature:     return def == Element::Fire;
            case Element::Fire:       return def == Element::Nature;
            case Element::Time:       return def == Element::Knowledge;
            case Element::Knowledge:  return def == Element::Time;
            default: return false;
        }
    };

    float mult = 1.0f;
    for (Element def : defense) {
        if (def == Element::None) continue;
        if (is_super_effective(attack, def)) mult *= 1.5f;
        if (is_super_effective(def, attack)) mult *= 0.67f;
    }
    return mult;
}

void BattleSystem::EmitCombatEvent(AudioEvent event, CovenantBoost boost,
                                    ClashResult clash)
{
    CombatAudioData audio{event, boost, clash, CalculateBattleIntensity()};
    EventBus::Instance().Publish(audio);
}

} // namespace Unsuffered
