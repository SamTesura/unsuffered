// ============================================================================
// AudioManager.cpp - Central audio controller implementation
// ============================================================================

#include "AudioManager.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace Unsuffered {

bool AudioManager::Initialize() {
    Logger::Info("Initializing audio subsystem...");

    // Open default audio device
    m_device = alcOpenDevice(nullptr);
    if (!m_device) {
        Logger::Error("Failed to open OpenAL device");
        return false;
    }

    m_context = alcCreateContext(m_device, nullptr);
    if (!m_context || !alcMakeContextCurrent(m_context)) {
        Logger::Error("Failed to create OpenAL context");
        return false;
    }

    // Initialize channels
    if (!m_music.Create() || !m_ambience.Create() || !m_sfx_pool.Create()) {
        Logger::Error("Failed to create audio channels");
        return false;
    }

    if (!m_voice.Create()) {
        Logger::Error("Failed to create voice channel");
        return false;
    }

    // Initialize corruption filter
    if (!m_corruption_filter.Initialize()) {
        Logger::Warn("OpenAL EFX not available - corruption audio disabled");
    }

    // Initialize sub-managers
    m_combat_audio.Initialize(this);
    m_bond_audio.Initialize(this);
    m_creature_audio.Initialize(this);
    m_ui_audio.Initialize(this);
    m_narrative_audio.Initialize(this);

    // Wire up EventBus handlers
    RegisterEventHandlers();

    Logger::Info("Audio subsystem initialized (pool: {} sources)", AudioSourcePool::POOL_SIZE);
    return true;
}

void AudioManager::Shutdown() {
    Logger::Info("Shutting down audio subsystem...");

    m_music.Destroy();
    m_ambience.Destroy();
    m_sfx_pool.Destroy();
    m_voice.Destroy();
    m_corruption_filter.Shutdown();

    // Free all cached buffers
    for (auto& [id, buffer] : m_buffer_cache) {
        alDeleteBuffers(1, &buffer);
    }
    m_buffer_cache.clear();

    if (m_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(m_context);
    }
    if (m_device) {
        alcCloseDevice(m_device);
    }

    Logger::Info("Audio subsystem shut down");
}

void AudioManager::Update() {
    // Refill streaming buffers and process fades
    m_music.Update();
    m_ambience.Update();

    // Update corruption distortion filter
    m_corruption_filter.Update(1.0f / 60.0f);  // Assume 60fps for filter timing

    // Update combat audio (silence timers, intensity)
    m_combat_audio.Update(1.0f / 60.0f);

    // Sync 3D listener with camera position
    // (Engine calls SetListenerPosition each frame before this)
}

// --- Region Audio ---
void AudioManager::SetRegionAmbience(RegionID region) {
    if (region == m_current_region) return;

    auto old_region = m_current_region;
    m_current_region = region;

    // Resolve region ambient track from manifest
    std::string region_key = "region_" + std::to_string(region) + "_ambient";
    auto path = ResolveAssetPath(region_key);

    if (!path.empty()) {
        m_ambience.CrossfadeTo(path, 3.0f);
        Logger::Info("Region ambience: {} -> {} (3s crossfade)", old_region, region);
    }
}

void AudioManager::CrossfadeRegion(RegionID from, RegionID to, float duration) {
    m_current_region = to;
    std::string track = ResolveAssetPath("region_" + std::to_string(to) + "_ambient");
    if (!track.empty()) {
        m_ambience.CrossfadeTo(track, duration);
    }
}

// --- Corruption ---
void AudioManager::SetCorruptionLevel(float level) {
    m_corruption_level = std::clamp(level, 0.0f, 1.0f);
    m_corruption_filter.SetLevel(m_corruption_level);

    // Apply to all active channels
    m_corruption_filter.Apply(m_music.GetCurrentTrack().empty() ? 0 : 1);  // Placeholder
    Logger::Debug("Corruption audio level: {:.2f}", m_corruption_level);
}

// --- Volume Controls ---
void AudioManager::SetMasterVolume(float vol) {
    m_master_volume = std::clamp(vol, 0.0f, 1.0f);
    alListenerf(AL_GAIN, m_master_volume);
}

void AudioManager::SetMusicVolume(float vol) {
    m_music_volume = std::clamp(vol, 0.0f, 1.0f);
    m_music.SetVolume(m_music_volume * m_master_volume);
}

void AudioManager::SetSFXVolume(float vol) { m_sfx_volume = std::clamp(vol, 0.0f, 1.0f); }
void AudioManager::SetVoiceVolume(float vol) { m_voice_volume = std::clamp(vol, 0.0f, 1.0f); }

// --- SFX ---
void AudioManager::PlaySFX(const AudioAssetID& asset_id, const glm::vec3& position) {
    auto* source = m_sfx_pool.Acquire();
    if (!source) source = m_sfx_pool.AcquireForced();

    ALuint buffer = GetOrLoadBuffer(asset_id);
    if (buffer == 0) return;

    source->SetBuffer(buffer);
    source->SetPosition(position);
    source->SetVolume(m_sfx_volume * m_master_volume);
    source->Play();
}

void AudioManager::PlayVoice(const AudioAssetID& asset_id) {
    ALuint buffer = GetOrLoadBuffer(asset_id);
    if (buffer == 0) return;

    m_voice.Stop();
    m_voice.SetBuffer(buffer);
    m_voice.SetVolume(m_voice_volume * m_master_volume);
    m_voice.Play();
}

// --- 3D Listener ---
void AudioManager::SetListenerPosition(const glm::vec3& pos, const glm::vec3& forward) {
    alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
    glm::vec3 up = {0, 1, 0};
    float orientation[] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
    alListenerfv(AL_ORIENTATION, orientation);
}

// --- Music ---
void AudioManager::PlayMusic(const AudioAssetID& track_id) {
    auto path = ResolveAssetPath(track_id);
    if (!path.empty()) {
        m_music.Load(path);
        m_music.SetVolume(m_music_volume * m_master_volume);
        m_music.Play();
    }
}

void AudioManager::StopMusic(float fade_duration) {
    m_music.FadeTo(0.0f, fade_duration);
}

void AudioManager::PauseMusic() { m_music.Pause(); }
void AudioManager::ResumeMusic() { m_music.Resume(); }

// --- Save/Load ---
AudioManager::AudioState AudioManager::SaveState() const {
    return {
        m_music.GetCurrentTrack(),
        m_music.GetPlaybackPosition(),
        m_ambience.GetCurrentTrack(),
        m_ambience.GetPlaybackPosition(),
        m_corruption_level,
        m_master_volume
    };
}

void AudioManager::RestoreState(const AudioState& state) {
    m_master_volume = state.master_volume;
    m_corruption_level = state.corruption_level;
    m_corruption_filter.SetLevel(m_corruption_level);

    if (!state.current_music_track.empty()) {
        m_music.Load(state.current_music_track);
        m_music.SeekTo(state.music_position);
        m_music.SetVolume(0.0f);
        m_music.Play();
        m_music.FadeTo(m_music_volume * m_master_volume, 0.5f);  // 0.5s fade-in
    }

    if (!state.current_ambience_track.empty()) {
        m_ambience.Load(state.current_ambience_track);
        m_ambience.SeekTo(state.ambience_position);
        m_ambience.Play();
    }

    Logger::Info("Audio state restored (music: {}, corruption: {:.2f})",
                 state.current_music_track, state.corruption_level);
}

// --- Asset Manifest ---
bool AudioManager::LoadManifest(const std::string& manifest_path) {
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        Logger::Error("Failed to load audio manifest: {}", manifest_path);
        return false;
    }

    try {
        nlohmann::json manifest = nlohmann::json::parse(file);

        // Load music entries
        if (manifest.contains("music")) {
            for (auto& [region, tracks] : manifest["music"].items()) {
                for (auto& [type, path] : tracks.items()) {
                    std::string key = region + "_" + type;
                    m_asset_manifest[key] = path.get<std::string>();
                }
            }
        }

        // Load SFX entries
        if (manifest.contains("sfx")) {
            for (auto& [id, path] : manifest["sfx"].items()) {
                m_asset_manifest[id] = path.get<std::string>();
            }
        }

        // Load creature voice entries
        if (manifest.contains("creatures")) {
            for (auto& [creature_id, voices] : manifest["creatures"].items()) {
                for (auto& [context, paths] : voices.items()) {
                    if (paths.is_array()) {
                        // Multiple variants: store first for now
                        m_asset_manifest[creature_id + "_" + context] =
                            paths[0].get<std::string>();
                    } else if (paths.is_object()) {
                        // Bond-level specific
                        for (auto& [level, path] : paths.items()) {
                            m_asset_manifest[creature_id + "_" + context + "_" + level] =
                                path.get<std::string>();
                        }
                    }
                }
            }
        }

        Logger::Info("Audio manifest loaded: {} entries", m_asset_manifest.size());
        return true;
    } catch (const nlohmann::json::exception& e) {
        Logger::Error("Audio manifest parse error: {}", e.what());
        return false;
    }
}

std::string AudioManager::ResolveAssetPath(const AudioAssetID& id) const {
    auto it = m_asset_manifest.find(id);
    if (it != m_asset_manifest.end()) return it->second;
    Logger::Warn("Audio asset not found in manifest: {}", id);
    return "";
}

ALuint AudioManager::GetOrLoadBuffer(const AudioAssetID& id) {
    // Check cache
    auto it = m_buffer_cache.find(id);
    if (it != m_buffer_cache.end()) return it->second;

    // Resolve path and load
    auto path = ResolveAssetPath(id);
    if (path.empty()) return 0;

    // TODO: Use dr_libs to decode audio file into PCM
    // For now, create empty buffer as placeholder
    ALuint buffer;
    alGenBuffers(1, &buffer);
    m_buffer_cache[id] = buffer;

    Logger::Debug("Loaded audio buffer: {} -> {}", id, path);
    return buffer;
}

void AudioManager::PlayFootstep(const std::string& surface_type, const glm::vec3& position) {
    std::string asset_id = "footstep_" + surface_type;
    PlaySFX(asset_id, position);
}

// --- EventBus Integration ---
void AudioManager::RegisterEventHandlers() {
    auto& bus = EventBus::Instance();

    // Region events
    bus.Subscribe<RegionAudioData>([this](const RegionAudioData& data) {
        CrossfadeRegion(data.from_region, data.to_region, data.crossfade_duration);
    });

    // Combat events
    bus.Subscribe<CombatAudioData>([this](const CombatAudioData& data) {
        switch (data.event) {
            case AudioEvent::BattleStart:
                m_combat_audio.StartBattle(0); break;
            case AudioEvent::ClashOccur:
                m_combat_audio.OnClash(data.clash_result); break;
            case AudioEvent::CovenantBoostUsed:
                m_combat_audio.PlayCovenantBoost(data.boost_type); break;
            case AudioEvent::CriticalHit:
                m_combat_audio.OnCriticalHit(); break;
            default: break;
        }
    });

    // Bond events
    bus.Subscribe<BondAudioData>([this](const BondAudioData& data) {
        if (data.is_departure) {
            m_bond_audio.OnCreatureDeparture(data.creature);
        } else {
            m_bond_audio.OnBondChange(data.creature, data.old_level, data.new_level);
        }
    });

    Logger::Info("Audio event handlers registered");
}

// ============================================================================
// CombatAudioManager Implementation
// ============================================================================

void CombatAudioManager::Initialize(AudioManager* parent) { m_parent = parent; }

void CombatAudioManager::StartBattle(RegionID region) {
    m_in_battle = true;
    m_intensity = 0.5f;
    std::string track = "region_" + std::to_string(region) + "_combat";
    m_parent->PlayMusic(track);
    Logger::Info("Battle audio started (region {})", region);
}

void CombatAudioManager::EndBattle(bool victory) {
    m_in_battle = false;
    m_parent->StopMusic(1.0f);
    m_parent->PlaySFX(victory ? "battle_victory" : "battle_defeat");
}

void CombatAudioManager::SetBattleIntensity(float intensity) {
    m_intensity = std::clamp(intensity, 0.0f, 1.0f);
    // Dynamic music mixing would adjust layer volumes here
}

void CombatAudioManager::OnClash(ClashResult result) {
    // 200ms silence before impact
    m_in_silence = true;
    m_silence_timer = 0.2f;
    m_parent->PauseMusic();

    // Schedule impact SFX after silence
    std::string sfx = "clash_";
    switch (result) {
        case ClashResult::AttackerWins: sfx += "attacker_wins"; break;
        case ClashResult::DefenderWins: sfx += "defender_wins"; break;
        case ClashResult::Draw:         sfx += "draw"; break;
    }
    // Impact will play when silence_timer expires in Update()
}

void CombatAudioManager::PlayCovenantBoost(CovenantBoost boost_type) {
    std::string sfx, voice;
    switch (boost_type) {
        case CovenantBoost::AffirmWill:
            sfx = "covenant_affirm_will"; voice = "grak_rise"; break;
        case CovenantBoost::SharedSuffering:
            sfx = "covenant_shared_suffering"; voice = "grak_pain"; break;
        case CovenantBoost::CovenantSurge:
            sfx = "covenant_surge"; voice = "grak_surge"; break;
        case CovenantBoost::WillToOvercome:
            sfx = "covenant_overcome"; voice = "grak_overcome"; break;
        case CovenantBoost::BondResonance:
            sfx = "covenant_resonance"; voice = "grak_resonate"; break;
        case CovenantBoost::EternalReturn:
            sfx = "covenant_eternal_return"; voice = "grak_return"; break;
    }
    m_parent->PlaySFX(sfx);
    m_parent->PlayVoice(voice);
}

void CombatAudioManager::OnCreatureFaint(CreatureID creature) {
    m_parent->PlaySFX("creature_faint");
    SetBattleIntensity(std::min(1.0f, m_intensity + 0.2f));
}

void CombatAudioManager::OnCriticalHit() {
    // 400ms music pause then intensity spike
    m_in_silence = true;
    m_silence_timer = 0.4f;
    m_parent->PauseMusic();
    m_parent->PlaySFX("critical_hit");
    SetBattleIntensity(std::min(1.0f, m_intensity + 0.3f));
}

void CombatAudioManager::Update(float dt) {
    if (m_in_silence) {
        m_silence_timer -= dt;
        if (m_silence_timer <= 0.0f) {
            m_in_silence = false;
            m_parent->ResumeMusic();
        }
    }
}

// ============================================================================
// BondAudioManager Implementation
// ============================================================================

void BondAudioManager::Initialize(AudioManager* parent) { m_parent = parent; }

void BondAudioManager::OnBondChange(CreatureID creature, BondLevel old_level, BondLevel new_level) {
    if (new_level > old_level) {
        m_parent->PlaySFX(GetBondLevelSound(new_level));
        Logger::Info("Bond audio: creature {} level {} -> {}", creature,
                     static_cast<int>(old_level), static_cast<int>(new_level));
    } else {
        m_parent->PlaySFX("bond_decrease_warning");
    }
}

void BondAudioManager::OnCreatureDeparture(CreatureID creature) {
    // Play the creature's bond theme reversed
    std::string asset = "creature_" + std::to_string(creature) + "_departure";
    m_parent->PlaySFX(asset);
    Logger::Info("Bond audio: creature {} departed permanently", creature);
}

void BondAudioManager::OnCreatureTamed(CreatureID creature) {
    m_parent->PlaySFX("covenant_formed");
    m_parent->PlayVoice("grak_covenant");
}

std::string BondAudioManager::GetBondLevelSound(BondLevel level) const {
    switch (level) {
        case BondLevel::Recognition:  return "bond_level_1_bell";
        case BondLevel::Trust:        return "bond_level_2_chime";
        case BondLevel::Partnership:  return "bond_level_3_chord";
        case BondLevel::Kinship:      return "bond_level_4_harmony";
        case BondLevel::Covenant:     return "bond_level_5_resolution";
        case BondLevel::Transcendence:return "bond_level_6_transcendence";
    }
    return "bond_level_1_bell";
}

// ============================================================================
// NarrativeAudioManager Implementation
// ============================================================================

void NarrativeAudioManager::Initialize(AudioManager* parent) { m_parent = parent; }

void NarrativeAudioManager::PlayCutscene(const std::string& cutscene_id) {
    m_parent->PlayMusic("cutscene_" + cutscene_id);
}

void NarrativeAudioManager::StopCutscene() {
    m_parent->StopMusic(1.0f);
}

void NarrativeAudioManager::PlayEnding(Ending ending_type) {
    std::string track;
    switch (ending_type) {
        case Ending::TheLastMan:
            track = "ending_last_man"; break;         // Title theme fading to silence
        case Ending::TheHigherMan:
            track = "ending_higher_man"; break;       // Aggressive, ends abruptly
        case Ending::TheUbermensch:
            track = "ending_ubermensch"; break;       // Full theme, harmonic resolution
        case Ending::EternalRecurrence:
            track = "ending_eternal_recurrence"; break;// Major key + creature voices
    }
    m_parent->PlayMusic(track);
    Logger::Info("Ending audio: {}", track);
}

void NarrativeAudioManager::OnDialogueOpen() { m_parent->GetUIAudio().OnMenuOpen(); }
void NarrativeAudioManager::OnDialogueClose() { m_parent->GetUIAudio().OnMenuClose(); }

// ============================================================================
// UIAudioManager / CreatureAudioManager stubs
// ============================================================================

void UIAudioManager::Initialize(AudioManager* parent) { m_parent = parent; }
void UIAudioManager::OnMenuOpen()  { m_parent->PlaySFX("ui_menu_open"); }
void UIAudioManager::OnMenuClose() { m_parent->PlaySFX("ui_menu_close"); }
void UIAudioManager::OnSave()      { m_parent->PlaySFX("ui_save_confirm"); }
void UIAudioManager::OnLoad()      { m_parent->PlaySFX("ui_load"); }
void UIAudioManager::OnNavigate()  { m_parent->PlaySFX("ui_navigate"); }
void UIAudioManager::OnSelect()    { m_parent->PlaySFX("ui_select"); }
void UIAudioManager::OnCancel()    { m_parent->PlaySFX("ui_cancel"); }

void CreatureAudioManager::Initialize(AudioManager* parent) { m_parent = parent; }

void CreatureAudioManager::PlayCall(CreatureID creature, const std::string& context) {
    std::string asset = "creature_" + std::to_string(creature) + "_" + context;
    m_parent->PlaySFX(asset);
}

void CreatureAudioManager::PlayBondCall(CreatureID creature, BondLevel bond_level) {
    std::string asset = "creature_" + std::to_string(creature) +
                        "_bond_" + std::to_string(static_cast<int>(bond_level));
    m_parent->PlaySFX(asset);
}

// ============================================================================
// AudioSource / AudioSourcePool / MusicStream stubs
// (Full implementation requires dr_libs PCM decoding)
// ============================================================================

bool AudioSource::Create() { alGenSources(1, &m_source); return m_source != 0; }
void AudioSource::Destroy() { if (m_source) alDeleteSources(1, &m_source); }
void AudioSource::SetBuffer(ALuint buf) { alSourcei(m_source, AL_BUFFER, buf); }
void AudioSource::SetPosition(const glm::vec3& p) { alSource3f(m_source, AL_POSITION, p.x, p.y, p.z); }
void AudioSource::SetVolume(float v) { alSourcef(m_source, AL_GAIN, v); }
void AudioSource::SetPitch(float p) { alSourcef(m_source, AL_PITCH, p); }
void AudioSource::SetLooping(bool l) { alSourcei(m_source, AL_LOOPING, l ? AL_TRUE : AL_FALSE); }
void AudioSource::Play() { alSourcePlay(m_source); }
void AudioSource::Stop() { alSourceStop(m_source); }
void AudioSource::Pause() { alSourcePause(m_source); }
bool AudioSource::IsPlaying() const {
    ALint state; alGetSourcei(m_source, AL_SOURCE_STATE, &state); return state == AL_PLAYING;
}

bool AudioSourcePool::Create() {
    for (auto& s : m_sources) if (!s.Create()) return false;
    return true;
}
void AudioSourcePool::Destroy() { for (auto& s : m_sources) s.Destroy(); }
AudioSource* AudioSourcePool::Acquire() {
    for (int i = 0; i < POOL_SIZE; ++i) {
        int idx = (m_next + i) % POOL_SIZE;
        if (!m_sources[idx].IsPlaying()) { m_next = (idx + 1) % POOL_SIZE; return &m_sources[idx]; }
    }
    return nullptr;
}
AudioSource* AudioSourcePool::AcquireForced() {
    m_sources[m_next].Stop();
    auto* s = &m_sources[m_next];
    m_next = (m_next + 1) % POOL_SIZE;
    return s;
}

bool MusicStream::Create() {
    alGenSources(1, &m_source);
    alGenBuffers(2, m_buffers);
    return m_source != 0;
}
void MusicStream::Destroy() {
    Stop();
    alDeleteSources(1, &m_source);
    alDeleteBuffers(2, m_buffers);
}
void MusicStream::Load(const std::string& fp) { m_current_track = fp; LoadPCMData(fp); m_cursor = 0; }
void MusicStream::Play() { if (m_source) alSourcePlay(m_source); }
void MusicStream::Stop() { if (m_source) alSourceStop(m_source); }
void MusicStream::Pause() { if (m_source) alSourcePause(m_source); }
void MusicStream::Resume() { if (m_source) alSourcePlay(m_source); }
void MusicStream::Update() {
    // Process fade
    if (m_fade_speed != 0.0f) {
        m_volume += m_fade_speed * (1.0f / 60.0f);
        if ((m_fade_speed > 0 && m_volume >= m_fade_target) ||
            (m_fade_speed < 0 && m_volume <= m_fade_target)) {
            m_volume = m_fade_target;
            m_fade_speed = 0.0f;
        }
        alSourcef(m_source, AL_GAIN, m_volume);
    }
    // TODO: Refill streaming buffers from m_pcm_data
}
void MusicStream::FadeTo(float target, float dur) {
    m_fade_target = target;
    m_fade_speed = (target - m_volume) / std::max(dur, 0.01f);
}
void MusicStream::CrossfadeTo(const std::string& track, float dur) {
    FadeTo(0.0f, dur);
    m_pending_track = track;
    m_crossfade_duration = dur;
    m_crossfading = true;
}
void MusicStream::SetVolume(float v) { m_volume = v; alSourcef(m_source, AL_GAIN, v); }
bool MusicStream::IsPlaying() const {
    ALint state; alGetSourcei(m_source, AL_SOURCE_STATE, &state); return state == AL_PLAYING;
}
void MusicStream::SeekTo(float pos) { m_playback_position = pos; /* TODO: PCM cursor seek */ }
void MusicStream::LoadPCMData(const std::string& fp) {
    // TODO: Use dr_wav/dr_mp3/dr_flac to decode
    Logger::Debug("Loading PCM: {}", fp);
}
void MusicStream::FillBuffer(ALuint buffer) {
    // TODO: Fill buffer from m_pcm_data at m_cursor
}

bool CorruptionAudioFilter::Initialize() {
    // TODO: Check for OpenAL EFX extension and create effect slots
    Logger::Info("Corruption audio filter initialized");
    return true;
}
void CorruptionAudioFilter::Shutdown() {}
void CorruptionAudioFilter::SetLevel(float c) { m_level = std::clamp(c, 0.0f, 1.0f); }
void CorruptionAudioFilter::Apply(ALuint source) { /* TODO: Apply EFX */ }
void CorruptionAudioFilter::Update(float dt) {
    m_time += dt;
    // Time-varying effects update here
}

} // namespace Unsuffered
