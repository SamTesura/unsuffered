// ============================================================================
// Save System & GameState Implementation
// JSON serialization with audio state preservation
// ============================================================================

#include "save/SaveManager.h"
#include "core/Logger.h"
#include <fstream>
#include <filesystem>
#include <chrono>

namespace Unsuffered {

// ============================================================================
// Save Manager
// ============================================================================

bool SaveManager::SaveGame(int slot, const SaveData& data) {
    try {
        std::filesystem::create_directories(m_save_directory);
        std::string path = GetSavePath(slot);

        nlohmann::json j = SerializeSaveData(data);

        std::ofstream file(path);
        if (!file.is_open()) {
            Logger::Error("Failed to open save file: {}", path);
            return false;
        }
        file << j.dump(2);

        // [AUDIO HOOK] Fire SaveGame event for confirmation tone
        AudioEventData event{AudioEvent::SaveGame};
        EventBus::Instance().Publish(event);

        Logger::Info("Game saved to slot {} (playtime: {:.1f}h)", slot, data.play_time_hours);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Save failed: {}", e.what());
        return false;
    }
}

bool SaveManager::LoadGame(int slot, SaveData& out_data) {
    try {
        std::string path = GetSavePath(slot);
        std::ifstream file(path);
        if (!file.is_open()) {
            Logger::Error("Save file not found: {}", path);
            return false;
        }

        nlohmann::json j;
        file >> j;
        out_data = DeserializeSaveData(j);

        // [AUDIO HOOK] Fire LoadGame event
        // AudioManager::RestoreState() will resume music from saved position
        AudioEventData event{AudioEvent::LoadGame};
        EventBus::Instance().Publish(event);

        Logger::Info("Game loaded from slot {} (chapter: {}, playtime: {:.1f}h)",
                     slot, out_data.current_chapter, out_data.play_time_hours);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Load failed: {}", e.what());
        return false;
    }
}

bool SaveManager::QuickSave(const SaveData& data) {
    return SaveGame(99, data); // Slot 99 = quicksave
}

bool SaveManager::QuickLoad(SaveData& out_data) {
    return LoadGame(99, out_data);
}

bool SaveManager::SaveExists(int slot) const {
    return std::filesystem::exists(GetSavePath(slot));
}

bool SaveManager::DeleteSave(int slot) {
    std::string path = GetSavePath(slot);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        Logger::Info("Save slot {} deleted", slot);
        return true;
    }
    return false;
}

std::string SaveManager::GetSavePath(int slot) const {
    return m_save_directory + "save_" + std::to_string(slot) + ".json";
}

nlohmann::json SaveManager::SerializeSaveData(const SaveData& data) {
    nlohmann::json j;

    // Player state
    j["player_position"] = {data.player_position.x, data.player_position.y, data.player_position.z};
    j["current_region"] = data.current_region;
    j["current_chapter"] = data.current_chapter;
    j["play_time_hours"] = data.play_time_hours;
    j["is_ng_plus"] = data.is_ng_plus;
    j["corruption_level"] = data.corruption_level;

    // Creatures
    nlohmann::json creatures_json = nlohmann::json::array();
    for (const auto& c : data.creatures) {
        nlohmann::json cj;
        cj["id"] = c.id;
        cj["bond_depth"] = c.bond_depth;
        cj["neglect_hours"] = c.neglect_hours;
        cj["in_party"] = c.in_party;
        cj["stats"] = c.current_stats;
        creatures_json.push_back(cj);
    }
    j["creatures"] = creatures_json;

    // Quest states
    nlohmann::json quests_json = nlohmann::json::array();
    for (const auto& [id, complete] : data.quest_states) {
        quests_json.push_back({{"id", id}, {"complete", complete}});
    }
    j["quests"] = quests_json;

    // Choice history
    nlohmann::json choices_json = nlohmann::json::array();
    for (const auto& c : data.choices) {
        choices_json.push_back({
            {"choice_id", c.choice_id},
            {"chosen_option", c.chosen_option},
            {"chapter", c.chapter},
            {"is_philosophical", c.is_philosophical},
            {"weight", c.weight}
        });
    }
    j["choices"] = choices_json;

    // [AUDIO HOOK] Audio state for seamless resume
    j["audio_state"] = {
        {"music_track", data.audio_state.current_music_track},
        {"music_position", data.audio_state.music_position},
        {"ambience_track", data.audio_state.current_ambience_track},
        {"corruption_level", data.audio_state.corruption_level}
    };

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    j["timestamp"] = std::ctime(&time_t);

    return j;
}

SaveData SaveManager::DeserializeSaveData(const nlohmann::json& j) {
    SaveData data;

    auto& pos = j["player_position"];
    data.player_position = {pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>()};
    data.current_region = j["current_region"].get<RegionID>();
    data.current_chapter = j["current_chapter"].get<int>();
    data.play_time_hours = j["play_time_hours"].get<float>();
    data.is_ng_plus = j.value("is_ng_plus", false);
    data.corruption_level = j.value("corruption_level", 0.0f);

    // Creatures
    for (const auto& cj : j["creatures"]) {
        SaveData::CreatureSave cs;
        cs.id = cj["id"].get<CreatureID>();
        cs.bond_depth = cj["bond_depth"].get<int>();
        cs.neglect_hours = cj["neglect_hours"].get<float>();
        cs.in_party = cj["in_party"].get<bool>();
        cs.current_stats = cj["stats"].get<std::array<float, 8>>();
        data.creatures.push_back(cs);
    }

    // Quests
    for (const auto& qj : j["quests"]) {
        data.quest_states.emplace_back(qj["id"].get<QuestID>(), qj["complete"].get<bool>());
    }

    // Choices
    for (const auto& cj : j["choices"]) {
        SaveData::ChoiceSave cs;
        cs.choice_id = cj["choice_id"].get<std::string>();
        cs.chosen_option = cj["chosen_option"].get<std::string>();
        cs.chapter = cj["chapter"].get<int>();
        cs.is_philosophical = cj["is_philosophical"].get<bool>();
        cs.weight = cj["weight"].get<int>();
        data.choices.push_back(cs);
    }

    // Audio state
    if (j.contains("audio_state")) {
        auto& as = j["audio_state"];
        data.audio_state.current_music_track = as.value("music_track", "");
        data.audio_state.music_position = as.value("music_position", 0.0f);
        data.audio_state.current_ambience_track = as.value("ambience_track", "");
        data.audio_state.corruption_level = as.value("corruption_level", 0.0f);
    }

    return data;
}

// ============================================================================
// Game State
// ============================================================================

void GameState::Initialize() {
    m_state = State::MainMenu;
    m_play_time_seconds = 0.0;
    Logger::Info("GameState initialized");
}

void GameState::Update(double dt) {
    // Track play time (only during gameplay states)
    if (m_state == State::Exploring || m_state == State::InBattle ||
        m_state == State::InDialogue) {
        m_play_time_seconds += dt;
    }
}

void GameState::TransitionTo(State new_state) {
    m_previous_state = m_state;
    m_state = new_state;

    // [AUDIO HOOK] State transitions drive audio changes
    switch (new_state) {
        case State::MainMenu: {
            AudioEventData e{AudioEvent::MenuOpen};
            EventBus::Instance().Publish(e);
            break;
        }
        case State::Exploring: {
            AudioEventData e{AudioEvent::RegionEnter};
            EventBus::Instance().Publish(e);
            break;
        }
        case State::InBattle: {
            // Battle start is handled by BattleSystem::StartBattle()
            break;
        }
        case State::InDialogue: {
            AudioEventData e{AudioEvent::DialogueOpen};
            EventBus::Instance().Publish(e);
            break;
        }
        case State::Cutscene: {
            AudioEventData e{AudioEvent::CutsceneStart};
            EventBus::Instance().Publish(e);
            break;
        }
        case State::InMenu: {
            AudioEventData e{AudioEvent::MenuOpen};
            EventBus::Instance().Publish(e);
            break;
        }
        default:
            break;
    }

    Logger::Info("GameState: {} -> {}",
                 static_cast<int>(m_previous_state), static_cast<int>(m_state));
}

} // namespace Unsuffered
