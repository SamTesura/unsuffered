#pragma once
// ============================================================================
// Resource Layer - Asset loading, caching, and management
// ============================================================================

#include "core/Types.h"
#include "core/Logger.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace Unsuffered {

// Generic resource cache template
template<typename T>
class ResourceCache {
public:
    void Store(const std::string& key, std::shared_ptr<T> resource) {
        m_cache[key] = resource;
    }

    std::shared_ptr<T> Get(const std::string& key) const {
        auto it = m_cache.find(key);
        return (it != m_cache.end()) ? it->second : nullptr;
    }

    bool Has(const std::string& key) const { return m_cache.count(key) > 0; }

    void Remove(const std::string& key) { m_cache.erase(key); }

    void Clear() { m_cache.clear(); }

    size_t Size() const { return m_cache.size(); }

private:
    std::unordered_map<std::string, std::shared_ptr<T>> m_cache;
};

// Asset Manager - coordinates loading across all resource types
class AssetManager {
public:
    bool Initialize(const std::string& asset_root);

    // Load creature data from JSON
    bool LoadCreatureDatabase(const std::string& json_path);

    // Load ability data from JSON
    bool LoadAbilityDatabase(const std::string& json_path);

    // Load region data from JSON
    bool LoadRegionDatabase(const std::string& json_path);

    // [AUDIO HOOK] Load audio manifest
    // This maps audio asset IDs to file paths for the AudioManager
    bool LoadAudioManifest(const std::string& json_path);

    // Resolve a relative asset path to full path
    std::string ResolvePath(const std::string& relative) const;

    const std::string& GetRoot() const { return m_root; }

private:
    std::string m_root;
};

} // namespace Unsuffered
