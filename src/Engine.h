#pragma once
// ============================================================================
// Engine - Central coordinator for all subsystems
// ============================================================================
// Initialization order: Platform -> Core -> Resource -> Audio -> Rendering -> Game
// Shutdown order: reverse
// Game loop: fixed timestep at 60Hz with rendering interpolation
// ============================================================================

#include "platform/Platform.h"
#include "core/EventBus.h"
#include "core/Logger.h"
#include "resource/AssetManager.h"
#include "rendering/Renderer.h"
#include "audio/AudioManager.h"
#include "save/SaveManager.h"

#include <memory>

namespace Unsuffered {

class Engine {
public:
    bool Initialize(const std::string& window_title, int width, int height);
    void Run();
    void Shutdown();

    // Subsystem access
    Window& GetWindow()            { return m_window; }
    Input& GetInput()              { return m_input; }
    Renderer& GetRenderer()        { return m_renderer; }
    AudioManager& GetAudio()       { return m_audio; }
    AssetManager& GetAssets()      { return m_assets; }
    SaveManager& GetSaveManager()  { return m_save; }
    GameState& GetGameState()      { return m_game_state; }

    bool IsRunning() const { return m_running; }
    void RequestQuit() { m_running = false; }

    static Engine& Instance() {
        static Engine engine;
        return engine;
    }

private:
    // Platform layer
    Window m_window;
    Input m_input;

    // Resource layer
    AssetManager m_assets;

    // Rendering layer
    Renderer m_renderer;

    // Audio layer
    AudioManager m_audio;

    // Game layer
    GameState m_game_state;
    SaveManager m_save;

    bool m_running = false;
};

} // namespace Unsuffered
