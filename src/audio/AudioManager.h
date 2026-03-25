#pragma once
// ============================================================================
// AUDIO SUBSYSTEM - Unsuffered: The Fallen
// ============================================================================
// First-class engine subsystem. Four simultaneous channels:
//   Music (streaming), Ambience (streaming), SFX (32-source pool), Voice (priority)
// All channels pass through master bus with Corruption distortion filter.
//
// Integration points are marked with [AUDIO HOOK] throughout the codebase.
// This file defines the complete audio API that game systems call into.
// ============================================================================

#include "core/Types.h"
#include "core/EventBus.h"
#include "core/Logger.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <queue>
#include <glm/glm.hpp>

namespace Unsuffered {

// ============================================================================
// AudioSource - Individual OpenAL source wrapper
// ============================================================================
class AudioSource {
public:
    bool Create();
    void Destroy();

    void SetBuffer(ALuint buffer);
    void SetPosition(const glm::vec3& pos);
    void SetVolume(float vol);
    void SetPitch(float pitch);
    void SetLooping(bool loop);

    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const;

    ALuint GetHandle() const { return m_source; }

private:
    ALuint m_source = 0;
};

// ============================================================================
// AudioSourcePool - Pre-allocated SFX source pool (32 sources)
// Avoids runtime allocation. Uses round-robin with priority override.
// ============================================================================
class AudioSourcePool {
public:
    static constexpr int POOL_SIZE = 32;

    bool Create();
    void Destroy();

    // Get next available source (round-robin)
    AudioSource* Acquire();

    // Force-acquire by stopping oldest
    AudioSource* AcquireForced();

private:
    std::array<AudioSource, POOL_SIZE> m_sources;
    int m_next = 0;
};

// ============================================================================
// MusicStream - Double-buffered streaming for music/ambience tracks
// ============================================================================
class MusicStream {
public:
    bool Create();
    void Destroy();

    void Load(const std::string& filepath);
    void Play();
    void Stop();
    void Pause();
    void Resume();

    // Called every frame to refill buffers and process fades
    void Update();

    // Smooth volume transitions
    void FadeTo(float target_volume, float duration_seconds);
    void CrossfadeTo(const std::string& new_track, float duration_seconds);

    void SetVolume(float vol);
    float GetVolume() const { return m_volume; }
    bool IsPlaying() const;

    // For save/load: get current playback position
    float GetPlaybackPosition() const { return m_playback_position; }
    void SeekTo(float position);

    const std::string& GetCurrentTrack() const { return m_current_track; }

private:
    ALuint m_source = 0;
    ALuint m_buffers[2] = {0, 0};  // Double buffer for gapless playback

    std::vector<float> m_pcm_data;
    size_t m_cursor = 0;
    int m_sample_rate = 44100;
    int m_channels = 2;

    float m_volume = 1.0f;
    float m_fade_target = 1.0f;
    float m_fade_speed = 0.0f;
    float m_playback_position = 0.0f;

    std::string m_current_track;
    std::string m_pending_track;   // For crossfade
    float m_crossfade_duration = 0.0f;
    bool m_crossfading = false;

    void FillBuffer(ALuint buffer);
    void LoadPCMData(const std::string& filepath);
};

// ============================================================================
// CorruptionAudioFilter - Progressive distortion via OpenAL EFX
// ============================================================================
// Corruption levels map to audio effects:
//   0.00 - 0.25: Subtle pitch wobble (EFX pitch shift)
//   0.25 - 0.50: Intermittent static bursts (random noise injection)
//   0.50 - 0.75: Reversed audio fragments (buffer manipulation)
//   0.75 - 1.00: Heartbeat underlayer + bitcrushed output
// ============================================================================
class CorruptionAudioFilter {
public:
    bool Initialize();
    void Shutdown();

    // Set corruption level (0.0 = clean, 1.0 = fully corrupted)
    void SetLevel(float corruption);
    float GetLevel() const { return m_level; }

    // Apply corruption effect to a specific source
    void Apply(ALuint source);

    // Update per frame (process time-varying effects)
    void Update(float dt);

private:
    ALuint m_effect_slot = 0;
    ALuint m_filter = 0;
    float m_level = 0.0f;
    float m_time = 0.0f;

    // Corruption stage thresholds
    static constexpr float STAGE_WOBBLE = 0.25f;
    static constexpr float STAGE_STATIC = 0.50f;
    static constexpr float STAGE_REVERSE = 0.75f;
    static constexpr float STAGE_HEARTBEAT = 0.90f;

    void ApplyPitchWobble(ALuint source);
    void ApplyStaticBursts(ALuint source);
    void ApplyReversedFragments(ALuint source);
    void ApplyHeartbeatCrush(ALuint source);
};

// ============================================================================
// CombatAudioManager - Battle-specific audio control
// ============================================================================
// Manages: battle music layers, clash silence, covenant boost SFX,
// creature faint sounds, intensity-driven dynamic mixing.
// ============================================================================
class CombatAudioManager {
public:
    void Initialize(class AudioManager* parent);

    // [AUDIO HOOK] Called by BattleSystem::StartBattle()
    void StartBattle(RegionID region);

    // [AUDIO HOOK] Called by BattleSystem::EndBattle()
    void EndBattle(bool victory);

    // [AUDIO HOOK] Dynamic intensity (0.0 calm - 1.0 critical)
    // Called when HP changes, ally faints, etc.
    void SetBattleIntensity(float intensity);

    // [AUDIO HOOK] Called by ClashResolver::Resolve()
    // Triggers 200ms silence followed by impact SFX
    void OnClash(ClashResult result);

    // [AUDIO HOOK] Called when Grak uses a Covenant Boost
    // Plays boost-specific SFX + Grak voice line
    void PlayCovenantBoost(CovenantBoost boost_type);

    // [AUDIO HOOK] Called when a creature faints
    void OnCreatureFaint(CreatureID creature);

    // [AUDIO HOOK] Called on critical hit
    // Pauses music 400ms, resumes with intensity spike
    void OnCriticalHit();

    void Update(float dt);

private:
    AudioManager* m_parent = nullptr;
    float m_intensity = 0.5f;
    float m_silence_timer = 0.0f;
    bool m_in_silence = false;
    bool m_in_battle = false;
};

// ============================================================================
// BondAudioManager - Creature bond audio events
// ============================================================================
class BondAudioManager {
public:
    void Initialize(class AudioManager* parent);

    // [AUDIO HOOK] Called by BondSystem when bond level changes
    // Level 1: soft bell. Level 3: warm chord. Level 5: harmonic resolution.
    // Level 6: transcendent choir swell.
    void OnBondChange(CreatureID creature, BondLevel old_level, BondLevel new_level);

    // [AUDIO HOOK] Called when a creature permanently leaves
    // Plays the creature's bond theme in reverse
    void OnCreatureDeparture(CreatureID creature);

    // [AUDIO HOOK] Called when a new creature is successfully tamed
    void OnCreatureTamed(CreatureID creature);

private:
    AudioManager* m_parent = nullptr;
    std::string GetBondLevelSound(BondLevel level) const;
};

// ============================================================================
// CreatureAudioManager - Per-creature voice profiles
// ============================================================================
class CreatureAudioManager {
public:
    void Initialize(class AudioManager* parent);

    // Play creature vocalization based on context
    // idle: ambient calls while exploring
    // combat: battle cries and pain sounds
    // bond: calls that evolve with Bond Depth
    void PlayCall(CreatureID creature, const std::string& context);

    // Bond-aware calls: higher bond = more complex vocalization
    void PlayBondCall(CreatureID creature, BondLevel bond_level);

private:
    AudioManager* m_parent = nullptr;
};

// ============================================================================
// UIAudioManager - Interface sounds
// ============================================================================
class UIAudioManager {
public:
    void Initialize(class AudioManager* parent);

    void OnMenuOpen();    // [AUDIO HOOK]
    void OnMenuClose();   // [AUDIO HOOK]
    void OnSave();        // [AUDIO HOOK] Save confirmation tone
    void OnLoad();        // [AUDIO HOOK]
    void OnNavigate();    // Cursor movement
    void OnSelect();      // Button press
    void OnCancel();      // Back button

private:
    AudioManager* m_parent = nullptr;
};

// ============================================================================
// NarrativeAudioManager - Story event audio
// ============================================================================
class NarrativeAudioManager {
public:
    void Initialize(class AudioManager* parent);

    void PlayCutscene(const std::string& cutscene_id);  // [AUDIO HOOK]
    void StopCutscene();

    // [AUDIO HOOK] Each ending has unique musical finale:
    //   LastMan: title theme fading to silence
    //   HigherMan: aggressive orchestral ending abruptly
    //   Ubermensch: full theme with harmonic resolution
    //   EternalRecurrence: title theme in major key + creature voices
    void PlayEnding(Ending ending_type);

    void OnDialogueOpen();   // [AUDIO HOOK]
    void OnDialogueClose();  // [AUDIO HOOK]

private:
    AudioManager* m_parent = nullptr;
};

// ============================================================================
// AudioManager - Central audio controller
// ============================================================================
// This is the primary audio interface. All game systems interact with audio
// exclusively through this class and its sub-managers.
//
// Initialization: called once in Engine::Initialize()
// Update: called every frame in Engine::Run() game loop
// Shutdown: called in Engine::Shutdown()
// ============================================================================
class AudioManager {
public:
    bool Initialize();
    void Shutdown();

    // Called every frame in the game loop
    // Processes: streaming buffers, crossfades, corruption filter, 3D listener
    void Update();

    // --- Region Audio ---
    // [AUDIO HOOK] Called by WorldMap when player enters a new territory
    void SetRegionAmbience(RegionID region);

    // [AUDIO HOOK] Called during territory boundary crossing
    // Crossfades between two region ambient tracks over duration seconds
    void CrossfadeRegion(RegionID from, RegionID to, float duration = 3.0f);

    // --- Global Effects ---
    // [AUDIO HOOK] Called by CorruptionSystem when corruption level changes
    // Applies progressive distortion to ALL audio channels
    void SetCorruptionLevel(float level);  // 0.0 - 1.0
    float GetCorruptionLevel() const { return m_corruption_level; }

    // --- Master Controls ---
    void SetMasterVolume(float vol);
    void SetMusicVolume(float vol);
    void SetSFXVolume(float vol);
    void SetVoiceVolume(float vol);

    // --- SFX ---
    // Play a one-shot sound effect. Optional 3D position for spatial audio.
    void PlaySFX(const AudioAssetID& asset_id, const glm::vec3& position = {0,0,0});

    // Play a voice line (priority channel, interrupts previous)
    void PlayVoice(const AudioAssetID& asset_id);

    // --- 3D Listener ---
    // [AUDIO HOOK] Synced with Camera position every frame
    void SetListenerPosition(const glm::vec3& pos, const glm::vec3& forward);

    // --- Music Control ---
    void PlayMusic(const AudioAssetID& track_id);
    void StopMusic(float fade_duration = 1.0f);
    void PauseMusic();
    void ResumeMusic();

    // --- Save/Load Audio State ---
    struct AudioState {
        std::string current_music_track;
        float music_position;
        std::string current_ambience_track;
        float ambience_position;
        float corruption_level;
        float master_volume;
    };
    AudioState SaveState() const;
    void RestoreState(const AudioState& state);  // 0.5s fade-in on restore

    // --- Sub-managers ---
    CombatAudioManager&    GetCombatAudio()    { return m_combat_audio; }
    BondAudioManager&      GetBondAudio()      { return m_bond_audio; }
    CreatureAudioManager&  GetCreatureAudio()   { return m_creature_audio; }
    UIAudioManager&        GetUIAudio()         { return m_ui_audio; }
    NarrativeAudioManager& GetNarrativeAudio()  { return m_narrative_audio; }

    // --- Asset Loading ---
    // Load audio manifest from JSON (maps asset IDs to file paths)
    bool LoadManifest(const std::string& manifest_path);

    // Get file path for an asset ID
    std::string ResolveAssetPath(const AudioAssetID& id) const;

    // --- Footstep System ---
    // [AUDIO HOOK] Called by player movement system
    // Surface type determines footstep sound (ash, stone, metal, organic, water)
    void PlayFootstep(const std::string& surface_type, const glm::vec3& position);

private:
    // OpenAL context
    ALCdevice*  m_device  = nullptr;
    ALCcontext* m_context = nullptr;

    // Audio channels
    MusicStream     m_music;
    MusicStream     m_ambience;
    AudioSourcePool m_sfx_pool;
    AudioSource     m_voice;

    // Effects
    CorruptionAudioFilter m_corruption_filter;

    // Sub-managers
    CombatAudioManager    m_combat_audio;
    BondAudioManager      m_bond_audio;
    CreatureAudioManager  m_creature_audio;
    UIAudioManager        m_ui_audio;
    NarrativeAudioManager m_narrative_audio;

    // State
    float m_master_volume    = 1.0f;
    float m_music_volume     = 0.8f;
    float m_sfx_volume       = 1.0f;
    float m_voice_volume     = 1.0f;
    float m_corruption_level = 0.0f;

    RegionID m_current_region = 0;

    // Asset manifest: maps asset IDs to file paths
    std::unordered_map<std::string, std::string> m_asset_manifest;

    // Buffer cache: loaded audio buffers keyed by asset ID
    std::unordered_map<std::string, ALuint> m_buffer_cache;

    // Load or retrieve cached audio buffer
    ALuint GetOrLoadBuffer(const AudioAssetID& id);

    // Subscribe to EventBus for audio events
    void RegisterEventHandlers();
};

} // namespace Unsuffered
