#include "Engine.h"
#include <GL/glew.h>
#include <cmath>
#include <algorithm>
#include <array>

namespace Unsuffered {

// ============================================================================
// AABB Collision System — World object bounding boxes
// ============================================================================
struct AABB {
    glm::vec3 min, max;
};

// Test if a point (with radius for Grak's body) overlaps an AABB
// Returns the push-out vector to resolve the collision
static glm::vec3 ResolveAABBCollision(const glm::vec3& pos, float radius, const AABB& box) {
    // Find the closest point on the AABB to the player position (XZ only)
    float closest_x = std::clamp(pos.x, box.min.x, box.max.x);
    float closest_z = std::clamp(pos.z, box.min.z, box.max.z);

    float dx = pos.x - closest_x;
    float dz = pos.z - closest_z;
    float dist_sq = dx * dx + dz * dz;

    // Only collide if player is within radius AND within the Y range of the object
    if (dist_sq < radius * radius && pos.y < box.max.y) {
        float dist = std::sqrt(dist_sq);
        if (dist < 0.0001f) {
            // Player is inside the box — push out along X arbitrarily
            return {radius, 0.0f, 0.0f};
        }
        float overlap = radius - dist;
        return glm::vec3(dx / dist * overlap, 0.0f, dz / dist * overlap);
    }
    return {0.0f, 0.0f, 0.0f};
}

// Build world collision AABBs from the scene layout
static std::array<AABB, 16> BuildWorldColliders() {
    std::array<AABB, 16> colliders;
    int idx = 0;

    // Covenant altar base (center at 0, 0.5, -5; scale 1.5, 1.0, 1.5)
    colliders[idx++] = {{-0.75f, 0.0f, -5.75f}, {0.75f, 1.0f, -4.25f}};

    // Altar top slab (center at 0, 1.1, -5; scale 2.0, 0.15, 2.0)
    colliders[idx++] = {{-1.0f, 0.0f, -6.0f}, {1.0f, 1.25f, -4.0f}};

    // Four pillars (wider bases now: 0.70 footprint at base, 0.45 shaft)
    colliders[idx++] = {{-3.85f, 0.0f, -3.35f}, {-3.15f, 3.0f, -2.65f}};
    colliders[idx++] = {{3.15f, 0.0f, -3.35f}, {3.85f, 2.2f, -2.65f}};
    colliders[idx++] = {{-3.85f, 0.0f, -7.35f}, {-3.15f, 3.2f, -6.65f}};
    colliders[idx++] = {{3.15f, 0.0f, -7.35f}, {3.85f, 2.6f, -6.65f}};

    // Left wall fragment (center -6, 1, -5; scale 0.3, 2, 3; rot ~15deg — approximate AABB)
    colliders[idx++] = {{-6.6f, 0.0f, -6.6f}, {-5.4f, 2.0f, -3.4f}};
    // Right wall fragment (center 6, 0.8, -4; scale 0.3, 1.6, 2.5)
    colliders[idx++] = {{5.4f, 0.0f, -5.3f}, {6.6f, 1.6f, -2.7f}};
    // Fallen lintel (center -5.5, 0.2, -2.5; large but flat)
    colliders[idx++] = {{-6.8f, 0.0f, -2.9f}, {-4.2f, 0.45f, -2.1f}};

    // Scattered ruins (7 remaining slots)
    struct RuinBox { glm::vec3 pos; glm::vec3 half_ext; };
    RuinBox ruins[] = {
        {{-5, 0.2f, 2},     {0.4f, 0.2f, 0.3f}},
        {{ 4, 0.15f, 3},    {0.25f, 0.15f, 0.35f}},
        {{-2, 0.25f, 5},    {0.5f, 0.25f, 0.2f}},
        {{ 6, 0.3f, -2},    {0.3f, 0.3f, 0.3f}},
        {{-6, 0.1f, -8},    {0.2f, 0.1f, 0.45f}},
        {{ 2, 0.18f, 7},    {0.35f, 0.175f, 0.25f}},
        {{-4, 0.2f, -4},    {0.275f, 0.2f, 0.225f}},
    };
    for (int i = 0; i < 7 && idx < 16; i++) {
        colliders[idx++] = {
            ruins[i].pos - ruins[i].half_ext,
            ruins[i].pos + ruins[i].half_ext
        };
    }

    return colliders;
}

// ============================================================================
// Engine
// ============================================================================
bool Engine::Initialize(const std::string& window_title, int width, int height) {
    Logger::Init("unsuffered.log");
    Logger::Info("=== UNSUFFERED - THE FALLEN ===");
    Logger::Info("Initializing engine...");

    if (!m_window.Create(window_title, width, height)) { Logger::Error("Window creation failed"); return false; }
    Logger::Info("Window created: {}x{}", width, height);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) { Logger::Error("GLEW: {}", reinterpret_cast<const char*>(glewGetErrorString(err))); return false; }
    Logger::Info("OpenGL version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    if (!m_assets.Initialize("assets/")) { Logger::Error("AssetManager failed"); return false; }
    if (!m_audio.Initialize()) { Logger::Warn("Audio failed - continuing without"); }
    else { m_audio.LoadManifest("assets/data/audio_manifest.json"); Logger::Info("Audio manifest loaded"); }
    if (!m_renderer.Initialize(width, height)) { Logger::Error("Renderer failed"); return false; }
    m_game_state.Initialize();
    m_audio.PlayMusic("title_theme");

    m_running = true;
    Logger::Info("Engine initialized successfully");
    Logger::Info("Controls: WASD=move Grak, Right-click+Mouse=orbit camera, Q/E=zoom, C=corruption, ESC=quit");
    return true;
}

void Engine::Run() {
    constexpr double FIXED_DT = 1.0 / 60.0;
    double accumulator = 0.0;
    auto previous = Timer::Now();

    // Grak state
    glm::vec3 grak_pos = {0.0f, 0.0f, 3.0f};
    float grak_facing = 0.0f; // Radians, 0 = facing -Z
    float grak_speed = 3.5f;
    constexpr float GRAK_RADIUS = 0.35f; // Collision radius

    // Camera orbit
    float cam_yaw = 0.0f;
    float cam_pitch = 25.0f;
    float cam_distance = 7.0f;
    float corruption = 0.0f;
    bool mouse_captured = false;

    // Build world collision geometry once
    auto world_colliders = BuildWorldColliders();
    Logger::Info("Collision system: {} AABB colliders active", world_colliders.size());

    while (m_running) {
        auto current = Timer::Now();
        double frame_time = current - previous;
        previous = current;
        if (frame_time > 0.25) frame_time = 0.25;
        float dt = static_cast<float>(frame_time);

        m_input.Poll();
        if (m_input.QuitRequested() || m_input.IsKeyDown(SDL_SCANCODE_ESCAPE)) { m_running = false; continue; }

        // Mouse capture (right click)
        if (m_input.IsMouseDown(3)) {
            if (!mouse_captured) { SDL_SetRelativeMouseMode(SDL_TRUE); mouse_captured = true; }
        } else {
            if (mouse_captured) { SDL_SetRelativeMouseMode(SDL_FALSE); mouse_captured = false; }
        }

        // Camera rotation
        if (mouse_captured) {
            auto delta = m_input.GetMouseDelta();
            cam_yaw -= delta.x * 0.25f;
            cam_pitch -= delta.y * 0.25f;
            cam_pitch = std::clamp(cam_pitch, -10.0f, 75.0f);
        }

        // Zoom
        if (m_input.IsKeyDown(SDL_SCANCODE_Q)) cam_distance = std::max(3.0f, cam_distance - 6.0f * dt);
        if (m_input.IsKeyDown(SDL_SCANCODE_E)) cam_distance = std::min(20.0f, cam_distance + 6.0f * dt);

        // Grak movement (WASD relative to camera yaw)
        float yaw_rad = glm::radians(cam_yaw);
        glm::vec3 forward = {-std::sin(yaw_rad), 0, -std::cos(yaw_rad)};
        glm::vec3 right = {std::cos(yaw_rad), 0, -std::sin(yaw_rad)};

        glm::vec3 move_dir{0};
        if (m_input.IsKeyDown(SDL_SCANCODE_W)) move_dir += forward;
        if (m_input.IsKeyDown(SDL_SCANCODE_S)) move_dir -= forward;
        if (m_input.IsKeyDown(SDL_SCANCODE_A)) move_dir -= right;
        if (m_input.IsKeyDown(SDL_SCANCODE_D)) move_dir += right;

        if (glm::length(move_dir) > 0.01f) {
            move_dir = glm::normalize(move_dir);
            grak_pos += move_dir * grak_speed * dt;
            grak_pos.y = 0.0f; // Stay on ground
            grak_facing = std::atan2(-move_dir.x, -move_dir.z);

            // === COLLISION RESOLUTION ===
            // Run multiple iterations for corner cases where Grak touches two objects
            for (int iter = 0; iter < 3; iter++) {
                for (const auto& box : world_colliders) {
                    glm::vec3 push = ResolveAABBCollision(grak_pos, GRAK_RADIUS, box);
                    grak_pos += push;
                }
            }

            // Clamp to world bounds
            grak_pos.x = std::clamp(grak_pos.x, -11.0f, 11.0f);
            grak_pos.z = std::clamp(grak_pos.z, -11.0f, 11.0f);
        }

        bool is_moving = glm::length(move_dir) > 0.01f;
        m_renderer.SetGrakPosition(grak_pos);
        m_renderer.SetGrakVelocity(is_moving ? move_dir * grak_speed : glm::vec3(0));
        m_renderer.SetGrakFacing(grak_facing);

        // Corruption toggle
        static bool c_was = false;
        bool c_now = m_input.IsKeyDown(SDL_SCANCODE_C);
        if (c_now && !c_was) {
            corruption += 0.25f;
            if (corruption > 1.01f) corruption = 0.0f;
            m_renderer.SetCorruptionLevel(corruption);
            Logger::Info("Corruption: {:.0f}%", corruption * 100);
        }
        c_was = c_now;

        // Third-person camera following Grak
        glm::vec3 cam_target = grak_pos + glm::vec3(0, 0.8f, 0);
        float pr = glm::radians(cam_pitch);
        float yr = glm::radians(cam_yaw);
        glm::vec3 cam_pos = cam_target + glm::vec3(
            cam_distance * std::cos(pr) * std::sin(yr),
            cam_distance * std::sin(pr),
            cam_distance * std::cos(pr) * std::cos(yr)
        );
        m_renderer.GetCamera().LookAt(cam_pos, cam_target);

        // Audio
        m_audio.Update();
        m_audio.SetListenerPosition(cam_pos, glm::normalize(cam_target - cam_pos));

        // Game logic
        accumulator += frame_time;
        while (accumulator >= FIXED_DT) { m_game_state.Update(FIXED_DT); accumulator -= FIXED_DT; }

        // Render
        m_renderer.BeginFrame();
        m_renderer.Render(accumulator / FIXED_DT);
        m_window.SwapBuffers();
    }
}

void Engine::Shutdown() {
    Logger::Info("Shutting down engine...");
    m_audio.Shutdown(); m_renderer.Shutdown(); m_window.Destroy();
    Logger::Info("Engine shut down cleanly");
}

} // namespace Unsuffered
