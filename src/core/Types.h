#pragma once
// ============================================================================
// Unsuffered - The Fallen | Core Type Definitions
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <concepts>
#include <glm/glm.hpp>

namespace Unsuffered {

// --- Entity & Resource IDs ---
using EntityID   = uint32_t;
using CreatureID = uint32_t;
using AbilityID  = uint32_t;
using RegionID   = uint16_t;
using QuestID    = uint16_t;
using AudioAssetID = std::string;

constexpr EntityID INVALID_ENTITY = 0;

// --- Creature Categories (6 ecological types, 210 total) ---
enum class CreatureCategory : uint8_t {
    Ashwalker,      // 45 - Surface survivors adapted to ash
    Remnant,        // 35 - Former divine servants, degraded
    BoundForm,      // 30 - Fused by Fallen corruption, dual-typed
    ThresholdKin,   // 25 - Live between territories, adaptive
    DeepOne,        // 15 - Ancient pre-god creatures
    Hollow          // 60 - Stripped of will, cannot bond deeply
};

// --- Element Types (mapped to Fallen domains + neutral) ---
enum class Element : uint8_t {
    Ash, Memory, Death, Love, Faith, Wealth, Connection,
    Nature, Fire, Time, Knowledge, Art, Justice, Hope,
    Identity, Void, None,
    COUNT
};

// --- Stat Types ---
enum class StatType : uint8_t {
    HP, Stamina, Poise, Posture,  // Resource bars
    ATK, DEF, SPD, WILL,          // Core stats
    COUNT
};

// --- Bond Levels (growth through relationship, not EXP) ---
enum class BondLevel : uint8_t {
    Recognition  = 1,   // 0 depth    - Basic commands
    Trust        = 2,   // 100 depth  - Ability slot 2
    Partnership  = 3,   // 300 depth  - Covenant combos
    Kinship      = 4,   // 600 depth  - Stat bonuses
    Covenant     = 5,   // 1000 depth - Ultimate ability
    Transcendence = 6   // 2000 depth - Evolution, unique ability
};

constexpr int BondDepthRequired(BondLevel level) {
    switch (level) {
        case BondLevel::Recognition:  return 0;
        case BondLevel::Trust:        return 100;
        case BondLevel::Partnership:  return 300;
        case BondLevel::Kinship:      return 600;
        case BondLevel::Covenant:     return 1000;
        case BondLevel::Transcendence:return 2000;
    }
    return 0;
}

// --- The Fourteen Fallen ---
enum class FallenID : uint8_t {
    Mourne,          // Memory   - The Forgetting Fields
    PaleShepherd,    // Death    - The Ossuary Sprawl
    DreadMother,     // Love     - The Clutching Gardens
    IronSermon,      // Faith    - The Cathedral Wastes
    Giltskin,        // Wealth   - The Coin Flats
    TheSevering,     // Connection - The Isolate Reaches
    BloomRot,        // Nature   - The Overgrown Depths
    AshenTyrant,     // Fire/War - The Scorch Marches
    StillWater,      // Time     - The Stagnant Pools
    WhisperThrone,   // Knowledge - The Library Labyrinth
    VoidSinger,      // Art      - The Silent Galleries
    ChainFather,     // Justice  - The Verdict Plains
    DreamEater,      // Hope     - The Hollow Dunes
    TheUnnamed,      // Identity - The Mirror Wastes
    COUNT
};

// --- Covenant Boost Types (Grak's combat contributions) ---
enum class CovenantBoost : uint8_t {
    AffirmWill,       // 2 CP  - +25% damage for 2 turns
    SharedSuffering,  // 3 CP  - Absorb 30% ally damage
    CovenantSurge,    // 5 CP  - Full stamina restore
    WillToOvercome,   // 4 CP  - Remove all debuffs
    BondResonance,    // 6 CP  - Synchronized attacks 1 turn
    EternalReturn     // 8 CP  - Revive at 25% HP (once/battle)
};

constexpr int CovenantBoostCost(CovenantBoost boost) {
    switch (boost) {
        case CovenantBoost::AffirmWill:      return 2;
        case CovenantBoost::SharedSuffering: return 3;
        case CovenantBoost::CovenantSurge:   return 5;
        case CovenantBoost::WillToOvercome:  return 4;
        case CovenantBoost::BondResonance:   return 6;
        case CovenantBoost::EternalReturn:   return 8;
    }
    return 0;
}

// --- Game Endings (Nietzschean philosophical mapping) ---
enum class Ending : uint8_t {
    TheLastMan,        // Passive nihilism - submission to Fallen rule
    TheHigherMan,      // Active rebellion - destroys but creates nothing
    TheUbermensch,     // Value creation - dissolves false authority
    EternalRecurrence  // Affirmation - NG+ with all bonds maxed
};

// --- Battle States ---
enum class BattlePhase : uint8_t {
    Input, Resolution, Animation, Aftermath
};

enum class AIState : uint8_t {
    Aggressive, Defensive, Support, Desperate
};

enum class ClashResult : uint8_t {
    AttackerWins, DefenderWins, Draw
};

// ============================================================================
// [AUDIO HOOK] Audio Event Types
// Every event that should trigger audio is catalogued here.
// AudioManager listens for these via EventBus.
// ============================================================================
enum class AudioEvent : uint8_t {
    // --- Region Audio ---
    RegionEnter,           // Player enters new territory
    RegionTransition,      // Crossing boundary (triggers crossfade)

    // --- Combat Audio ---
    BattleStart,           // Encounter initiated
    BattleEnd,             // Battle concluded
    BattleVictory,
    BattleDefeat,
    AttackHit,             // Damage dealt
    AttackMiss,
    ClashOccur,            // Two attacks collide (200ms silence + impact)
    CriticalHit,           // Music pause 400ms + intensity spike
    CovenantBoostUsed,     // Grak uses ability (SFX + voice line)
    CreatureFaint,

    // --- Bond Audio ---
    BondIncrease,          // Level-specific ascending jingle
    BondDecrease,          // Warning tone
    CreatureDeparture,     // Reversed bond theme (permanent leave)
    CreatureTamed,         // New covenant formed

    // --- Corruption Audio ---
    CorruptionIncrease,    // Distortion level change
    CorruptionThreshold,   // Crossed 25/50/75% boundary

    // --- Narrative Audio ---
    CutsceneStart,
    CutsceneEnd,
    DialogueOpen,
    DialogueClose,
    ChoiceMade,
    EndingTriggered,       // Ending-specific musical finale

    // --- UI Audio ---
    MenuOpen,
    MenuClose,
    SaveGame,              // Save confirmation tone
    LoadGame,

    // --- World Audio ---
    Footstep,              // Surface-type dependent
    DoorOpen,
    ItemPickup,
    AmbientTrigger         // Environmental one-shots
};

// --- C++20 Concept: Tameable creatures ---
template<typename T>
concept Tameable = requires(T t) {
    { t.GetWillToPower() } -> std::convertible_to<float>;
    { t.CanAcceptCovenant() } -> std::convertible_to<bool>;
    { t.GetCategory() } -> std::convertible_to<CreatureCategory>;
};

// --- Event callback ---
using EventCallback = std::function<void(const void* data)>;

// --- Audio event data structures ---
struct AudioEventData {
    AudioEvent event;
    EntityID source_entity = INVALID_ENTITY;
    glm::vec3 position = {0, 0, 0};
    float intensity = 1.0f;
    std::string asset_override = "";
};

struct RegionAudioData {
    RegionID from_region;
    RegionID to_region;
    float crossfade_duration = 3.0f;
};

struct CombatAudioData {
    AudioEvent event;
    CovenantBoost boost_type;
    ClashResult clash_result;
    float battle_intensity = 0.5f;
};

struct BondAudioData {
    CreatureID creature;
    BondLevel old_level;
    BondLevel new_level;
    bool is_departure = false;
};

} // namespace Unsuffered
