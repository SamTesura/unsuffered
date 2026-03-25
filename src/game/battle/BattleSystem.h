#pragma once
// ============================================================================
// Battle System - 2v2 turn-based with Clash mechanics
// Four phases: Input -> Resolution -> Animation -> Aftermath
// ============================================================================

#include "core/Types.h"
#include "core/EventBus.h"
#include "game/creature/Creature.h"
#include <array>
#include <vector>
#include <functional>

namespace Unsuffered {

// --- Battle Action ---
struct BattleAction {
    enum class Type { Attack, CovenantBoost, Swap, Rest, Item, Flee };
    Type type;
    AbilityID ability_id = 0;
    CovenantBoost boost_type = CovenantBoost::AffirmWill;
    int target_slot = 0;  // 0 or 1 (2v2)
    int swap_creature_index = -1;
};

// --- Battle Slot (one creature in the 2v2 field) ---
struct BattleSlot {
    Creature* creature = nullptr;
    bool is_active = false;
    float posture_buildup = 0.0f;  // Offensive pressure
    int stun_turns = 0;            // From poise break
    std::vector<std::string> status_effects;
};

// --- Battle State ---
struct BattleState {
    std::array<BattleSlot, 2> player_slots;   // Grak's two creatures
    std::array<BattleSlot, 2> enemy_slots;    // Opponent pair
    int grak_cp = 5;           // Covenant Points (Grak's resource)
    int grak_max_cp = 10;
    float corruption_level;    // Inherited from world
    int turn_number = 0;
    bool eternal_return_used = false;  // Once per battle
    BattlePhase phase = BattlePhase::Input;
    RegionID region;  // For region-specific combat music
};

// --- Clash Resolver ---
class ClashResolver {
public:
    struct ClashData {
        const Ability* attack_a;
        const Ability* attack_b;
        ClashResult result;
        float damage_to_a;
        float damage_to_b;
    };

    // [AUDIO HOOK] Fires CombatAudioData with ClashOccur event
    // CombatAudioManager::OnClash() triggers 200ms silence + impact SFX
    static ClashData Resolve(const Ability& a, const Creature& creature_a,
                             const Ability& b, const Creature& creature_b);

private:
    static ClashResult DetermineWinner(float weight_a, float weight_b);
};

// --- Battle System ---
class BattleSystem {
public:
    using ActionCallback = std::function<void(const BattleAction&)>;

    void Initialize();

    // Start a new battle encounter
    // [AUDIO HOOK] Fires BattleStart -> CombatAudioManager::StartBattle(region)
    void StartBattle(std::vector<Creature*>& player_party,
                     std::vector<Creature*>& enemies,
                     float corruption, RegionID region);

    // End battle
    // [AUDIO HOOK] Fires BattleEnd -> CombatAudioManager::EndBattle(victory)
    void EndBattle(bool victory);

    // Process a turn
    void SubmitPlayerAction(int slot, const BattleAction& action);
    void SubmitGrakAction(const BattleAction& action);

    // Core damage formula
    // Bond depth affects damage: +2% per bond depth point
    // Corruption penalizes: -30% at max corruption
    float CalculateDamage(const Creature& attacker, const Creature& defender,
                          const Ability& ability);

    // Covenant Boost execution
    // [AUDIO HOOK] Fires CovenantBoostUsed -> PlayCovenantBoost(type)
    bool ExecuteCovenantBoost(CovenantBoost boost, int target_slot);

    // Process Grak's CP regeneration
    // Base: +1 per turn. Spike: +3 when ally takes critical damage.
    void RegenerateCP(bool ally_took_critical = false);

    // Check win/lose conditions
    bool IsPlayerDefeated() const;
    bool IsEnemyDefeated() const;

    // State access
    const BattleState& GetState() const { return m_state; }
    BattlePhase GetPhase() const { return m_state.phase; }

private:
    BattleState m_state;

    // Phase processing
    void ProcessResolution(const std::vector<BattleAction>& player_actions,
                           const std::vector<BattleAction>& enemy_actions);

    // [AUDIO HOOK] After damage: recalculate battle intensity
    // CombatAudioManager::SetBattleIntensity(calculated_intensity)
    float CalculateBattleIntensity() const;

    void CheckPoiseBreak(BattleSlot& slot);
    void CheckPostureBreak(BattleSlot& slot);

    // Type effectiveness table
    static float GetTypeMultiplier(Element attack, const std::array<Element, 2>& defense);

    void EmitCombatEvent(AudioEvent event, CovenantBoost boost = CovenantBoost::AffirmWill,
                         ClashResult clash = ClashResult::Draw);
};

} // namespace Unsuffered
