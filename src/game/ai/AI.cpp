// ============================================================================
// AI System Implementation
// Tiered AI: FSM (Tier 0-1), Behavior Trees (Tier 2-3), Pattern Memory
// Total CPU budget: 2ms per frame on minimum spec
// ============================================================================

#include "game/ai/AI.h"
#include "core/Logger.h"
#include <algorithm>
#include <cmath>

namespace Unsuffered {

// ============================================================================
// World AI Controller (Tier 0: ~0.01ms per creature)
// ============================================================================

void WorldAIController::Update(double dt, const glm::vec3& player_pos) {
    float dist_to_player = glm::distance(m_position, player_pos);
    m_state_timer += static_cast<float>(dt);

    switch (m_state) {
        case WorldAIState::Idle:
            if (dist_to_player < m_detection_range) {
                TransitionTo(WorldAIState::Alert);
            } else if (m_state_timer > 5.0f) {
                TransitionTo(WorldAIState::Wander);
            }
            break;

        case WorldAIState::Wander: {
            glm::vec3 dir = glm::normalize(m_wander_target - m_position);
            m_position += dir * 2.0f * static_cast<float>(dt);
            float dist = glm::distance(m_position, m_wander_target);
            if (dist < 1.0f || m_state_timer > 8.0f) {
                TransitionTo(WorldAIState::Idle);
            }
            if (dist_to_player < m_detection_range) {
                TransitionTo(WorldAIState::Alert);
            }
            break;
        }

        case WorldAIState::Alert:
            if (dist_to_player > m_detection_range * 1.5f) {
                TransitionTo(WorldAIState::Idle);
            } else if (dist_to_player < m_detection_range * 0.5f) {
                TransitionTo(WorldAIState::Flee);
            }
            break;

        case WorldAIState::Flee: {
            glm::vec3 away = glm::normalize(m_position - player_pos);
            m_position += away * 4.0f * static_cast<float>(dt);
            if (dist_to_player > m_detection_range * 2.0f) {
                TransitionTo(WorldAIState::Idle);
            }
            break;
        }

        case WorldAIState::Patrol:
            // Patrol follows pre-defined waypoints (simplified)
            if (m_state_timer > 10.0f) {
                TransitionTo(WorldAIState::Idle);
            }
            break;
    }
}

void WorldAIController::TransitionTo(WorldAIState state) {
    m_state = state;
    m_state_timer = 0.0f;

    if (state == WorldAIState::Wander) {
        // Random wander target within 20 units
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(-20.0f, 20.0f);
        m_wander_target = m_position + glm::vec3(dist(rng), 0, dist(rng));
    }
}

// ============================================================================
// Battle FSM (Tier 1: ~0.1ms per creature)
// ============================================================================

BattleAction BattleFSM::Evaluate(const BattleContext& ctx) {
    UpdateState(ctx);

    switch (m_state) {
        case AIState::Aggressive:  return ChooseHighestDamage(ctx);
        case AIState::Defensive:   return ChooseDefenseOrHeal(ctx);
        case AIState::Support:     return ChooseSupportAlly(ctx);
        case AIState::Desperate:   return ChooseLastResort(ctx);
    }
    return ChooseHighestDamage(ctx); // Fallback
}

void BattleFSM::UpdateState(const BattleContext& ctx) {
    float self_hp = GetHPRatio(ctx.self);
    float ally_hp = GetHPRatio(ctx.ally);

    if (self_hp < 0.2f) {
        m_state = AIState::Desperate;
    } else if (self_hp < 0.5f) {
        m_state = AIState::Defensive;
    } else if (ally_hp < 0.3f && ctx.ally.is_active) {
        m_state = AIState::Support;
    } else {
        m_state = AIState::Aggressive;
    }
}

BattleAction BattleFSM::ChooseHighestDamage(const BattleContext& ctx) {
    BattleAction action;
    action.type = BattleAction::Type::Attack;
    action.ability_id = 0; // First available ability

    if (ctx.self.creature) {
        auto abilities = ctx.self.creature->GetAvailableAbilities();
        if (!abilities.empty()) {
            // Pick highest power ability that creature has stamina for
            const Ability* best = abilities[0];
            for (const auto* ab : abilities) {
                if (ab->power > best->power &&
                    ctx.self.creature->GetStat(StatType::Stamina) >= ab->stamina_cost) {
                    best = ab;
                }
            }
            action.ability_id = best->id;
        }
    }

    // Target the lower-HP enemy
    float hp_a = GetHPRatio(ctx.enemy_a);
    float hp_b = GetHPRatio(ctx.enemy_b);
    action.target_slot = (hp_a <= hp_b) ? 0 : 1;

    return action;
}

BattleAction BattleFSM::ChooseDefenseOrHeal(const BattleContext& ctx) {
    BattleAction action;

    // 60% chance to rest (restore stamina), 40% to attack weakest
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(m_rng) < 0.6f) {
        action.type = BattleAction::Type::Rest;
    } else {
        action = ChooseHighestDamage(ctx);
    }
    return action;
}

BattleAction BattleFSM::ChooseSupportAlly(const BattleContext& ctx) {
    // If ally is low, try to take pressure off by attacking their attacker
    BattleAction action;
    action.type = BattleAction::Type::Attack;
    action.target_slot = 0; // Attack front-line enemy
    if (ctx.self.creature) {
        auto abilities = ctx.self.creature->GetAvailableAbilities();
        if (!abilities.empty()) {
            action.ability_id = abilities[0]->id;
        }
    }
    return action;
}

BattleAction BattleFSM::ChooseLastResort(const BattleContext& ctx) {
    BattleAction action;

    if (ctx.self.creature) {
        auto abilities = ctx.self.creature->GetAvailableAbilities();
        // Use the heaviest/most powerful attack regardless of stamina cost
        const Ability* heaviest = nullptr;
        for (const auto* ab : abilities) {
            if (!heaviest || ab->power > heaviest->power) {
                heaviest = ab;
            }
        }
        if (heaviest) {
            action.type = BattleAction::Type::Attack;
            action.ability_id = heaviest->id;
            // Target whichever enemy is closest to fainting
            action.target_slot = (GetHPRatio(ctx.enemy_a) <= GetHPRatio(ctx.enemy_b)) ? 0 : 1;
        } else {
            action.type = BattleAction::Type::Rest;
        }
    }
    return action;
}

float BattleFSM::GetHPRatio(const BattleSlot& slot) const {
    if (!slot.creature || !slot.is_active) return 0.0f;
    return slot.creature->GetHP() / slot.creature->GetMaxHP();
}

// ============================================================================
// Behavior Tree Nodes (Tier 2-3)
// ============================================================================

BTStatus BTSequence::Tick(const BattleFSM::BattleContext& ctx) {
    while (m_current < m_children.size()) {
        BTStatus status = m_children[m_current]->Tick(ctx);
        if (status == BTStatus::Running) return BTStatus::Running;
        if (status == BTStatus::Failure) {
            m_current = 0;
            return BTStatus::Failure;
        }
        m_current++;
    }
    m_current = 0;
    return BTStatus::Success;
}

BTStatus BTSelector::Tick(const BattleFSM::BattleContext& ctx) {
    while (m_current < m_children.size()) {
        BTStatus status = m_children[m_current]->Tick(ctx);
        if (status == BTStatus::Running) return BTStatus::Running;
        if (status == BTStatus::Success) {
            m_current = 0;
            return BTStatus::Success;
        }
        m_current++;
    }
    m_current = 0;
    return BTStatus::Failure;
}

// ============================================================================
// Pattern Memory (Tier 3: Fallen boss AI)
// ============================================================================

void PatternMemory::RecordPlayerAction(const BattleAction& action, int turn) {
    m_history.push_back({action.type, turn});

    // Trim to memory window
    if (m_history.size() > MEMORY_WINDOW) {
        m_history.erase(m_history.begin());
    }
}

float PatternMemory::PredictActionProbability(BattleAction::Type type) const {
    if (m_history.empty()) return 0.0f;

    int count = 0;
    for (const auto& record : m_history) {
        if (record.type == type) count++;
    }
    return static_cast<float>(count) / static_cast<float>(m_history.size());
}

BattleAction::Type PatternMemory::PredictNextAction() const {
    if (m_history.empty()) return BattleAction::Type::Attack;

    // Count occurrences of each action type
    std::unordered_map<int, int> counts;
    for (const auto& record : m_history) {
        counts[static_cast<int>(record.type)]++;
    }

    // Return most frequent
    int best_type = static_cast<int>(BattleAction::Type::Attack);
    int best_count = 0;
    for (const auto& [type, count] : counts) {
        if (count > best_count) {
            best_count = count;
            best_type = type;
        }
    }
    return static_cast<BattleAction::Type>(best_type);
}

// ============================================================================
// Elite AI Controller (Tier 2-3)
// ============================================================================

void EliteAIController::Initialize(int difficulty_tier) {
    m_tier = difficulty_tier;

    // Build behavior tree based on tier
    auto root = std::make_unique<BTSelector>();

    // Tier 2+3: Basic tactical awareness
    // Tier 3: Adds pattern memory counter-play

    m_root = std::move(root);

    Logger::Info("EliteAIController initialized at tier {}", m_tier);
}

BattleAction EliteAIController::Evaluate(const BattleFSM::BattleContext& ctx) {
    // For Tier 3 (Fallen bosses), use pattern prediction to counter player
    if (m_tier >= 3) {
        auto predicted = m_pattern_memory.PredictNextAction();

        // Counter strategy:
        // If player attacks a lot -> go defensive / use heavy weight attacks for clashes
        // If player rests a lot -> be aggressive
        // If player swaps a lot -> target the incoming creature
        if (predicted == BattleAction::Type::Attack) {
            // Choose heavy-weight ability to win clashes
            BattleAction action;
            action.type = BattleAction::Type::Attack;
            if (ctx.self.creature) {
                auto abilities = ctx.self.creature->GetAvailableAbilities();
                const Ability* heaviest = nullptr;
                for (const auto* ab : abilities) {
                    if (!heaviest || ab->weight > heaviest->weight) {
                        heaviest = ab;
                    }
                }
                if (heaviest) action.ability_id = heaviest->id;
            }
            action.target_slot = 0;
            return action;
        }
    }

    // Fall back to FSM for simpler decisions
    BattleFSM fsm;
    return fsm.Evaluate(ctx);
}

} // namespace Unsuffered
