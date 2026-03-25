#pragma once
// ============================================================================
// Renderer - Multi-pass deferred pipeline with cel shading, outlines,
//            corruption post-process, shadow mapping, particles, and
//            procedural character rendering for Grak the Hobgoblin.
// ============================================================================
#include "core/Types.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <cmath>
#include <memory>

namespace Unsuffered {

// === Shader Manager ===
class ShaderManager {
public:
    GLuint LoadShader(const std::string& name, const std::string& vert_path, const std::string& frag_path);
    void Use(const std::string& name);
    void SetMat4(const std::string& n, const std::string& u, const glm::mat4& v);
    void SetMat3(const std::string& n, const std::string& u, const glm::mat3& v);
    void SetFloat(const std::string& n, const std::string& u, float v);
    void SetInt(const std::string& n, const std::string& u, int v);
    void SetVec2(const std::string& n, const std::string& u, const glm::vec2& v);
    void SetVec3(const std::string& n, const std::string& u, const glm::vec3& v);
    GLuint GetProgram(const std::string& n) const;
    void Shutdown();
private:
    std::unordered_map<std::string, GLuint> m_programs;
    GLuint CompileShader(GLenum type, const std::string& source);
};

// === Camera ===
class Camera {
public:
    void SetPerspective(float fov, float aspect, float np, float fp) {
        m_projection = glm::perspective(glm::radians(fov), aspect, np, fp);
    }
    void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = {0,1,0}) {
        m_position = eye; m_forward = glm::normalize(target - eye);
        m_view = glm::lookAt(eye, target, up);
    }
    const glm::mat4& GetView() const { return m_view; }
    const glm::mat4& GetProjection() const { return m_projection; }
    const glm::vec3& GetPosition() const { return m_position; }
    const glm::vec3& GetForward() const { return m_forward; }
private:
    glm::mat4 m_view{1.0f}, m_projection{1.0f};
    glm::vec3 m_position{0,2,6}, m_forward{0,0,-1};
};

// === Core Geometry ===
struct Vertex { glm::vec3 position, normal; glm::vec2 texcoord; };

class Mesh {
public:
    void Create(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);
    void Draw() const;
    void Destroy();
    bool IsValid() const { return m_vao != 0; }
private:
    GLuint m_vao=0, m_vbo=0, m_ebo=0; uint32_t m_index_count=0;
};

// === Framebuffer Objects ===
struct GBuffer {
    GLuint fbo=0, position_tex=0, normal_tex=0, albedo_tex=0, depth_rbo=0;
    void Create(int w, int h); void Bind() const; void Unbind() const; void Destroy();
};

struct SceneFBO {
    GLuint fbo=0, color_tex=0;
    void Create(int w, int h); void Bind() const; void Unbind() const; void Destroy();
};

// === Shadow Map ===
struct ShadowMap {
    GLuint fbo = 0, depth_tex = 0;
    int resolution = 2048;
    glm::mat4 light_space_matrix{1.0f};
    void Create(int res);
    void Bind() const;
    void Unbind() const;
    void Destroy();
};

// === Region Visual Profile ===
struct RegionVisualProfile {
    glm::vec3 dominant_hue{0.4f,0.35f,0.3f};
    int cel_steps = 4;
    glm::vec3 outline_color{0.12f,0.09f,0.07f};
    RegionID region_id = 0;
};

// === Particle System ===
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float life;          // Remaining life [0,1]
    float max_life;
    float size;
    float rotation;
    float rotation_speed;
};

class ParticleSystem {
public:
    void Initialize(int max_particles);
    void Shutdown();
    void Update(float dt);
    void Emit(const glm::vec3& origin, const glm::vec3& color, int count,
              float speed_min, float speed_max, float life_min, float life_max,
              float size_min, float size_max);
    void EmitAsh(float dt, const glm::vec3& center, float radius);
    void EmitCovenantAura(float dt, const glm::vec3& altar_pos, float intensity);
    void EmitFootstep(const glm::vec3& pos);
    void Draw(ShaderManager& shaders, const Mesh& point_mesh) const;
    int GetActiveCount() const { return m_active_count; }
private:
    std::vector<Particle> m_particles;
    int m_active_count = 0;
    float m_ash_accumulator = 0.0f;
    float m_aura_accumulator = 0.0f;
};

// === Procedural Character (Grak the Hobgoblin) ===
// Concept art: hunched, battle-worn scavenger with massive backpack, walking staff,
//              tattered layered clothing, armor scraps, pouches, bedroll, pots/pans.
// Articulated body built from shaped primitives with ~40+ parts.
struct GrakLimb {
    Mesh mesh;
    glm::vec3 anchor_offset;
    glm::vec3 rest_scale;
    glm::vec3 color;
    float swing_phase = 0.0f;
    float swing_amplitude = 0.0f;
    float swing_speed = 1.0f;
};

enum class GrakAnimState : uint8_t {
    Idle, Walking, Running, CovenantPulse
};

class GrakCharacter {
public:
    void Initialize();
    void Shutdown();
    void Update(float dt, const glm::vec3& velocity, float corruption);
    void Draw(ShaderManager& shaders, const glm::vec3& position, float facing_angle,
              float time, float corruption);
    GrakAnimState GetAnimState() const { return m_anim_state; }
private:
    // Body meshes
    Mesh m_torso, m_head, m_jaw;
    Mesh m_upper_arm_l, m_upper_arm_r, m_lower_arm_l, m_lower_arm_r;
    Mesh m_hand_l, m_hand_r;
    Mesh m_upper_leg_l, m_upper_leg_r, m_lower_leg_l, m_lower_leg_r;
    Mesh m_foot_l, m_foot_r;
    Mesh m_pauldron_l, m_pauldron_r;
    Mesh m_eye_l, m_eye_r;
    Mesh m_covenant_brand;
    Mesh m_belt, m_loincloth;
    Mesh m_ear_l, m_ear_r;
    Mesh m_tusk_l, m_tusk_r;

    // Gear meshes (concept art: heavy scavenger loadout)
    Mesh m_backpack_main, m_backpack_top;     // Huge pack
    Mesh m_bedroll;                            // Rolled blanket on top
    Mesh m_staff;                              // Walking staff in right hand
    Mesh m_staff_top;                          // Staff knob/wrap
    Mesh m_pouch_l, m_pouch_r, m_pouch_back;  // Belt pouches
    Mesh m_pot;                                // Hanging pot
    Mesh m_pan;                                // Hanging pan
    Mesh m_chest_strap;                        // Cross-chest leather strap
    Mesh m_leg_wrap_l, m_leg_wrap_r;           // Leg wrappings/bandages
    Mesh m_arm_wrap_l;                         // Forearm bandage
    Mesh m_chest_plate;                        // Scavenged breastplate fragment
    Mesh m_hood;                               // Tattered hood/cowl

    // Animation
    GrakAnimState m_anim_state = GrakAnimState::Idle;
    float m_walk_phase = 0.0f;
    float m_idle_phase = 0.0f;
    float m_covenant_pulse = 0.0f;
    float m_blink_timer = 0.0f;
    bool  m_is_blinking = false;
    float m_corruption_twitch = 0.0f;

    // Helpers
    void DrawLimb(ShaderManager& shaders, const Mesh& mesh,
                  const glm::mat4& parent, const glm::vec3& offset,
                  const glm::vec3& scale, const glm::vec3& color,
                  float rot_angle = 0.0f, const glm::vec3& rot_axis = {1,0,0});
};

// === Model Data Types (used by both Renderer and ModelLoader) ===

// Material extracted from model file (OBJ .mtl, glTF PBR, etc.)
struct ModelMaterial {
    std::string name;
    glm::vec3 diffuse_color{0.6f, 0.6f, 0.6f};
    glm::vec3 specular_color{0.2f, 0.2f, 0.2f};
    glm::vec3 ambient_color{0.1f, 0.1f, 0.1f};
    glm::vec3 emissive_color{0.0f, 0.0f, 0.0f};
    float shininess = 32.0f;
    float opacity = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.8f;
    std::string diffuse_texture_path;
    std::string normal_texture_path;
    GLuint diffuse_texture = 0;
};

// Parameters for procedural vertex animation (passed to shaders)
struct ModelAnimParams {
    float walk_phase = 0.0f;     // Walk cycle phase [0, 2*PI]
    float walk_amplitude = 0.0f; // 0 = idle, 1 = full walk
    float idle_phase = 0.0f;     // Idle breathing phase
    float time = 0.0f;           // Continuous time for saccades, breathing, noise
    glm::vec3 aabb_min{0};       // Model bounding box (local space)
    glm::vec3 aabb_max{0};
    bool enabled = false;        // Whether to animate this draw call
};

// A single sub-mesh within a model (one mesh + one material)
struct ModelSubMesh {
    Mesh mesh;
    ModelMaterial material;
    std::string name;
    glm::mat4 local_transform{1.0f};
    glm::vec3 aabb_min{0};
    glm::vec3 aabb_max{0};
};

// A complete loaded model (may contain multiple sub-meshes)
struct Model {
    std::string path;
    std::string name;
    std::vector<ModelSubMesh> meshes;
    glm::vec3 aabb_min{0};
    glm::vec3 aabb_max{0};
    glm::vec3 center{0};
    float bounding_radius = 0.0f;

    void Destroy() {
        for (auto& sm : meshes) {
            sm.mesh.Destroy();
            if (sm.material.diffuse_texture) {
                glDeleteTextures(1, &sm.material.diffuse_texture);
                sm.material.diffuse_texture = 0;
            }
        }
        meshes.clear();
    }
    bool IsValid() const { return !meshes.empty(); }
};

// A placed instance of a model in the scene
struct ModelInstance {
    std::shared_ptr<Model> model;
    glm::mat4 transform{1.0f};
    glm::vec3 color_override{-1.0f};
    float scale_factor = 1.0f;
    bool cast_shadow = true;
    bool visible = true;
    std::string tag;
};

// Forward declaration — full definition in ModelLoader.h
class ModelLoader;

// === Main Renderer ===
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool Initialize(int width, int height);
    void Shutdown();
    void BeginFrame();
    void Render(double alpha);
    void EndFrame();
    void SetRegionProfile(const RegionVisualProfile& p) { m_current_profile = p; }
    void SetCorruptionLevel(float l) { m_corruption = l; }
    void SetGrakPosition(const glm::vec3& pos) { m_grak_pos = pos; }
    void SetGrakVelocity(const glm::vec3& vel) { m_grak_vel = vel; }
    void SetGrakFacing(float angle) { m_grak_facing = angle; }
    ShaderManager& GetShaderManager() { return m_shaders; }
    Camera& GetCamera() { return m_camera; }
    ModelLoader& GetModelLoader();  // Defined in Renderer.cpp

    // Model instance management — place loaded models in the scene
    void AddModelInstance(const ModelInstance& instance);
    void ClearModelInstances();

    // Load and set the character model (replaces procedural Grak)
    bool LoadCharacterModel(const std::string& path, float scale = 1.0f);

    // Load scene models from a directory
    int LoadSceneModels(const std::string& directory);

private:
    ShaderManager m_shaders;
    Camera m_camera;
    GBuffer m_gbuffer;
    SceneFBO m_scene_fbo;
    SceneFBO m_cel_fbo;        // Intermediate for cel -> corruption chain
    ShadowMap m_shadow_map;
    RegionVisualProfile m_current_profile;
    int m_width=0, m_height=0;

    // Geometry (procedural fallbacks)
    Mesh m_cube, m_ground, m_sphere;

    // Character (procedural fallback)
    GrakCharacter m_grak;
    bool m_use_procedural_grak = true;   // Falls back to procedural if no model loaded
    float m_grak_ground_offset = 0.0f;  // Y offset to plant feet on ground
    float m_grak_model_scale = 1.0f;    // Auto-scale factor for loaded model

    // Model loader (Assimp-based, supports OBJ/glTF/FBX/etc.)
    // Raw pointer to avoid incomplete-type issues with unique_ptr across TUs.
    // Lifetime managed explicitly in Initialize()/Shutdown().
    ModelLoader* m_model_loader = nullptr;

    // Loaded character model (replaces procedural Grak when loaded)
    std::shared_ptr<Model> m_grak_model;

    // Scene model instances (loaded from files)
    std::vector<ModelInstance> m_scene_models;
    bool m_has_scene_models = false;

    // Particles
    ParticleSystem m_particles;

    GLuint m_fullscreen_vao = 0;
    float m_time = 0.0f, m_corruption = 0.0f;
    glm::vec3 m_grak_pos{0, 0, 0};
    glm::vec3 m_grak_vel{0, 0, 0};
    float m_grak_facing = 0.0f;

    // 3D model walk animation state (separate from procedural GrakCharacter)
    float m_model_walk_phase = 0.0f;
    float m_model_idle_phase = 0.0f;

    // Helper: build transform and animation params for the loaded Grak model
    glm::mat4 BuildGrakModelTransform() const;
    ModelAnimParams BuildGrakAnimParams() const;

    // Render passes
    void ShadowPass();
    void GeometryPass();
    void CelShadingPass();
    void CorruptionPass();
    void OutlinePass();

    // Draw loaded model instances (geometry + shadow)
    void DrawSceneModels();
    void DrawSceneModelsShadow();

    // Setup
    void CreateTestScene();
    void DrawFullscreenTriangle();
};

} // namespace Unsuffered
