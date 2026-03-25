#pragma once
// ============================================================================
// AI System - Tiered approach for performance-conscious AI
// Tier 0: Idle creatures (0.01ms) - Simple FSM
// Tier 1: Battle opponents (0.1ms) - Weighted random FSM
// Tier 2: Elite encounters (0.5ms) - Behavior tree with pattern memory
// Tier 3: Fallen bosses (1.0ms) - Full BT + player pattern analysis
// Total budget: 2ms per frame on minimum spec
// ============================================================================

#include "core/Types.h"
#include "game/creature/Creature.h"
#include "game/battle/BattleSystem.h"
#include <vector>
#include <memory>
#include <random>

namespace Unsuffered {

// --- World AI States (Tier 0) ---
enum class WorldAIState { Idle, Wander, Flee, Alert, Patrol };

// --- World AI Controller (Tier 0: 0.01ms budget) ---
class WorldAIController {
public:
    void Update(double dt, const glm::vec3& player_pos);
    WorldAIState GetState() const { return m_state; }

    void SetPosition(const glm::vec3& pos) { m_position = pos; }
    glm::vec3 GetPosition() const { return m_position; }

    float GetDetectionRange() const { return m_detection_range; }
    void SetDetectionRange(float range) { m_detection_range = range; }

private:
    WorldAIState m_state = WorldAIState::Idle;
    glm::vec3 m_position{0};
    glm::vec3 m_wander_target{0};
    float m_detection_range = 15.0f;
    float m_state_timer = 0.0f;

    void TransitionTo(WorldAIState state);
};

// --- Battle FSM (Tier 1: 0.1ms budget) ---
class BattleFSM {
public:
    struct BattleContext {
        const BattleSlot& self;
        const BattleSlot& ally;
        const BattleSlot& enemy_a;
        const BattleSlot& enemy_b;
        float corruption_level;
        int turn_number;
    };

    BattleAction Evaluate(const BattleContext& ctx);

private:
    AIState m_state = AIState::Aggressive;

    void UpdateState(const BattleContext& ctx);
    BattleAction ChooseHighestDamage(const BattleContext& ctx);
    BattleAction ChooseDefenseOrHeal(const BattleContext& ctx);
    BattleAction ChooseSupportAlly(const BattleContext& ctx);
    BattleAction ChooseLastResort(const BattleContext& ctx);

    float GetHPRatio(const BattleSlot& slot) const;

    std::mt19937 m_rng{std::random_device{}()};
};

// --- Behavior Tree Node (Tier 2-3) ---
enum class BTStatus { Running, Success, Failure };

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus Tick(const BattleFSM::BattleContext& ctx) = 0;
};

class BTSequence : public BTNode {
public:
    void AddChild(std::unique_ptr<BTNode> child) { m_children.push_back(std::move(child)); }
    BTStatus Tick(const BattleFSM::BattleContext& ctx) override;

private:
    std::vector<std::unique_ptr<BTNode>> m_children;
    size_t m_current = 0;
};

class BTSelector : public BTNode {
public:
    void AddChild(std::unique_ptr<BTNode> child) { m_children.push_back(std::move(child)); }
    BTStatus Tick(const BattleFSM::BattleContext& ctx) override;

private:
    std::vector<std::unique_ptr<BTNode>> m_children;
    size_t m_current = 0;
};

// --- Pattern Memory (Tier 3: Fallen bosses analyze player behavior) ---
class PatternMemory {
public:
    void RecordPlayerAction(const BattleAction& action, int turn);
    float PredictActionProbability(BattleAction::Type type) const;
    BattleAction::Type PredictNextAction() const;

private:
    struct ActionRecord {
        BattleAction::Type type;
        int turn;
    };
    std::vector<ActionRecord> m_history;
    static constexpr int MEMORY_WINDOW = 10;
};

// --- Elite AI Controller (Tier 2: Behavior Tree) ---
class EliteAIController {
public:
    void Initialize(int difficulty_tier); // 2 or 3
    BattleAction Evaluate(const BattleFSM::BattleContext& ctx);

private:
    std::unique_ptr<BTNode> m_root;
    PatternMemory m_pattern_memory;  // Only for Tier 3
    int m_tier = 2;
};

} // namespace Unsuffered
