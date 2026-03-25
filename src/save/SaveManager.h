#pragma once
// ============================================================================
// Save System - JSON serialization with audio state preservation
// ============================================================================

#include "core/Types.h"
#include "audio/AudioManager.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Unsuffered {

// --- Save Data ---
struct SaveData {
    // Player state
    glm::vec3 player_position;
    RegionID current_region;
    int current_chapter;
    float play_time_hours;

    // Creature data (bonds, stats, party composition)
    struct CreatureSave {
        CreatureID id;
        int bond_depth;
        float neglect_hours;
        bool in_party;
        std::array<float, 8> current_stats;
    };
    std::vector<CreatureSave> creatures;

    // World state
    float corruption_level;
    std::vector<std::pair<QuestID, bool>> quest_states;

    // Choice history (for ending determination)
    struct ChoiceSave {
        std::string choice_id;
        std::string chosen_option;
        int chapter;
        bool is_philosophical;
        int weight; // Maps to ChoiceRecord::Philosophy
    };
    std::vector<ChoiceSave> choices;

    // [AUDIO HOOK] Audio state for seamless resume
    AudioManager::AudioState audio_state;

    // Meta
    bool is_ng_plus = false;
    int save_slot = 0;
    std::string timestamp;
};

// --- Save Manager ---
class SaveManager {
public:
    // Save to slot (JSON for dev, binary for release)
    bool SaveGame(int slot, const SaveData& data);

    // Load from slot
    // [AUDIO HOOK] After loading, calls AudioManager::RestoreState()
    // to resume music from exact playback position with 0.5s fade-in
    bool LoadGame(int slot, SaveData& out_data);

    // Quick save/load
    bool QuickSave(const SaveData& data);
    bool QuickLoad(SaveData& out_data);

    // Check if save exists
    bool SaveExists(int slot) const;

    // Delete a save
    bool DeleteSave(int slot);

    // Get save file path
    std::string GetSavePath(int slot) const;

private:
    nlohmann::json SerializeSaveData(const SaveData& data);
    SaveData DeserializeSaveData(const nlohmann::json& j);

    std::string m_save_directory = "saves/";
};

// --- Game State ---
// Central game state manager that coordinates all systems
class GameState {
public:
    enum class State { MainMenu, Exploring, InBattle, InDialogue, InMenu, Cutscene, Loading };

    void Initialize();
    void Update(double dt);

    State GetCurrentState() const { return m_state; }
    void TransitionTo(State new_state);

    // [AUDIO HOOK] State transitions trigger audio changes:
    //   MainMenu    -> Play title music
    //   Exploring   -> Play region ambience
    //   InBattle    -> CombatAudioManager::StartBattle()
    //   InDialogue  -> NarrativeAudioManager::OnDialogueOpen()
    //   Cutscene    -> NarrativeAudioManager::PlayCutscene()
    //   InMenu      -> UIAudioManager::OnMenuOpen()

    float GetPlayTimeHours() const { return m_play_time_seconds / 3600.0f; }

private:
    State m_state = State::MainMenu;
    State m_previous_state = State::MainMenu;
    double m_play_time_seconds = 0.0;
};

} // namespace Unsuffered
