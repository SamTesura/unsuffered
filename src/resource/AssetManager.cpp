// ============================================================================
// Asset Manager Implementation
// Coordinates loading across all resource types
// ============================================================================

#include "resource/AssetManager.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Unsuffered {

bool AssetManager::Initialize(const std::string& asset_root) {
    m_root = asset_root;

    // Ensure trailing separator
    if (!m_root.empty() && m_root.back() != '/' && m_root.back() != '\\') {
        m_root += '/';
    }

    if (!std::filesystem::exists(m_root)) {
        Logger::Error("Asset root not found: {}", m_root);
        return false;
    }

    Logger::Info("AssetManager initialized with root: {}", m_root);
    return true;
}

bool AssetManager::LoadCreatureDatabase(const std::string& json_path) {
    try {
        std::ifstream file(ResolvePath(json_path));
        if (!file.is_open()) {
            Logger::Error("Failed to open creature database: {}", json_path);
            return false;
        }

        nlohmann::json j;
        file >> j;

        int count = 0;
        if (j.contains("creatures")) {
            count = static_cast<int>(j["creatures"].size());
        }

        Logger::Info("Creature database loaded: {} creatures from {}", count, json_path);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse creature database: {}", e.what());
        return false;
    }
}

bool AssetManager::LoadAbilityDatabase(const std::string& json_path) {
    try {
        std::ifstream file(ResolvePath(json_path));
        if (!file.is_open()) {
            Logger::Error("Failed to open ability database: {}", json_path);
            return false;
        }

        nlohmann::json j;
        file >> j;

        int count = 0;
        if (j.contains("abilities")) {
            count = static_cast<int>(j["abilities"].size());
        }

        Logger::Info("Ability database loaded: {} abilities from {}", count, json_path);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse ability database: {}", e.what());
        return false;
    }
}

bool AssetManager::LoadRegionDatabase(const std::string& json_path) {
    try {
        std::ifstream file(ResolvePath(json_path));
        if (!file.is_open()) {
            Logger::Error("Failed to open region database: {}", json_path);
            return false;
        }

        nlohmann::json j;
        file >> j;

        int count = 0;
        if (j.contains("regions")) {
            count = static_cast<int>(j["regions"].size());
        }

        Logger::Info("Region database loaded: {} regions from {}", count, json_path);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse region database: {}", e.what());
        return false;
    }
}

bool AssetManager::LoadAudioManifest(const std::string& json_path) {
    // [AUDIO HOOK] The audio manifest maps asset IDs to file paths.
    // AudioManager uses this to resolve sound file locations.
    try {
        std::ifstream file(ResolvePath(json_path));
        if (!file.is_open()) {
            Logger::Error("Failed to open audio manifest: {}", json_path);
            return false;
        }

        nlohmann::json j;
        file >> j;

        int music_count = 0, sfx_count = 0, creature_count = 0;
        if (j.contains("music"))     music_count = static_cast<int>(j["music"].size());
        if (j.contains("sfx"))       sfx_count = static_cast<int>(j["sfx"].size());
        if (j.contains("creatures")) creature_count = static_cast<int>(j["creatures"].size());

        Logger::Info("Audio manifest loaded: {} music, {} sfx, {} creature voice profiles",
                     music_count, sfx_count, creature_count);
        return true;
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse audio manifest: {}", e.what());
        return false;
    }
}

std::string AssetManager::ResolvePath(const std::string& relative) const {
    return m_root + relative;
}

} // namespace Unsuffered
