// ============================================================================
// ModelLoader — Assimp-based model loading implementation
// Supports OBJ, glTF 2.0, FBX, DAE, and 40+ other 3D formats.
//
// Texture loading pipeline:
//   1. Check for embedded textures (GLB containers) via Assimp GetEmbeddedTexture
//   2. Decode compressed (PNG) with stb_image or handle uncompressed ARGB8888
//   3. Fall back to external file loading via stb_image
//   4. Upload decoded RGBA to GPU with mipmaps and anisotropic filtering
// ============================================================================

#include "rendering/ModelLoader.h"  // Includes Renderer.h with all model types
#include "stb_image.h"
#include "core/Logger.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Unsuffered {

// ============================================================================
// Helpers
// ============================================================================

// Convert Assimp 4x4 matrix to glm::mat4
static glm::mat4 AiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

// Convert Assimp vec3 to glm::vec3
static glm::vec3 AiToGlm(const aiVector3D& v) {
    return {v.x, v.y, v.z};
}

// Convert Assimp color3 to glm::vec3
static glm::vec3 AiToGlm(const aiColor3D& c) {
    return {c.r, c.g, c.b};
}

// ============================================================================
// Initialize / Shutdown
// ============================================================================

bool ModelLoader::Initialize() {
    Logger::Info("ModelLoader initialized (Assimp: OBJ, glTF, FBX, +40 formats)");
    return true;
}

void ModelLoader::Shutdown() {
    // Destroy all cached textures
    for (auto& [path, tex] : m_texture_cache) {
        if (tex) glDeleteTextures(1, &tex);
    }
    m_texture_cache.clear();

    // Destroy all cached models
    for (auto& [path, model] : m_model_cache) {
        if (model) model->Destroy();
    }
    m_model_cache.clear();

    m_total_vertices = 0;
    m_total_triangles = 0;
    Logger::Info("ModelLoader shutdown — all models and textures released");
}

// ============================================================================
// Cache Management
// ============================================================================

std::shared_ptr<Model> ModelLoader::GetCached(const std::string& path) const {
    auto it = m_model_cache.find(path);
    return (it != m_model_cache.end()) ? it->second : nullptr;
}

void ModelLoader::Evict(const std::string& path) {
    auto it = m_model_cache.find(path);
    if (it != m_model_cache.end()) {
        if (it->second) it->second->Destroy();
        m_model_cache.erase(it);
    }
}

// ============================================================================
// Load Model
// ============================================================================

std::shared_ptr<Model> ModelLoader::Load(const std::string& path) {
    return Load(path, 1.0f, false, true);
}

std::shared_ptr<Model> ModelLoader::Load(const std::string& path, float scale,
                                          bool flip_uvs, bool gen_normals) {
    // Check cache first
    auto it = m_model_cache.find(path);
    if (it != m_model_cache.end()) {
        Logger::Info("ModelLoader: cache hit for '{}'", path);
        return it->second;
    }

    // Verify file exists
    if (!std::filesystem::exists(path)) {
        Logger::Error("ModelLoader: file not found: '{}'", path);
        return nullptr;
    }

    // Determine file extension for logging
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    Logger::Info("ModelLoader: loading '{}' (format: {})", path, ext);

    // Configure Assimp import flags
    unsigned int flags =
        aiProcess_Triangulate |           // Convert all faces to triangles
        aiProcess_CalcTangentSpace |      // For normal mapping
        aiProcess_JoinIdenticalVertices | // Optimize vertex count
        aiProcess_SortByPType |           // Split meshes by primitive type
        aiProcess_OptimizeMeshes |        // Merge small meshes
        aiProcess_ImproveCacheLocality;   // Optimize for GPU cache

    if (flip_uvs) flags |= aiProcess_FlipUVs;

    // GenSmoothNormals and GenNormals CONFLICT — only use one!
    // Smooth normals look better with cel-shading
    if (gen_normals) flags |= aiProcess_GenSmoothNormals;

    // Import with Assimp
    Assimp::Importer importer;

    // Set import properties for glTF
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Logger::Error("ModelLoader: Assimp error loading '{}': {}", path, importer.GetErrorString());
        return nullptr;
    }

    // Log embedded texture info
    if (scene->mNumTextures > 0) {
        Logger::Info("ModelLoader: GLB contains {} embedded textures", scene->mNumTextures);
        for (unsigned int t = 0; t < scene->mNumTextures; t++) {
            const aiTexture* tex = scene->mTextures[t];
            if (tex->mHeight == 0) {
                Logger::Info("  Texture[{}]: compressed, {} bytes, format hint: '{}'",
                             t, tex->mWidth, tex->achFormatHint);
            } else {
                Logger::Info("  Texture[{}]: uncompressed, {}x{}", t, tex->mWidth, tex->mHeight);
            }
        }
    }

    // Create our Model
    auto model = std::make_shared<Model>();
    model->path = path;
    model->name = std::filesystem::path(path).stem().string();

    // Get base directory for resolving texture paths
    std::string base_dir = std::filesystem::path(path).parent_path().string();
    if (base_dir.empty()) base_dir = ".";

    // Apply global scale transform
    glm::mat4 root_transform = glm::scale(glm::mat4(1.0f), glm::vec3(scale));

    // Process the node hierarchy recursively
    ProcessNode(scene->mRootNode, scene, *model, root_transform, base_dir);

    // Compute combined bounds
    ComputeModelBounds(*model);

    // Cache it
    m_model_cache[path] = model;

    Logger::Info("ModelLoader: loaded '{}' — {} sub-meshes, AABB [{:.2f},{:.2f},{:.2f}] to [{:.2f},{:.2f},{:.2f}], radius {:.2f}",
                 model->name, model->meshes.size(),
                 model->aabb_min.x, model->aabb_min.y, model->aabb_min.z,
                 model->aabb_max.x, model->aabb_max.y, model->aabb_max.z,
                 model->bounding_radius);

    return model;
}

// ============================================================================
// Process Scene Nodes (recursive)
// ============================================================================

void ModelLoader::ProcessNode(const aiNode* node, const aiScene* scene,
                               Model& model, const glm::mat4& parent_transform,
                               const std::string& base_dir) {
    // Combine parent transform with this node's local transform
    glm::mat4 node_transform = parent_transform * AiToGlm(node->mTransformation);

    // Process each mesh in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        model.meshes.push_back(ProcessMesh(mesh, scene, node_transform, base_dir));
    }

    // Recurse into children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, model, node_transform, base_dir);
    }
}

// ============================================================================
// Process a Single Mesh
// ============================================================================

ModelSubMesh ModelLoader::ProcessMesh(const aiMesh* ai_mesh, const aiScene* scene,
                                       const glm::mat4& node_transform,
                                       const std::string& base_dir) {
    ModelSubMesh submesh;
    submesh.name = ai_mesh->mName.C_Str();
    submesh.local_transform = node_transform;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(ai_mesh->mNumVertices);
    submesh.aabb_min = glm::vec3(std::numeric_limits<float>::max());
    submesh.aabb_max = glm::vec3(std::numeric_limits<float>::lowest());

    // --- Extract vertices ---
    for (unsigned int i = 0; i < ai_mesh->mNumVertices; i++) {
        Vertex v;

        // Position
        v.position = AiToGlm(ai_mesh->mVertices[i]);

        // Update sub-mesh AABB
        submesh.aabb_min = glm::min(submesh.aabb_min, v.position);
        submesh.aabb_max = glm::max(submesh.aabb_max, v.position);

        // Normal
        if (ai_mesh->HasNormals()) {
            v.normal = AiToGlm(ai_mesh->mNormals[i]);
        } else {
            v.normal = {0, 1, 0};
        }

        // Texture coordinates (first UV set)
        if (ai_mesh->mTextureCoords[0]) {
            v.texcoord = {ai_mesh->mTextureCoords[0][i].x,
                          ai_mesh->mTextureCoords[0][i].y};
        } else {
            v.texcoord = {0, 0};
        }

        vertices.push_back(v);
    }

    // --- Extract indices (faces — already triangulated by Assimp) ---
    for (unsigned int i = 0; i < ai_mesh->mNumFaces; i++) {
        const aiFace& face = ai_mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    // --- Extract material (now passes scene for embedded texture access) ---
    if (ai_mesh->mMaterialIndex < scene->mNumMaterials) {
        submesh.material = ProcessMaterial(scene->mMaterials[ai_mesh->mMaterialIndex],
                                            scene, base_dir);
    }

    // --- Create GPU mesh ---
    submesh.mesh.Create(vertices, indices);

    // Update global stats
    m_total_vertices += static_cast<int>(vertices.size());
    m_total_triangles += static_cast<int>(indices.size()) / 3;

    return submesh;
}

// ============================================================================
// Process Material — handles both external and embedded textures
// ============================================================================

ModelMaterial ModelLoader::ProcessMaterial(const aiMaterial* ai_mat, const aiScene* scene,
                                           const std::string& base_dir) {
    ModelMaterial mat;

    // Name
    aiString name;
    if (ai_mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        mat.name = name.C_Str();
    }

    // Diffuse color
    aiColor3D diffuse(0.6f, 0.6f, 0.6f);
    if (ai_mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
        mat.diffuse_color = AiToGlm(diffuse);
    }

    // Specular color
    aiColor3D specular(0.2f, 0.2f, 0.2f);
    if (ai_mat->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS) {
        mat.specular_color = AiToGlm(specular);
    }

    // Ambient color
    aiColor3D ambient(0.1f, 0.1f, 0.1f);
    if (ai_mat->Get(AI_MATKEY_COLOR_AMBIENT, ambient) == AI_SUCCESS) {
        mat.ambient_color = AiToGlm(ambient);
    }

    // Emissive color
    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    if (ai_mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        mat.emissive_color = AiToGlm(emissive);
    }

    // Shininess
    float shininess = 32.0f;
    ai_mat->Get(AI_MATKEY_SHININESS, shininess);
    mat.shininess = shininess;

    // Opacity
    float opacity = 1.0f;
    ai_mat->Get(AI_MATKEY_OPACITY, opacity);
    mat.opacity = opacity;

    // PBR properties (glTF)
    float metallic = 0.0f;
    if (ai_mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        mat.metallic = metallic;
    }
    float roughness = 0.8f;
    if (ai_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        mat.roughness = roughness;
    }

    // --- Texture loading lambda: tries embedded first, then external file ---
    auto tryLoadTexture = [&](aiTextureType type) -> GLuint {
        if (ai_mat->GetTextureCount(type) == 0) return 0;

        aiString tex_path;
        if (ai_mat->GetTexture(type, 0, &tex_path) != AI_SUCCESS) return 0;

        std::string path_str = tex_path.C_Str();
        Logger::Info("  Material '{}' has texture path: '{}' (type {})",
                     mat.name, path_str, static_cast<int>(type));

        // Check for embedded texture (GLB format: path is "*0", "*1", etc.)
        if (path_str.length() > 0 && path_str[0] == '*') {
            // Embedded texture reference
            const aiTexture* embedded = scene->GetEmbeddedTexture(path_str.c_str());
            if (embedded) {
                std::string cache_key = "embedded:" + path_str;
                GLuint tex = LoadEmbeddedTexture(embedded, cache_key);
                if (tex) {
                    Logger::Info("  Loaded embedded texture '{}' ({}x{})",
                                 path_str,
                                 embedded->mHeight == 0 ? 0 : embedded->mWidth,
                                 embedded->mHeight);
                    return tex;
                }
            } else {
                Logger::Warn("  Embedded texture '{}' not found in scene", path_str);
            }
        }

        // External file path
        std::string full_path = base_dir + "/" + path_str;
        std::replace(full_path.begin(), full_path.end(), '\\', '/');
        mat.diffuse_texture_path = full_path;

        GLuint tex = LoadTexture(full_path);
        if (tex) {
            Logger::Info("  Loaded external texture: {}", full_path);
        }
        return tex;
    };

    // Try diffuse texture types in priority order
    mat.diffuse_texture = tryLoadTexture(aiTextureType_DIFFUSE);
    if (mat.diffuse_texture == 0) {
        mat.diffuse_texture = tryLoadTexture(aiTextureType_BASE_COLOR);
    }

    // Normal map
    if (ai_mat->GetTextureCount(aiTextureType_NORMALS) > 0) {
        aiString tex_path;
        if (ai_mat->GetTexture(aiTextureType_NORMALS, 0, &tex_path) == AI_SUCCESS) {
            mat.normal_texture_path = base_dir + "/" + tex_path.C_Str();
        }
    }

    return mat;
}

// ============================================================================
// Load Texture from File (PNG, JPG, BMP, TGA via stb_image)
// ============================================================================

GLuint ModelLoader::LoadTexture(const std::string& path) {
    // Check cache
    auto it = m_texture_cache.find(path);
    if (it != m_texture_cache.end()) return it->second;

    // Check if file exists
    if (!std::filesystem::exists(path)) {
        Logger::Warn("ModelLoader: texture not found: '{}'", path);
        return 0;
    }

    // Load with stb_image
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if (!data) {
        Logger::Warn("ModelLoader: stb_image failed to load '{}': {}",
                     path, stbi_failure_reason());
        return 0;
    }

    Logger::Info("ModelLoader: decoded texture '{}' ({}x{}, {} channels)",
                 path, width, height, channels);

    GLuint tex = UploadTextureToGPU(data, width, height);
    stbi_image_free(data);

    if (tex) {
        m_texture_cache[path] = tex;
    }
    return tex;
}

// ============================================================================
// Load Embedded Texture from GLB Container
// Handles both compressed (PNG/JPG — stb_image decode) and
// uncompressed (ARGB8888 — direct pixel copy) embedded textures.
// ============================================================================

GLuint ModelLoader::LoadEmbeddedTexture(const aiTexture* ai_tex, const std::string& cache_key) {
    // Check cache
    auto it = m_texture_cache.find(cache_key);
    if (it != m_texture_cache.end()) return it->second;

    int width, height, channels;
    unsigned char* data = nullptr;

    if (ai_tex->mHeight == 0) {
        // Compressed format (PNG, JPG) — mWidth = byte count, pcData = compressed bytes
        Logger::Info("ModelLoader: decoding embedded compressed texture ({} bytes, format hint: '{}')",
                     ai_tex->mWidth, ai_tex->achFormatHint);

        stbi_set_flip_vertically_on_load(false);
        data = stbi_load_from_memory(
            reinterpret_cast<const unsigned char*>(ai_tex->pcData),
            static_cast<int>(ai_tex->mWidth),
            &width, &height, &channels, 4  // Force RGBA
        );

        if (!data) {
            Logger::Warn("ModelLoader: stb_image failed to decode embedded texture: {}",
                         stbi_failure_reason());
            return 0;
        }

        Logger::Info("ModelLoader: decoded embedded texture to {}x{} RGBA", width, height);
    } else {
        // Uncompressed ARGB8888 — mWidth x mHeight pixels, pcData = aiTexel array
        width = static_cast<int>(ai_tex->mWidth);
        height = static_cast<int>(ai_tex->mHeight);
        Logger::Info("ModelLoader: using uncompressed embedded texture ({}x{})", width, height);

        int pixel_count = width * height;
        data = static_cast<unsigned char*>(malloc(pixel_count * 4));
        const aiTexel* src = ai_tex->pcData;
        for (int i = 0; i < pixel_count; i++) {
            data[i * 4 + 0] = src[i].r;
            data[i * 4 + 1] = src[i].g;
            data[i * 4 + 2] = src[i].b;
            data[i * 4 + 3] = src[i].a;
        }
    }

    GLuint tex = UploadTextureToGPU(data, width, height);

    // Free pixel data (different allocator depending on source)
    if (ai_tex->mHeight == 0)
        stbi_image_free(data);
    else
        free(data);

    if (tex) {
        m_texture_cache[cache_key] = tex;
        Logger::Info("ModelLoader: uploaded embedded texture to GPU (handle={})", tex);
    }

    return tex;
}

// ============================================================================
// Upload RGBA Pixel Data to GPU
// Creates an OpenGL texture with mipmaps and anisotropic filtering.
// ============================================================================

GLuint ModelLoader::UploadTextureToGPU(unsigned char* data, int width, int height) {
    if (!data || width <= 0 || height <= 0) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    // Upload RGBA data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Generate mipmaps for quality downscaling
    glGenerateMipmap(GL_TEXTURE_2D);

    // Filtering: trilinear for smooth mip transitions
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Wrap mode: repeat (standard for most textures)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Anisotropic filtering: up to 8x (good quality/perf balance for RTX 3050)
    float max_aniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
    if (max_aniso > 1.0f) {
        float aniso = std::min(max_aniso, 8.0f);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, aniso);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ============================================================================
// Compute Model Bounds
// ============================================================================

void ModelLoader::ComputeModelBounds(Model& model) {
    if (model.meshes.empty()) return;

    model.aabb_min = glm::vec3(std::numeric_limits<float>::max());
    model.aabb_max = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto& sm : model.meshes) {
        // Transform sub-mesh AABB corners by its local transform
        glm::vec3 corners[8] = {
            {sm.aabb_min.x, sm.aabb_min.y, sm.aabb_min.z},
            {sm.aabb_max.x, sm.aabb_min.y, sm.aabb_min.z},
            {sm.aabb_min.x, sm.aabb_max.y, sm.aabb_min.z},
            {sm.aabb_max.x, sm.aabb_max.y, sm.aabb_min.z},
            {sm.aabb_min.x, sm.aabb_min.y, sm.aabb_max.z},
            {sm.aabb_max.x, sm.aabb_min.y, sm.aabb_max.z},
            {sm.aabb_min.x, sm.aabb_max.y, sm.aabb_max.z},
            {sm.aabb_max.x, sm.aabb_max.y, sm.aabb_max.z},
        };

        for (auto& c : corners) {
            glm::vec4 transformed = sm.local_transform * glm::vec4(c, 1.0f);
            glm::vec3 tc = glm::vec3(transformed);
            model.aabb_min = glm::min(model.aabb_min, tc);
            model.aabb_max = glm::max(model.aabb_max, tc);
        }
    }

    model.center = (model.aabb_min + model.aabb_max) * 0.5f;
    model.bounding_radius = glm::length(model.aabb_max - model.center);
}

// ============================================================================
// Draw Model (Static — G-Buffer Pass)
// ============================================================================

void ModelLoader::DrawModel(ShaderManager& shaders, const Model& model, const glm::mat4& transform,
                             const glm::vec3& color_override) {
    // Tell shader this is NOT animated
    shaders.SetFloat("gbuffer", "u_is_animated", 0.0f);

    for (const auto& sm : model.meshes) {
        glm::mat4 final_transform = transform * sm.local_transform;

        shaders.SetMat4("gbuffer", "u_model", final_transform);
        shaders.SetMat3("gbuffer", "u_normal_matrix",
                        glm::transpose(glm::inverse(glm::mat3(final_transform))));

        // Texture binding
        if (sm.material.diffuse_texture) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, sm.material.diffuse_texture);
            shaders.SetInt("gbuffer", "u_diffuse_tex", 4);
            shaders.SetFloat("gbuffer", "u_has_texture", 1.0f);
            shaders.SetFloat("gbuffer", "u_tex_brightness", 3.5f);
        } else {
            shaders.SetFloat("gbuffer", "u_has_texture", 0.0f);
        }

        if (color_override.x >= 0.0f) {
            shaders.SetVec3("gbuffer", "u_color", color_override);
        } else {
            shaders.SetVec3("gbuffer", "u_color", sm.material.diffuse_color);
        }

        sm.mesh.Draw();
    }
}

// ============================================================================
// Draw Model with Procedural Vertex Animation (G-Buffer Pass)
// ============================================================================

void ModelLoader::DrawModelAnimated(ShaderManager& shaders, const Model& model,
                                     const glm::mat4& transform, const ModelAnimParams& anim,
                                     const glm::vec3& color_override) {
    // Set animation uniforms (shared across all sub-meshes)
    shaders.SetFloat("gbuffer", "u_walk_phase", anim.walk_phase);
    shaders.SetFloat("gbuffer", "u_walk_amplitude", anim.walk_amplitude);
    shaders.SetFloat("gbuffer", "u_idle_phase", anim.idle_phase);
    shaders.SetFloat("gbuffer", "u_time", anim.time);
    shaders.SetFloat("gbuffer", "u_is_animated", anim.enabled ? 1.0f : 0.0f);

    for (const auto& sm : model.meshes) {
        glm::mat4 final_transform = transform * sm.local_transform;

        // Use per-submesh AABB (matches raw vertex positions the shader receives)
        shaders.SetVec3("gbuffer", "u_aabb_min", sm.aabb_min);
        shaders.SetVec3("gbuffer", "u_aabb_max", sm.aabb_max);

        shaders.SetMat4("gbuffer", "u_model", final_transform);
        shaders.SetMat3("gbuffer", "u_normal_matrix",
                        glm::transpose(glm::inverse(glm::mat3(final_transform))));

        // Texture binding
        if (sm.material.diffuse_texture) {
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, sm.material.diffuse_texture);
            shaders.SetInt("gbuffer", "u_diffuse_tex", 4);
            shaders.SetFloat("gbuffer", "u_has_texture", 1.0f);
            shaders.SetFloat("gbuffer", "u_tex_brightness", 3.5f);
        } else {
            shaders.SetFloat("gbuffer", "u_has_texture", 0.0f);
        }

        if (color_override.x >= 0.0f) {
            shaders.SetVec3("gbuffer", "u_color", color_override);
        } else {
            shaders.SetVec3("gbuffer", "u_color", sm.material.diffuse_color);
        }

        sm.mesh.Draw();
    }

    // Reset animation flag so subsequent static objects aren't animated
    shaders.SetFloat("gbuffer", "u_is_animated", 0.0f);
}

// ============================================================================
// Draw Model Shadow (depth-only pass)
// ============================================================================

void ModelLoader::DrawModelShadow(ShaderManager& shaders, const Model& model,
                                    const glm::mat4& transform) {
    for (const auto& sm : model.meshes) {
        glm::mat4 final_transform = transform * sm.local_transform;
        shaders.SetMat4("shadow", "u_model", final_transform);
        sm.mesh.Draw();
    }
}

// ============================================================================
// Draw Model Shadow with Procedural Vertex Animation
// ============================================================================

void ModelLoader::DrawModelShadowAnimated(ShaderManager& shaders, const Model& model,
                                            const glm::mat4& transform,
                                            const ModelAnimParams& anim) {
    shaders.SetFloat("shadow", "u_walk_phase", anim.walk_phase);
    shaders.SetFloat("shadow", "u_walk_amplitude", anim.walk_amplitude);
    shaders.SetFloat("shadow", "u_idle_phase", anim.idle_phase);
    shaders.SetFloat("shadow", "u_time", anim.time);
    shaders.SetFloat("shadow", "u_is_animated", anim.enabled ? 1.0f : 0.0f);

    for (const auto& sm : model.meshes) {
        // Use per-submesh AABB (matches raw vertex positions the shader receives)
        shaders.SetVec3("shadow", "u_aabb_min", sm.aabb_min);
        shaders.SetVec3("shadow", "u_aabb_max", sm.aabb_max);

        glm::mat4 final_transform = transform * sm.local_transform;
        shaders.SetMat4("shadow", "u_model", final_transform);
        sm.mesh.Draw();
    }

    shaders.SetFloat("shadow", "u_is_animated", 0.0f);
}

} // namespace Unsuffered
