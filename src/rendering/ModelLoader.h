#pragma once
// ============================================================================
// ModelLoader - Assimp-based model loading for OBJ, glTF, FBX, etc.
//
// Loads 3D models into the engine's Mesh format. Supports:
//   - OBJ (.obj + .mtl)          — Wavefront, most common export format
//   - glTF 2.0 (.gltf + .glb)   — Modern standard, PBR materials
//   - FBX (.fbx)                 — Autodesk format, rigged/animated
//   - Plus 40+ other formats via Assimp
//
// Model data types (ModelMaterial, ModelSubMesh, Model, ModelInstance) are
// defined in Renderer.h to avoid circular dependencies.
//
// Texture loading: supports both external image files (via stb_image) and
// embedded textures in GLB containers (via Assimp GetEmbeddedTexture +
// stb_image decode). TRELLIS models embed textures as compressed PNG/WebP.
// ============================================================================

#include "rendering/Renderer.h"  // For Mesh, Vertex, ShaderManager, Model types

// Include Assimp headers BEFORE namespace Unsuffered so aiNode, aiScene, etc.
// are properly declared in the global namespace (not Unsuffered::aiNode)
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <string>
#include <memory>
#include <unordered_map>

// Forward declaration for embedded texture support
struct aiTexture;

namespace Unsuffered {

// ============================================================================
// ModelLoader — loads, caches, and manages 3D models via Assimp
// ============================================================================
class ModelLoader {
public:
    bool Initialize();
    void Shutdown();

    // Load a model from file. Returns cached version if already loaded.
    std::shared_ptr<Model> Load(const std::string& path);

    // Load with explicit scale and axis correction
    std::shared_ptr<Model> Load(const std::string& path, float scale,
                                 bool flip_uvs = false, bool gen_normals = true);

    // Load a texture from file (PNG, JPG, BMP, TGA via stb_image)
    GLuint LoadTexture(const std::string& path);

    // Load an embedded texture from a GLB container (compressed or uncompressed)
    GLuint LoadEmbeddedTexture(const aiTexture* ai_tex, const std::string& cache_key);

    // Upload raw RGBA pixel data to GPU and return texture handle
    static GLuint UploadTextureToGPU(unsigned char* data, int width, int height);

    // Get cached model by path
    std::shared_ptr<Model> GetCached(const std::string& path) const;

    // Evict a model from cache
    void Evict(const std::string& path);

    // Get load statistics
    int GetCachedModelCount() const { return static_cast<int>(m_model_cache.size()); }
    int GetCachedTextureCount() const { return static_cast<int>(m_texture_cache.size()); }
    int GetTotalVertexCount() const { return m_total_vertices; }
    int GetTotalTriangleCount() const { return m_total_triangles; }

    // Draw a model instance using the gbuffer shader
    static void DrawModel(ShaderManager& shaders, const Model& model, const glm::mat4& transform,
                          const glm::vec3& color_override = {-1.0f, -1.0f, -1.0f});

    // Draw a model with procedural vertex animation (limb movement)
    static void DrawModelAnimated(ShaderManager& shaders, const Model& model,
                                   const glm::mat4& transform, const ModelAnimParams& anim,
                                   const glm::vec3& color_override = {-1.0f, -1.0f, -1.0f});

    // Draw a model for the shadow pass (depth only)
    static void DrawModelShadow(ShaderManager& shaders, const Model& model, const glm::mat4& transform);

    // Draw a model shadow with procedural vertex animation
    static void DrawModelShadowAnimated(ShaderManager& shaders, const Model& model,
                                         const glm::mat4& transform, const ModelAnimParams& anim);

private:
    std::unordered_map<std::string, std::shared_ptr<Model>> m_model_cache;
    std::unordered_map<std::string, GLuint> m_texture_cache;
    int m_total_vertices = 0;
    int m_total_triangles = 0;

    // Internal: process Assimp scene into our Model format
    void ProcessNode(const aiNode* node, const aiScene* scene,
                     Model& model, const glm::mat4& parent_transform,
                     const std::string& base_dir);

    ModelSubMesh ProcessMesh(const aiMesh* mesh, const aiScene* scene,
                              const glm::mat4& node_transform, const std::string& base_dir);

    // ProcessMaterial now takes the aiScene for embedded texture access
    ModelMaterial ProcessMaterial(const aiMaterial* mat, const aiScene* scene,
                                  const std::string& base_dir);

    // Compute combined AABB for the whole model
    void ComputeModelBounds(Model& model);
};

} // namespace Unsuffered
