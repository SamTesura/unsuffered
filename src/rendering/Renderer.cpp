#include "rendering/Renderer.h"
#include "rendering/ModelLoader.h"
#include "core/Logger.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <random>

namespace Unsuffered {

// Destructor must be in .cpp where ModelLoader is a complete type
Renderer::~Renderer() = default;
ModelLoader& Renderer::GetModelLoader() { return *m_model_loader; }

// ============================================================================
// ShaderManager Implementation
// ============================================================================

GLuint ShaderManager::CompileShader(GLenum type, const std::string& source) {
    GLuint s = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        Logger::Error("Shader compile: {}", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint ShaderManager::LoadShader(const std::string& name, const std::string& vp, const std::string& fp) {
    auto rf = [](const std::string& p) -> std::string {
        std::ifstream f(p);
        if (!f.is_open()) { Logger::Error("Missing shader: {}", p); return ""; }
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    std::string vs = rf(vp), fs = rf(fp);
    if (vs.empty() || fs.empty()) return 0;
    GLuint v = CompileShader(GL_VERTEX_SHADER, vs);
    GLuint f = CompileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, 1024, nullptr, log);
        Logger::Error("Link({}): {}", name, log);
        glDeleteProgram(p);
        glDeleteShader(v);
        glDeleteShader(f);
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    m_programs[name] = p;
    Logger::Info("Shader loaded: {}", name);
    return p;
}

void ShaderManager::Use(const std::string& n) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) glUseProgram(i->second);
}

void ShaderManager::SetMat4(const std::string& n, const std::string& u, const glm::mat4& v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, glm::value_ptr(v));
    }
}

void ShaderManager::SetMat3(const std::string& n, const std::string& u, const glm::mat3& v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniformMatrix3fv(l, 1, GL_FALSE, glm::value_ptr(v));
    }
}

void ShaderManager::SetFloat(const std::string& n, const std::string& u, float v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniform1f(l, v);
    }
}

void ShaderManager::SetInt(const std::string& n, const std::string& u, int v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniform1i(l, v);
    }
}

void ShaderManager::SetVec2(const std::string& n, const std::string& u, const glm::vec2& v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniform2fv(l, 1, glm::value_ptr(v));
    }
}

void ShaderManager::SetVec3(const std::string& n, const std::string& u, const glm::vec3& v) {
    auto i = m_programs.find(n);
    if (i != m_programs.end()) {
        GLint l = glGetUniformLocation(i->second, u.c_str());
        if (l >= 0) glUniform3fv(l, 1, glm::value_ptr(v));
    }
}

GLuint ShaderManager::GetProgram(const std::string& n) const {
    auto i = m_programs.find(n);
    return (i != m_programs.end()) ? i->second : 0;
}

void ShaderManager::Shutdown() {
    for (auto& [name, prog] : m_programs) {
        glDeleteProgram(prog);
    }
    m_programs.clear();
}

// ============================================================================
// Framebuffer Objects
// ============================================================================

void GBuffer::Create(int w, int h) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Position buffer (RGB32F)
    glGenTextures(1, &position_tex);
    glBindTexture(GL_TEXTURE_2D, position_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, position_tex, 0);

    // Normal buffer (RGB16F)
    glGenTextures(1, &normal_tex);
    glBindTexture(GL_TEXTURE_2D, normal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normal_tex, 0);

    // Albedo buffer (RGBA8)
    glGenTextures(1, &albedo_tex);
    glBindTexture(GL_TEXTURE_2D, albedo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, albedo_tex, 0);

    GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);

    // Depth renderbuffer
    glGenRenderbuffers(1, &depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Logger::Info("G-Buffer created ({}x{})", w, h);
}

void GBuffer::Bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
void GBuffer::Unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
void GBuffer::Destroy() {
    if (position_tex) glDeleteTextures(1, &position_tex);
    if (normal_tex) glDeleteTextures(1, &normal_tex);
    if (albedo_tex) glDeleteTextures(1, &albedo_tex);
    if (depth_rbo) glDeleteRenderbuffers(1, &depth_rbo);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    position_tex = normal_tex = albedo_tex = 0; depth_rbo = 0; fbo = 0;
}

void SceneFBO::Create(int w, int h) {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void SceneFBO::Bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
void SceneFBO::Unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
void SceneFBO::Destroy() {
    if (color_tex) glDeleteTextures(1, &color_tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    color_tex = 0; fbo = 0;
}

void ShadowMap::Create(int res) {
    resolution = res;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &depth_tex);
    glBindTexture(GL_TEXTURE_2D, depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Logger::Info("Shadow map created ({}x{})", res, res);
}
void ShadowMap::Bind() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }
void ShadowMap::Unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
void ShadowMap::Destroy() {
    if (depth_tex) glDeleteTextures(1, &depth_tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    depth_tex = 0; fbo = 0;
}

// ============================================================================
// Mesh Implementation
// ============================================================================

void Mesh::Create(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    m_index_count = static_cast<uint32_t>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);

    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    // Normal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    // Texcoord (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));

    glBindVertexArray(0);
}

void Mesh::Draw() const {
    if (m_vao == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::Destroy() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = 0;
    m_index_count = 0;
}

// ============================================================================
// Primitive Generation
// ============================================================================

static Mesh MakeCube() {
    std::vector<Vertex> v;
    auto face = [&](glm::vec3 n, glm::vec3 u, glm::vec3 r) {
        v.push_back({n*0.5f-u*0.5f-r*0.5f, n, {0,0}});
        v.push_back({n*0.5f-u*0.5f+r*0.5f, n, {1,0}});
        v.push_back({n*0.5f+u*0.5f+r*0.5f, n, {1,1}});
        v.push_back({n*0.5f+u*0.5f-r*0.5f, n, {0,1}});
    };
    face({0,0,1},{0,1,0},{1,0,0}); face({0,0,-1},{0,1,0},{-1,0,0});
    face({0,1,0},{0,0,-1},{1,0,0}); face({0,-1,0},{0,0,1},{1,0,0});
    face({1,0,0},{0,1,0},{0,0,-1}); face({-1,0,0},{0,1,0},{0,0,1});
    std::vector<uint32_t> idx;
    for (uint32_t f=0; f<6; f++) { uint32_t b=f*4; idx.insert(idx.end(),{b,b+1,b+2,b,b+2,b+3}); }
    Mesh m; m.Create(v, idx); return m;
}

static Mesh MakeSphere(int slices = 14, int stacks = 10) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    for (int j = 0; j <= stacks; j++) {
        float theta = glm::pi<float>() * static_cast<float>(j) / static_cast<float>(stacks);
        float st = std::sin(theta), ct = std::cos(theta);
        for (int i = 0; i <= slices; i++) {
            float phi = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(slices);
            float sp = std::sin(phi), cp = std::cos(phi);
            glm::vec3 pos = {cp * st * 0.5f, ct * 0.5f, sp * st * 0.5f};
            glm::vec3 norm = glm::normalize(pos);
            glm::vec2 uv = {static_cast<float>(i) / slices, static_cast<float>(j) / stacks};
            verts.push_back({pos, norm, uv});
        }
    }

    for (int j = 0; j < stacks; j++) {
        for (int i = 0; i < slices; i++) {
            uint32_t a = j * (slices + 1) + i;
            uint32_t b = a + slices + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }

    Mesh m;
    m.Create(verts, idx);
    return m;
}

// Tapered box for torso, jaw, feet
static Mesh MakeTaperedBox(float top_scale = 1.0f, float bottom_scale = 1.0f) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    float ts = top_scale * 0.5f, bs = bottom_scale * 0.5f;
    // 8 corners: top 4 (y=+0.5), bottom 4 (y=-0.5)
    glm::vec3 corners[8] = {
        {-ts, 0.5f, -ts}, {ts, 0.5f, -ts}, {ts, 0.5f, ts}, {-ts, 0.5f, ts},
        {-bs, -0.5f, -bs}, {bs, -0.5f, -bs}, {bs, -0.5f, bs}, {-bs, -0.5f, bs}
    };

    auto add_quad = [&](int a, int b, int c, int d) {
        glm::vec3 n = glm::normalize(glm::cross(corners[b] - corners[a], corners[d] - corners[a]));
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({corners[a], n, {0,0}});
        verts.push_back({corners[b], n, {1,0}});
        verts.push_back({corners[c], n, {1,1}});
        verts.push_back({corners[d], n, {0,1}});
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    };

    add_quad(0,1,2,3); // Top
    add_quad(7,6,5,4); // Bottom
    add_quad(0,3,7,4); // Left
    add_quad(2,1,5,6); // Right
    add_quad(3,2,6,7); // Front
    add_quad(1,0,4,5); // Back

    Mesh m;
    m.Create(verts, idx);
    return m;
}

// Cylinder for limbs
static Mesh MakeCylinder(int segments = 8, float radius_top = 0.5f, float radius_bot = 0.5f) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    for (int i = 0; i <= segments; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        float c = std::cos(angle), s = std::sin(angle);
        glm::vec3 nt = glm::normalize(glm::vec3(c, (radius_bot - radius_top), s));
        verts.push_back({{radius_bot * c, -0.5f, radius_bot * s}, nt,
                        {static_cast<float>(i) / segments, 0.0f}});
        verts.push_back({{radius_top * c, 0.5f, radius_top * s}, nt,
                        {static_cast<float>(i) / segments, 1.0f}});
    }

    for (int i = 0; i < segments; i++) {
        uint32_t b = i * 2;
        idx.insert(idx.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
    }

    // Top cap
    uint32_t top_center = static_cast<uint32_t>(verts.size());
    verts.push_back({{0, 0.5f, 0}, {0, 1, 0}, {0.5f, 0.5f}});
    uint32_t top_first = static_cast<uint32_t>(verts.size());
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        verts.push_back({{radius_top * std::cos(angle), 0.5f, radius_top * std::sin(angle)},
                        {0, 1, 0}, {0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)}});
    }
    for (int i = 0; i < segments; i++) {
        uint32_t next = top_first + ((i + 1) % segments);
        idx.insert(idx.end(), {top_center, top_first + static_cast<uint32_t>(i), next});
    }

    // Bottom cap
    uint32_t bot_center = static_cast<uint32_t>(verts.size());
    verts.push_back({{0, -0.5f, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    uint32_t bot_first = static_cast<uint32_t>(verts.size());
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        verts.push_back({{radius_bot * std::cos(angle), -0.5f, radius_bot * std::sin(angle)},
                        {0, -1, 0}, {0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)}});
    }
    for (int i = 0; i < segments; i++) {
        uint32_t next = bot_first + ((i + 1) % segments);
        idx.insert(idx.end(), {bot_center, next, bot_first + static_cast<uint32_t>(i)});
    }

    Mesh m;
    m.Create(verts, idx);
    return m;
}

// Wedge shape for tusks, ears
static Mesh MakeWedge() {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    glm::vec3 ft{0, 0.5f, 0.5f}, fbl{-0.5f, -0.5f, 0.5f}, fbr{0.5f, -0.5f, 0.5f};
    glm::vec3 bt{0, 0.5f, -0.5f}, bbl{-0.5f, -0.5f, -0.5f}, bbr{0.5f, -0.5f, -0.5f};

    auto add_tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({a, n, {0,0}});
        verts.push_back({b, n, {1,0}});
        verts.push_back({c, n, {0.5f,1}});
        idx.insert(idx.end(), {base, base+1, base+2});
    };
    auto add_quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({a, n, {0,0}});
        verts.push_back({b, n, {1,0}});
        verts.push_back({c, n, {1,1}});
        verts.push_back({d, n, {0,1}});
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    };

    add_tri(ft, fbl, fbr);
    add_tri(bt, bbl, bbr);
    add_quad(fbl, bbl, bbr, fbr);
    add_quad(ft, bt, bbl, fbl);
    add_quad(fbr, bbr, bt, ft);

    Mesh m;
    m.Create(verts, idx);
    return m;
}

static void DrawObj(ShaderManager& s, const Mesh& m, const glm::mat4& model, const glm::vec3& color) {
    s.SetMat4("gbuffer", "u_model", model);
    s.SetMat3("gbuffer", "u_normal_matrix", glm::transpose(glm::inverse(glm::mat3(model))));
    s.SetVec3("gbuffer", "u_color", color);
    s.SetFloat("gbuffer", "u_has_texture", 0.0f);  // Procedural geometry has no texture
    s.SetFloat("gbuffer", "u_is_animated", 0.0f);
    m.Draw();
}

static void DrawObjShadow(ShaderManager& s, const Mesh& m, const glm::mat4& model) {
    s.SetMat4("shadow", "u_model", model);
    s.SetFloat("shadow", "u_is_animated", 0.0f);
    m.Draw();
}

// ============================================================================
// ParticleSystem
// ============================================================================

void ParticleSystem::Initialize(int max_particles) {
    m_particles.resize(max_particles);
    for (auto& p : m_particles) p.life = 0.0f;
    m_active_count = 0;
    Logger::Info("Particle system initialized ({} max)", max_particles);
}

void ParticleSystem::Shutdown() { m_particles.clear(); }

void ParticleSystem::Update(float dt) {
    m_active_count = 0;
    for (auto& p : m_particles) {
        if (p.life <= 0.0f) continue;
        p.life -= dt / p.max_life;
        p.position += p.velocity * dt;
        p.velocity.y -= 0.3f * dt;
        p.rotation += p.rotation_speed * dt;
        p.size *= (1.0f - 0.3f * dt);
        m_active_count++;
    }
}

void ParticleSystem::Emit(const glm::vec3& origin, const glm::vec3& color, int count,
                           float speed_min, float speed_max, float life_min, float life_max,
                           float size_min, float size_max) {
    static std::mt19937 rng(42);
    std::uniform_real_distribution<float> speed_dist(speed_min, speed_max);
    std::uniform_real_distribution<float> life_dist(life_min, life_max);
    std::uniform_real_distribution<float> size_dist(size_min, size_max);
    std::uniform_real_distribution<float> angle_dist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> pitch_dist(-0.3f, 1.0f);

    int emitted = 0;
    for (auto& p : m_particles) {
        if (emitted >= count) break;
        if (p.life > 0.0f) continue;
        float angle = angle_dist(rng);
        float pitch = pitch_dist(rng);
        float spd = speed_dist(rng);
        p.position = origin;
        p.velocity = glm::vec3(std::cos(angle) * spd * (1.0f - std::abs(pitch)),
                               pitch * spd,
                               std::sin(angle) * spd * (1.0f - std::abs(pitch)));
        p.color = color;
        p.max_life = life_dist(rng);
        p.life = 1.0f;
        p.size = size_dist(rng);
        p.rotation = angle_dist(rng);
        p.rotation_speed = angle_dist(rng) - glm::pi<float>();
        emitted++;
    }
}

void ParticleSystem::EmitAsh(float dt, const glm::vec3& center, float radius) {
    m_ash_accumulator += dt;
    float ash_rate = 0.05f;
    while (m_ash_accumulator >= ash_rate) {
        m_ash_accumulator -= ash_rate;
        static std::mt19937 rng(123);
        std::uniform_real_distribution<float> offset(-radius, radius);
        std::uniform_real_distribution<float> gray(0.25f, 0.45f);
        float g = gray(rng);
        glm::vec3 pos = center + glm::vec3(offset(rng), 3.0f + offset(rng) * 0.3f, offset(rng));
        Emit(pos, {g, g * 0.9f, g * 0.8f}, 1, 0.05f, 0.2f, 3.0f, 6.0f, 0.02f, 0.06f);
    }
}

void ParticleSystem::EmitCovenantAura(float dt, const glm::vec3& altar_pos, float intensity) {
    m_aura_accumulator += dt;
    float rate = 0.03f / std::max(intensity, 0.1f);
    while (m_aura_accumulator >= rate) {
        m_aura_accumulator -= rate;
        static std::mt19937 rng(456);
        std::uniform_real_distribution<float> a(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> r(0.2f, 1.2f);
        float angle = a(rng);
        float rad = r(rng);
        glm::vec3 pos = altar_pos + glm::vec3(std::cos(angle) * rad, 0.3f, std::sin(angle) * rad);
        std::uniform_real_distribution<float> hue(0.0f, 1.0f);
        float h = hue(rng);
        glm::vec3 color = glm::mix(glm::vec3(0.1f, 0.8f, 0.7f), glm::vec3(0.6f, 0.2f, 0.9f), h);
        Emit(pos, color * intensity, 1, 0.3f, 0.8f, 1.0f, 2.5f, 0.03f, 0.08f);
    }
}

void ParticleSystem::EmitFootstep(const glm::vec3& pos) {
    Emit(pos, {0.35f, 0.30f, 0.25f}, 3, 0.1f, 0.4f, 0.3f, 0.8f, 0.02f, 0.04f);
}

void ParticleSystem::Draw(ShaderManager& shaders, const Mesh& point_mesh) const {
    for (const auto& p : m_particles) {
        if (p.life <= 0.0f) continue;
        float alpha = p.life;
        glm::vec3 col = p.color * alpha;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position);
        model = glm::rotate(model, p.rotation, {0, 1, 0});
        model = glm::scale(model, glm::vec3(p.size));
        DrawObj(shaders, point_mesh, model, col);
    }
}

// ============================================================================
// GrakCharacter — Procedural Hobgoblin (fallback when no GLB loaded)
// ============================================================================

namespace GrakColors {
    inline const glm::vec3 Skin{0.28f, 0.32f, 0.15f};
    inline const glm::vec3 SkinLight{0.33f, 0.38f, 0.18f};
    inline const glm::vec3 SkinDark{0.20f, 0.25f, 0.10f};
    inline const glm::vec3 Leather{0.24f, 0.17f, 0.09f};
    inline const glm::vec3 LeatherDark{0.16f, 0.11f, 0.06f};
    inline const glm::vec3 Metal{0.38f, 0.36f, 0.32f};
    inline const glm::vec3 Eyes{0.85f, 0.18f, 0.08f};
    inline const glm::vec3 Tusk{0.72f, 0.68f, 0.55f};
    inline const glm::vec3 CovenantGlow{0.08f, 0.70f, 0.60f};
    inline const glm::vec3 Cloth{0.18f, 0.14f, 0.10f};
    inline const glm::vec3 Wood{0.30f, 0.22f, 0.12f};
    inline const glm::vec3 WoodDark{0.20f, 0.14f, 0.08f};
}

void GrakCharacter::Initialize() {
    m_torso       = MakeTaperedBox(0.75f, 0.9f);
    m_head        = MakeSphere(10, 7);
    m_jaw         = MakeTaperedBox(0.6f, 0.85f);
    m_upper_arm_l = MakeCylinder(6, 0.45f, 0.40f);
    m_upper_arm_r = MakeCylinder(6, 0.45f, 0.40f);
    m_lower_arm_l = MakeCylinder(6, 0.40f, 0.30f);
    m_lower_arm_r = MakeCylinder(6, 0.40f, 0.30f);
    m_hand_l      = MakeSphere(6, 4);
    m_hand_r      = MakeSphere(6, 4);
    m_upper_leg_l = MakeCylinder(6, 0.48f, 0.38f);
    m_upper_leg_r = MakeCylinder(6, 0.48f, 0.38f);
    m_lower_leg_l = MakeCylinder(6, 0.38f, 0.32f);
    m_lower_leg_r = MakeCylinder(6, 0.38f, 0.32f);
    m_foot_l      = MakeTaperedBox(0.8f, 1.1f);
    m_foot_r      = MakeTaperedBox(0.8f, 1.1f);
    m_pauldron_l  = MakeSphere(6, 4);
    m_pauldron_r  = MakeSphere(6, 4);
    m_eye_l       = MakeSphere(6, 4);
    m_eye_r       = MakeSphere(6, 4);
    m_covenant_brand = MakeSphere(8, 6);
    m_belt        = MakeCylinder(8, 0.5f, 0.5f);
    m_loincloth   = MakeTaperedBox(0.5f, 1.0f);
    m_ear_l       = MakeWedge();
    m_ear_r       = MakeWedge();
    m_tusk_l      = MakeWedge();
    m_tusk_r      = MakeWedge();
    // Gear meshes
    m_backpack_main = MakeTaperedBox(0.85f, 0.95f);
    m_backpack_top  = MakeTaperedBox(0.90f, 0.80f);
    m_bedroll       = MakeCylinder(8, 0.5f, 0.5f);
    m_staff         = MakeCylinder(6, 0.3f, 0.35f);
    m_staff_top     = MakeSphere(6, 4);
    m_pouch_l       = MakeTaperedBox(0.85f, 1.0f);
    m_pouch_r       = MakeTaperedBox(0.85f, 1.0f);
    m_pouch_back    = MakeTaperedBox(0.90f, 1.0f);
    m_pot           = MakeCylinder(8, 0.45f, 0.5f);
    m_pan           = MakeCylinder(8, 0.5f, 0.5f);
    m_chest_strap   = MakeCylinder(8, 0.5f, 0.5f);
    m_leg_wrap_l    = MakeCylinder(6, 0.5f, 0.5f);
    m_leg_wrap_r    = MakeCylinder(6, 0.5f, 0.5f);
    m_arm_wrap_l    = MakeCylinder(6, 0.5f, 0.5f);
    m_chest_plate   = MakeTaperedBox(0.90f, 0.85f);
    m_hood          = MakeSphere(8, 6);

    Logger::Info("Grak character model initialized (procedural, 40+ parts)");
}

void GrakCharacter::Shutdown() {
    m_torso.Destroy(); m_head.Destroy(); m_jaw.Destroy();
    m_upper_arm_l.Destroy(); m_upper_arm_r.Destroy();
    m_lower_arm_l.Destroy(); m_lower_arm_r.Destroy();
    m_hand_l.Destroy(); m_hand_r.Destroy();
    m_upper_leg_l.Destroy(); m_upper_leg_r.Destroy();
    m_lower_leg_l.Destroy(); m_lower_leg_r.Destroy();
    m_foot_l.Destroy(); m_foot_r.Destroy();
    m_pauldron_l.Destroy(); m_pauldron_r.Destroy();
    m_eye_l.Destroy(); m_eye_r.Destroy();
    m_covenant_brand.Destroy();
    m_belt.Destroy(); m_loincloth.Destroy();
    m_ear_l.Destroy(); m_ear_r.Destroy();
    m_tusk_l.Destroy(); m_tusk_r.Destroy();
    m_backpack_main.Destroy(); m_backpack_top.Destroy();
    m_bedroll.Destroy(); m_staff.Destroy(); m_staff_top.Destroy();
    m_pouch_l.Destroy(); m_pouch_r.Destroy(); m_pouch_back.Destroy();
    m_pot.Destroy(); m_pan.Destroy();
    m_chest_strap.Destroy(); m_leg_wrap_l.Destroy(); m_leg_wrap_r.Destroy();
    m_arm_wrap_l.Destroy(); m_chest_plate.Destroy(); m_hood.Destroy();
}

void GrakCharacter::Update(float dt, const glm::vec3& velocity, float corruption) {
    float speed = glm::length(velocity);

    if (speed > 0.5f) {
        m_anim_state = (speed > 2.5f) ? GrakAnimState::Running : GrakAnimState::Walking;
    } else {
        m_anim_state = GrakAnimState::Idle;
    }

    float walk_speed_mult = (m_anim_state == GrakAnimState::Running) ? 8.0f : 5.0f;
    if (m_anim_state != GrakAnimState::Idle) {
        m_walk_phase += dt * walk_speed_mult;
        if (m_walk_phase > glm::two_pi<float>()) m_walk_phase -= glm::two_pi<float>();
    } else {
        m_walk_phase *= (1.0f - 3.0f * dt);
    }

    m_idle_phase += dt * 1.5f;
    if (m_idle_phase > glm::two_pi<float>()) m_idle_phase -= glm::two_pi<float>();

    m_covenant_pulse += dt * 2.5f;
    if (m_covenant_pulse > glm::two_pi<float>()) m_covenant_pulse -= glm::two_pi<float>();

    m_blink_timer -= dt;
    if (m_blink_timer <= 0.0f) {
        m_is_blinking = !m_is_blinking;
        if (m_is_blinking) {
            m_blink_timer = 0.12f;
        } else {
            static std::mt19937 rng(789);
            std::uniform_real_distribution<float> d(2.0f, 5.0f);
            m_blink_timer = d(rng);
        }
    }

    if (corruption > 0.4f) {
        static std::mt19937 rng(101);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        m_corruption_twitch = d(rng) * (corruption - 0.4f) * 0.15f;
    } else {
        m_corruption_twitch = 0.0f;
    }
}

void GrakCharacter::DrawLimb(ShaderManager& shaders, const Mesh& mesh,
                              const glm::mat4& parent, const glm::vec3& offset,
                              const glm::vec3& scale, const glm::vec3& color,
                              float rot_angle, const glm::vec3& rot_axis) {
    glm::mat4 model = parent;
    model = glm::translate(model, offset);
    if (std::abs(rot_angle) > 0.001f) {
        model = glm::rotate(model, rot_angle, rot_axis);
    }
    model = glm::scale(model, scale);
    DrawObj(shaders, mesh, model, color);
}

void GrakCharacter::Draw(ShaderManager& shaders, const glm::vec3& position, float facing_angle,
                          float time, float corruption) {
    float walk_swing = std::sin(m_walk_phase);
    float walk_swing2 = std::sin(m_walk_phase + glm::pi<float>());
    float walk_amplitude = (m_anim_state == GrakAnimState::Running) ? 0.55f :
                           (m_anim_state == GrakAnimState::Walking) ? 0.35f : 0.0f;
    float idle_bob = std::sin(m_idle_phase) * 0.015f;
    float idle_sway = std::sin(m_idle_phase * 0.7f) * 0.008f;
    float breathe = std::sin(m_idle_phase) * 0.018f;

    glm::mat4 root = glm::translate(glm::mat4(1.0f),
        position + glm::vec3(idle_sway, 0.02f + idle_bob, 0));
    root = glm::rotate(root, facing_angle + m_corruption_twitch, {0, 1, 0});

    // TORSO — hunched forward
    float torso_y = 0.68f;
    float hunch_forward = 0.18f;
    float torso_lean = walk_swing * walk_amplitude * 0.04f;
    glm::mat4 torso_mat = glm::translate(root, {0, torso_y, 0});
    torso_mat = glm::rotate(torso_mat, hunch_forward, {1, 0, 0});
    torso_mat = glm::rotate(torso_mat, torso_lean, {0, 0, 1});
    DrawLimb(shaders, m_torso, torso_mat, {0, 0, 0}, {0.52f, 0.55f + breathe, 0.35f}, GrakColors::Skin);

    // BELT
    DrawLimb(shaders, m_belt, torso_mat, {0, -0.24f, 0}, {0.55f, 0.06f, 0.38f}, GrakColors::LeatherDark);

    // LOINCLOTH
    float loincloth_swing = walk_swing * walk_amplitude * 0.08f;
    glm::mat4 loin_mat = glm::translate(torso_mat, {0, -0.32f, 0.05f});
    loin_mat = glm::rotate(loin_mat, loincloth_swing, {1, 0, 0});
    DrawLimb(shaders, m_loincloth, loin_mat, {0, 0, 0}, {0.30f, 0.18f, 0.02f}, GrakColors::Cloth);

    // HEAD
    float head_bob = std::sin(m_walk_phase * 2.0f) * walk_amplitude * 0.02f;
    glm::mat4 head_mat = glm::translate(torso_mat, {0, 0.42f + head_bob, 0.02f});
    head_mat = glm::rotate(head_mat, walk_swing * walk_amplitude * 0.03f, {0, 0, 1});
    DrawLimb(shaders, m_head, head_mat, {0, 0, 0}, {0.36f, 0.32f, 0.32f}, GrakColors::SkinLight);

    // JAW
    DrawLimb(shaders, m_jaw, head_mat, {0, -0.10f, 0.10f}, {0.26f, 0.12f, 0.16f}, GrakColors::Skin);

    // TUSKS
    DrawLimb(shaders, m_tusk_l, head_mat, {-0.08f, -0.12f, 0.14f},
             {0.03f, 0.08f, 0.03f}, GrakColors::Tusk, -0.2f, {0, 0, 1});
    DrawLimb(shaders, m_tusk_r, head_mat, {0.08f, -0.12f, 0.14f},
             {0.03f, 0.08f, 0.03f}, GrakColors::Tusk, 0.2f, {0, 0, 1});

    // EARS
    DrawLimb(shaders, m_ear_l, head_mat, {-0.18f, 0.06f, 0},
             {0.08f, 0.10f, 0.03f}, GrakColors::Skin, 0.3f, {0, 0, 1});
    DrawLimb(shaders, m_ear_r, head_mat, {0.18f, 0.06f, 0},
             {0.08f, 0.10f, 0.03f}, GrakColors::Skin, -0.3f, {0, 0, 1});

    // EYES
    if (!m_is_blinking) {
        float eye_glow = 0.8f + 0.2f * std::sin(time * 2.0f);
        if (corruption > 0.5f) {
            eye_glow = 0.5f + 0.5f * std::sin(time * 15.0f * corruption);
        }
        glm::vec3 eye_color = GrakColors::Eyes * eye_glow;
        DrawLimb(shaders, m_eye_l, head_mat, {-0.08f, 0.04f, 0.15f},
                 {0.05f, 0.04f, 0.03f}, eye_color);
        DrawLimb(shaders, m_eye_r, head_mat, {0.08f, 0.04f, 0.15f},
                 {0.05f, 0.04f, 0.03f}, eye_color);
    }

    // PAULDRONS
    DrawLimb(shaders, m_pauldron_l, torso_mat, {-0.32f, 0.24f, 0},
             {0.18f, 0.14f, 0.18f}, GrakColors::Metal);
    DrawLimb(shaders, m_pauldron_r, torso_mat, {0.32f, 0.24f, 0},
             {0.18f, 0.14f, 0.18f}, GrakColors::Metal);

    // LEFT ARM
    float arm_l_swing = walk_swing2 * walk_amplitude * 0.7f;
    glm::mat4 upper_arm_l_mat = glm::translate(torso_mat, {-0.30f, 0.14f, 0});
    upper_arm_l_mat = glm::rotate(upper_arm_l_mat, arm_l_swing, {1, 0, 0});
    DrawLimb(shaders, m_upper_arm_l, upper_arm_l_mat, {0, -0.14f, 0},
             {0.13f, 0.22f, 0.13f}, GrakColors::Skin);

    float elbow_bend_l = -0.2f - std::abs(arm_l_swing) * 0.3f;
    glm::mat4 lower_arm_l_mat = glm::translate(upper_arm_l_mat, {0, -0.28f, 0});
    lower_arm_l_mat = glm::rotate(lower_arm_l_mat, elbow_bend_l, {1, 0, 0});
    DrawLimb(shaders, m_lower_arm_l, lower_arm_l_mat, {0, -0.12f, 0},
             {0.11f, 0.20f, 0.11f}, GrakColors::SkinDark);

    // LEFT HAND with covenant brand
    glm::mat4 hand_l_mat = glm::translate(lower_arm_l_mat, {0, -0.26f, 0});
    DrawLimb(shaders, m_hand_l, hand_l_mat, {0, 0, 0}, {0.10f, 0.08f, 0.12f}, GrakColors::Skin);

    float brand_glow = 0.5f + 0.5f * std::sin(m_covenant_pulse);
    glm::vec3 brand_color = GrakColors::CovenantGlow * (0.4f + brand_glow * 0.6f);
    if (corruption > 0.6f) {
        float flicker = std::sin(time * 20.0f) * std::sin(time * 13.0f);
        brand_color *= 0.3f + 0.7f * std::max(0.0f, flicker);
    }
    DrawLimb(shaders, m_covenant_brand, hand_l_mat, {0, 0, 0.05f},
             {0.06f, 0.04f, 0.01f}, brand_color);

    // RIGHT ARM (holding walking staff)
    float arm_r_swing = walk_swing * walk_amplitude * 0.4f;
    glm::mat4 upper_arm_r_mat = glm::translate(torso_mat, {0.30f, 0.12f, 0});
    upper_arm_r_mat = glm::rotate(upper_arm_r_mat, -0.15f + arm_r_swing, {1, 0, 0});
    DrawLimb(shaders, m_upper_arm_r, upper_arm_r_mat, {0, -0.14f, 0},
             {0.13f, 0.22f, 0.13f}, GrakColors::Skin);

    float elbow_bend_r = -0.35f - std::abs(arm_r_swing) * 0.2f;
    glm::mat4 lower_arm_r_mat = glm::translate(upper_arm_r_mat, {0, -0.28f, 0});
    lower_arm_r_mat = glm::rotate(lower_arm_r_mat, elbow_bend_r, {1, 0, 0});
    DrawLimb(shaders, m_lower_arm_r, lower_arm_r_mat, {0, -0.12f, 0},
             {0.11f, 0.20f, 0.11f}, GrakColors::SkinDark);

    glm::mat4 hand_r_mat = glm::translate(lower_arm_r_mat, {0, -0.24f, 0});
    DrawLimb(shaders, m_hand_r, hand_r_mat, {0, 0, 0}, {0.10f, 0.08f, 0.12f}, GrakColors::Skin);

    // WALKING STAFF
    float staff_tilt = walk_swing * walk_amplitude * 0.05f;
    glm::mat4 staff_mat = glm::translate(hand_r_mat, {0.02f, 0, 0.02f});
    staff_mat = glm::rotate(staff_mat, staff_tilt, {0, 0, 1});
    DrawLimb(shaders, m_staff, staff_mat, {0, 0.40f, 0},
             {0.035f, 0.90f, 0.035f}, GrakColors::Wood);
    DrawLimb(shaders, m_staff_top, staff_mat, {0, 0.88f, 0},
             {0.06f, 0.06f, 0.06f}, GrakColors::WoodDark);

    // LEFT LEG
    float leg_l_swing = walk_swing * walk_amplitude;
    glm::mat4 upper_leg_l_mat = glm::translate(root, {-0.14f, 0.36f, 0});
    upper_leg_l_mat = glm::rotate(upper_leg_l_mat, leg_l_swing * 0.5f, {1, 0, 0});
    DrawLimb(shaders, m_upper_leg_l, upper_leg_l_mat, {0, -0.14f, 0},
             {0.15f, 0.24f, 0.15f}, GrakColors::SkinDark);

    float knee_bend_l = std::max(0.0f, -leg_l_swing) * 0.6f;
    glm::mat4 lower_leg_l_mat = glm::translate(upper_leg_l_mat, {0, -0.30f, 0});
    lower_leg_l_mat = glm::rotate(lower_leg_l_mat, knee_bend_l, {1, 0, 0});
    DrawLimb(shaders, m_lower_leg_l, lower_leg_l_mat, {0, -0.14f, 0},
             {0.12f, 0.22f, 0.12f}, GrakColors::Skin);

    DrawLimb(shaders, m_foot_l, lower_leg_l_mat, {0, -0.28f, 0.04f},
             {0.14f, 0.06f, 0.20f}, GrakColors::Leather);

    // RIGHT LEG
    float leg_r_swing = walk_swing2 * walk_amplitude;
    glm::mat4 upper_leg_r_mat = glm::translate(root, {0.14f, 0.36f, 0});
    upper_leg_r_mat = glm::rotate(upper_leg_r_mat, leg_r_swing * 0.5f, {1, 0, 0});
    DrawLimb(shaders, m_upper_leg_r, upper_leg_r_mat, {0, -0.14f, 0},
             {0.15f, 0.24f, 0.15f}, GrakColors::SkinDark);

    float knee_bend_r = std::max(0.0f, -leg_r_swing) * 0.6f;
    glm::mat4 lower_leg_r_mat = glm::translate(upper_leg_r_mat, {0, -0.30f, 0});
    lower_leg_r_mat = glm::rotate(lower_leg_r_mat, knee_bend_r, {1, 0, 0});
    DrawLimb(shaders, m_lower_leg_r, lower_leg_r_mat, {0, -0.14f, 0},
             {0.12f, 0.22f, 0.12f}, GrakColors::Skin);

    DrawLimb(shaders, m_foot_r, lower_leg_r_mat, {0, -0.28f, 0.04f},
             {0.14f, 0.06f, 0.20f}, GrakColors::Leather);

    // BACKPACK (concept art: massive, burdened)
    DrawLimb(shaders, m_backpack_main, torso_mat, {0, 0.02f, -0.22f},
             {0.38f, 0.48f, 0.25f}, GrakColors::Leather);
    DrawLimb(shaders, m_backpack_top, torso_mat, {0, 0.30f, -0.20f},
             {0.32f, 0.15f, 0.22f}, GrakColors::Leather);

    // BEDROLL on top of backpack
    DrawLimb(shaders, m_bedroll, torso_mat, {0, 0.42f, -0.20f},
             {0.28f, 0.08f, 0.08f}, GrakColors::Cloth, glm::half_pi<float>(), {0, 0, 1});

    // CHEST STRAP
    DrawLimb(shaders, m_chest_strap, torso_mat, {-0.15f, 0.10f, 0.05f},
             {0.04f, 0.50f, 0.04f}, GrakColors::LeatherDark, 0.3f, {0, 0, 1});

    // CHEST PLATE (scavenged)
    DrawLimb(shaders, m_chest_plate, torso_mat, {0.08f, 0.10f, 0.16f},
             {0.18f, 0.20f, 0.02f}, GrakColors::Metal);

    // POUCHES
    DrawLimb(shaders, m_pouch_l, torso_mat, {-0.28f, -0.20f, 0.05f},
             {0.08f, 0.08f, 0.06f}, GrakColors::Leather);
    DrawLimb(shaders, m_pouch_r, torso_mat, {0.28f, -0.20f, 0.05f},
             {0.08f, 0.08f, 0.06f}, GrakColors::Leather);

    // POT (hanging from backpack)
    float pot_swing = walk_swing * walk_amplitude * 0.1f;
    glm::mat4 pot_mat = glm::translate(torso_mat, {0.20f, -0.10f, -0.30f});
    pot_mat = glm::rotate(pot_mat, pot_swing, {0, 0, 1});
    DrawLimb(shaders, m_pot, pot_mat, {0, 0, 0}, {0.08f, 0.06f, 0.08f}, GrakColors::Metal);
}

// ============================================================================
// Renderer — Scene Setup
// ============================================================================

void Renderer::CreateTestScene() {
    m_cube = MakeCube();
    m_sphere = MakeSphere(14, 10);

    float S = 12.0f;
    std::vector<Vertex> gv = {
        {{-S, 0, -S}, {0, 1, 0}, {0, 0}}, {{S, 0, -S}, {0, 1, 0}, {1, 0}},
        {{S, 0, S}, {0, 1, 0}, {1, 1}},    {{-S, 0, S}, {0, 1, 0}, {0, 1}}
    };
    m_ground.Create(gv, {0, 1, 2, 0, 2, 3});
    glGenVertexArrays(1, &m_fullscreen_vao);

    m_grak.Initialize();
    m_particles.Initialize(1024);

    Logger::Info("Test scene created (with procedural Grak + particles)");
}

void Renderer::DrawFullscreenTriangle() {
    glBindVertexArray(m_fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ============================================================================
// Renderer — Initialize / Shutdown
// ============================================================================

bool Renderer::Initialize(int w, int h) {
    m_width = w;
    m_height = h;

    // Load shaders
    m_shaders.LoadShader("gbuffer", "assets/shaders/gbuffer.vert", "assets/shaders/gbuffer.frag");
    m_shaders.LoadShader("cel", "assets/shaders/fullscreen.vert", "assets/shaders/cel_shading.frag");
    m_shaders.LoadShader("outline", "assets/shaders/fullscreen.vert", "assets/shaders/outline.frag");
    m_shaders.LoadShader("corruption", "assets/shaders/fullscreen.vert", "assets/shaders/corruption.frag");
    m_shaders.LoadShader("shadow", "assets/shaders/shadow.vert", "assets/shaders/shadow.frag");

    // Create framebuffers
    m_gbuffer.Create(w, h);
    m_scene_fbo.Create(w, h);
    m_cel_fbo.Create(w, h);
    m_shadow_map.Create(1024); // Optimized for RTX 3050 Laptop (4GB VRAM)

    m_camera.SetPerspective(45.0f, static_cast<float>(w) / h, 0.1f, 500.0f);
    m_camera.LookAt({0, 3, 8}, {0, 0.5f, 0});

    // Initialize model loader (Assimp: OBJ, glTF, FBX, +40 formats)
    m_model_loader = new ModelLoader();
    m_model_loader->Initialize();

    // Try to load character model — fall back to procedural if not found
    m_use_procedural_grak = true;
    if (std::filesystem::exists("assets/models/grak.glb") ||
        std::filesystem::exists("assets/models/grak.gltf") ||
        std::filesystem::exists("assets/models/grak.obj")) {

        std::string grak_path;
        if (std::filesystem::exists("assets/models/grak.glb"))
            grak_path = "assets/models/grak.glb";
        else if (std::filesystem::exists("assets/models/grak.gltf"))
            grak_path = "assets/models/grak.gltf";
        else
            grak_path = "assets/models/grak.obj";

        if (LoadCharacterModel(grak_path)) {
            Logger::Info("Character model loaded from '{}' — using loaded model", grak_path);
        }
    } else {
        Logger::Info("No character model found in assets/models/ — using procedural Grak");
    }

    // Try to load scene models from assets/models/scene/
    if (std::filesystem::exists("assets/models/scene") &&
        std::filesystem::is_directory("assets/models/scene")) {
        int count = LoadSceneModels("assets/models/scene");
        if (count > 0) {
            Logger::Info("Loaded {} scene models from assets/models/scene/", count);
        }
    }

    // Also try individual scene model files
    for (const auto& name : {"altar", "pillar", "ruins", "wall", "sword", "ground", "scene"}) {
        for (const auto& ext : {".glb", ".gltf", ".obj", ".fbx"}) {
            std::string path = std::string("assets/models/") + name + ext;
            if (std::filesystem::exists(path)) {
                auto model = m_model_loader->Load(path);
                if (model) {
                    ModelInstance inst;
                    inst.model = model;
                    inst.transform = glm::mat4(1.0f);
                    inst.tag = name;
                    m_scene_models.push_back(inst);
                    m_has_scene_models = true;
                    Logger::Info("Scene model loaded: '{}'", path);
                }
                break;
            }
        }
    }

    CreateTestScene();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    Logger::Info("Renderer initialized ({}x{}) — 5-pass pipeline + model loader", w, h);
    if (m_model_loader) {
        Logger::Info("ModelLoader: {} models cached, {} textures, {} verts, {} tris",
                     m_model_loader->GetCachedModelCount(),
                     m_model_loader->GetCachedTextureCount(),
                     m_model_loader->GetTotalVertexCount(),
                     m_model_loader->GetTotalTriangleCount());
    }
    return true;
}

void Renderer::Shutdown() {
    if (m_model_loader) {
        m_model_loader->Shutdown();
        delete m_model_loader;
        m_model_loader = nullptr;
    }
    m_grak_model.reset();
    m_scene_models.clear();

    m_grak.Shutdown();
    m_particles.Shutdown();
    m_cube.Destroy();
    m_sphere.Destroy();
    m_ground.Destroy();
    if (m_fullscreen_vao) glDeleteVertexArrays(1, &m_fullscreen_vao);
    m_cel_fbo.Destroy();
    m_scene_fbo.Destroy();
    m_shadow_map.Destroy();
    m_gbuffer.Destroy();
    m_shaders.Shutdown();
}

// ============================================================================
// Frame Update
// ============================================================================

void Renderer::BeginFrame() {
    float dt = 1.0f / 60.0f;
    m_time += dt;

    // Update procedural character animation (always, for fallback)
    m_grak.Update(dt, m_grak_vel, m_corruption);

    // Update 3D model walk animation phase (for loaded model path)
    if (!m_use_procedural_grak) {
        float speed = glm::length(m_grak_vel);
        if (speed > 0.1f) {
            // Slow, heavy trudge — full cycle ~3.5s at walk, ~2.2s at run
            float walk_speed_mult = glm::mix(1.8f, 2.8f, glm::clamp(speed / 4.0f, 0.0f, 1.0f));
            m_model_walk_phase += dt * walk_speed_mult;
            if (m_model_walk_phase > glm::two_pi<float>())
                m_model_walk_phase -= glm::two_pi<float>();
        } else {
            m_model_walk_phase *= (1.0f - 2.0f * dt);
        }
        m_model_idle_phase += dt * 0.8f;  // slower idle — exhausted
        if (m_model_idle_phase > glm::two_pi<float>())
            m_model_idle_phase -= glm::two_pi<float>();
    }

    // Update particles
    m_particles.Update(dt);
    m_particles.EmitAsh(dt, m_grak_pos, 8.0f);
    m_particles.EmitCovenantAura(dt, {0, 1.2f, -5.0f}, 1.0f - m_corruption * 0.5f);

    if (m_grak.GetAnimState() == GrakAnimState::Walking ||
        m_grak.GetAnimState() == GrakAnimState::Running) {
        static float footstep_acc = 0.0f;
        footstep_acc += dt;
        float step_rate = (m_grak.GetAnimState() == GrakAnimState::Running) ? 0.2f : 0.35f;
        if (footstep_acc >= step_rate) {
            footstep_acc -= step_rate;
            m_particles.EmitFootstep(m_grak_pos + glm::vec3(0, 0.02f, 0));
        }
    }
}

void Renderer::Render(double alpha) {
    ShadowPass();
    GeometryPass();
    CelShadingPass();
    CorruptionPass();
    OutlinePass();
}

void Renderer::EndFrame() {}

// ============================================================================
// Model Transform Helpers
// ============================================================================

glm::mat4 Renderer::BuildGrakModelTransform() const {
    glm::vec3 adjusted_pos = m_grak_pos + glm::vec3(0, m_grak_ground_offset, 0);
    glm::mat4 t = glm::translate(glm::mat4(1.0f), adjusted_pos);
    t = glm::rotate(t, m_grak_facing + glm::pi<float>(), {0, 1, 0});
    t = glm::scale(t, glm::vec3(m_grak_model_scale));
    return t;
}

ModelAnimParams Renderer::BuildGrakAnimParams() const {
    ModelAnimParams anim;
    anim.enabled = true;
    anim.walk_phase = m_model_walk_phase;
    anim.idle_phase = m_model_idle_phase;
    anim.time = m_time;

    float speed = glm::length(m_grak_vel);
    // Reach full animation amplitude at lower speed (heavy trudge needs big motion even when slow)
    float raw_amp = glm::clamp(speed / 1.8f, 0.0f, 1.0f);
    anim.walk_amplitude = raw_amp * raw_amp * (3.0f - 2.0f * raw_amp);

    if (m_grak_model) {
        anim.aabb_min = m_grak_model->aabb_min;
        anim.aabb_max = m_grak_model->aabb_max;
    }
    return anim;
}

// ============================================================================
// PASS 0: Shadow Map
// ============================================================================

void Renderer::ShadowPass() {
    glm::vec3 light_dir = glm::normalize(glm::vec3(0.3f, -0.65f, 0.4f));
    glm::vec3 light_pos = m_grak_pos - light_dir * 15.0f;
    glm::mat4 light_view = glm::lookAt(light_pos, m_grak_pos, {0, 1, 0});
    glm::mat4 light_proj = glm::ortho(-15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 40.0f);
    m_shadow_map.light_space_matrix = light_proj * light_view;

    m_shadow_map.Bind();
    glViewport(0, 0, m_shadow_map.resolution, m_shadow_map.resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_shaders.Use("shadow");
    m_shaders.SetMat4("shadow", "u_light_space", m_shadow_map.light_space_matrix);

    // Ground shadow
    {
        glm::mat4 ground_model = glm::translate(glm::mat4(1.0f), {0, -0.025f, 0});
        ground_model = glm::scale(ground_model, {24.0f, 0.05f, 24.0f});
        DrawObjShadow(m_shaders, m_cube, ground_model);
    }

    // Pillar shadows
    struct PillarShadow { float x, z, height; };
    PillarShadow pillars[] = {
        {-3.5f, -3.0f, 2.5f}, {3.5f, -3.0f, 1.8f},
        {-3.5f, -7.0f, 2.8f}, {3.5f, -7.0f, 2.2f}
    };
    for (auto& p : pillars) {
        glm::mat4 bm = glm::translate(glm::mat4(1.0f), {p.x, 0.12f, p.z});
        bm = glm::scale(bm, {0.70f, 0.24f, 0.70f});
        DrawObjShadow(m_shaders, m_cube, bm);
        glm::mat4 pm = glm::translate(glm::mat4(1.0f), {p.x, p.height * 0.5f + 0.24f, p.z});
        pm = glm::scale(pm, {0.45f, p.height, 0.45f});
        DrawObjShadow(m_shaders, m_cube, pm);
    }

    // Wall shadows
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {-6.0f, 1.0f, -5.0f});
        m = glm::rotate(m, glm::radians(15.0f), {0, 1, 0});
        m = glm::scale(m, {0.3f, 2.0f, 3.0f});
        DrawObjShadow(m_shaders, m_cube, m);
    }
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {6.0f, 0.8f, -4.0f});
        m = glm::rotate(m, glm::radians(-10.0f), {0, 1, 0});
        m = glm::scale(m, {0.3f, 1.6f, 2.5f});
        DrawObjShadow(m_shaders, m_cube, m);
    }

    // Scene model shadows
    if (m_has_scene_models) {
        DrawSceneModelsShadow();
    }

    // Character model shadow (animated)
    if (!m_use_procedural_grak && m_grak_model) {
        ModelLoader::DrawModelShadowAnimated(m_shaders, *m_grak_model,
                                              BuildGrakModelTransform(), BuildGrakAnimParams());
    }

    m_shadow_map.Unbind();
}

// ============================================================================
// PASS 1: Geometry (G-Buffer)
// ============================================================================

void Renderer::GeometryPass() {
    m_gbuffer.Bind();
    glViewport(0, 0, m_width, m_height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_shaders.Use("gbuffer");
    m_shaders.SetMat4("gbuffer", "u_view", m_camera.GetView());
    m_shaders.SetMat4("gbuffer", "u_projection", m_camera.GetProjection());

    // === GRAK (loaded model or procedural fallback) ===
    if (!m_use_procedural_grak && m_grak_model) {
        ModelLoader::DrawModelAnimated(m_shaders, *m_grak_model,
                                        BuildGrakModelTransform(), BuildGrakAnimParams());
    } else {
        m_grak.Draw(m_shaders, m_grak_pos, m_grak_facing, m_time, m_corruption);
    }

    // === COVENANT ALTAR ===
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {0, 0.5f, -5.0f});
        m = glm::scale(m, {1.5f, 1.0f, 1.5f});
        DrawObj(m_shaders, m_cube, m, {0.45f, 0.39f, 0.33f});
    }
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {0, 1.1f, -5.0f});
        m = glm::scale(m, {2.0f, 0.15f, 2.0f});
        DrawObj(m_shaders, m_cube, m, {0.50f, 0.44f, 0.38f});
    }
    // Crystal
    {
        float bob = std::sin(m_time * 2.0f) * 0.15f;
        float glow = 0.7f + std::sin(m_time * 3.0f) * 0.3f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {0, 1.6f + bob, -5.0f});
        m = glm::rotate(m, m_time * 1.5f, {0,1,0});
        m = glm::scale(m, glm::vec3(0.25f));
        DrawObj(m_shaders, m_cube, m, glm::vec3(0.15f, glow * 0.8f, glow * 0.7f));
    }

    // === FOUR PILLARS ===
    struct PillarDef { float x, z, height; glm::vec3 color; };
    PillarDef pillar_defs[] = {
        {-3.5f, -3.0f, 2.5f, {0.45f, 0.40f, 0.35f}},
        { 3.5f, -3.0f, 1.8f, {0.42f, 0.38f, 0.33f}},
        {-3.5f, -7.0f, 2.8f, {0.48f, 0.43f, 0.38f}},
        { 3.5f, -7.0f, 2.2f, {0.44f, 0.39f, 0.34f}},
    };
    for (auto& p : pillar_defs) {
        // Base
        glm::mat4 bm = glm::translate(glm::mat4(1.0f), {p.x, 0.12f, p.z});
        bm = glm::scale(bm, {0.70f, 0.24f, 0.70f});
        DrawObj(m_shaders, m_cube, bm, p.color * 0.85f);
        // Shaft
        glm::mat4 pm = glm::translate(glm::mat4(1.0f), {p.x, p.height * 0.5f + 0.24f, p.z});
        pm = glm::scale(pm, {0.45f, p.height, 0.45f});
        DrawObj(m_shaders, m_cube, pm, p.color);
    }

    // === WALL FRAGMENTS ===
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {-6.0f, 1.0f, -5.0f});
        m = glm::rotate(m, glm::radians(15.0f), {0, 1, 0});
        m = glm::scale(m, {0.3f, 2.0f, 3.0f});
        DrawObj(m_shaders, m_cube, m, {0.50f, 0.46f, 0.40f});
    }
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {6.0f, 0.8f, -4.0f});
        m = glm::rotate(m, glm::radians(-10.0f), {0, 1, 0});
        m = glm::scale(m, {0.3f, 1.6f, 2.5f});
        DrawObj(m_shaders, m_cube, m, {0.48f, 0.44f, 0.38f});
    }
    // Fallen lintel
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {-5.5f, 0.2f, -2.5f});
        m = glm::rotate(m, glm::radians(35.0f), {0, 1, 0});
        m = glm::rotate(m, glm::radians(8.0f), {0, 0, 1});
        m = glm::scale(m, {2.5f, 0.25f, 0.35f});
        DrawObj(m_shaders, m_cube, m, {0.52f, 0.47f, 0.41f});
    }

    // === SCATTERED RUINS ===
    struct RuinDef { glm::vec3 pos; glm::vec3 scale; float rot; glm::vec3 color; };
    RuinDef ruins[] = {
        {{-5, 0.2f, 2}, {0.8f,0.4f,0.6f}, 30, {0.46f,0.42f,0.36f}},
        {{ 4, 0.15f, 3}, {0.5f,0.3f,0.7f}, 55, {0.48f,0.43f,0.37f}},
        {{-2, 0.25f, 5}, {1.0f,0.5f,0.4f}, 12, {0.44f,0.40f,0.34f}},
        {{ 6, 0.3f, -2}, {0.6f,0.6f,0.6f}, 78, {0.50f,0.45f,0.39f}},
        {{-6, 0.1f, -8}, {0.4f,0.2f,0.9f}, 45, {0.43f,0.39f,0.33f}},
        {{ 2, 0.18f, 7}, {0.7f,0.35f,0.5f}, 22, {0.45f,0.41f,0.35f}},
        {{-4, 0.2f, -4}, {0.55f,0.4f,0.45f}, 67, {0.48f,0.44f,0.38f}},
        {{ 7, 0.12f, 6}, {0.3f,0.25f,0.8f}, 90, {0.47f,0.42f,0.36f}},
    };
    for (auto& r : ruins) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), r.pos);
        m = glm::rotate(m, glm::radians(r.rot), {0, 1, 0});
        m = glm::scale(m, r.scale);
        DrawObj(m_shaders, m_cube, m, r.color);
    }

    // === SWORDS IN GROUND ===
    struct SwordDef { float x, z, rot, tilt; };
    SwordDef swords[] = {
        {-1.5f, 2.0f, 30.0f, 8.0f},
        { 2.5f, 1.5f, -45.0f, 12.0f},
        {-3.0f, -1.0f, 60.0f, 5.0f},
        { 1.0f, 4.0f, -20.0f, 15.0f},
        { 4.5f, -6.0f, 75.0f, 10.0f},
        {-4.0f, -8.0f, 40.0f, 7.0f},
    };
    for (auto& sw : swords) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {sw.x, 0.35f, sw.z});
        m = glm::rotate(m, glm::radians(sw.rot), {0, 1, 0});
        m = glm::rotate(m, glm::radians(sw.tilt), {0, 0, 1});
        glm::mat4 blade = glm::scale(m, {0.02f, 0.55f, 0.06f});
        DrawObj(m_shaders, m_cube, blade, {0.55f, 0.52f, 0.50f});
        glm::mat4 guard = glm::translate(m, {0, 0.28f, 0});
        guard = glm::scale(guard, {0.12f, 0.02f, 0.02f});
        DrawObj(m_shaders, m_cube, guard, {0.40f, 0.35f, 0.30f});
        glm::mat4 handle = glm::translate(m, {0, 0.35f, 0});
        handle = glm::scale(handle, {0.02f, 0.10f, 0.02f});
        DrawObj(m_shaders, m_cube, handle, {0.35f, 0.25f, 0.15f});
    }

    // === ORBITING COVENANT WISPS ===
    for (int i = 0; i < 5; i++) {
        float a = m_time * (0.8f + i * 0.15f) + i * 1.257f;
        float radius = 1.5f + i * 0.3f;
        float y = 1.5f + std::sin(a * 2.0f) * 0.4f;
        glm::vec3 pos = {radius * std::cos(a), y, -5.0f + radius * std::sin(a)};
        float pulse = 0.08f + std::sin(m_time * 3.0f + i * 1.2f) * 0.03f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        m = glm::scale(m, glm::vec3(pulse));
        glm::vec3 colors[] = {
            {0.08f, 0.55f, 0.50f}, {0.40f, 0.15f, 0.55f}, {0.60f, 0.45f, 0.08f},
            {0.15f, 0.40f, 0.60f}, {0.55f, 0.20f, 0.20f}
        };
        float dim = 1.0f - m_corruption * 0.5f;
        DrawObj(m_shaders, m_sphere, m, colors[i] * dim);
    }

    // === PARTICLES ===
    m_particles.Draw(m_shaders, m_sphere);

    // === LOADED SCENE MODELS ===
    if (m_has_scene_models) {
        DrawSceneModels();
    }

    // === GROUND PLANE ===
    float tile = 3.0f;
    for (int gx = -4; gx < 4; gx++) {
        for (int gz = -4; gz < 4; gz++) {
            bool dark = (gx + gz) % 2 == 0;
            float cx = gx * tile + tile * 0.5f;
            float cz = gz * tile + tile * 0.5f;
            float dx = cx - m_grak_pos.x;
            float dz = cz - m_grak_pos.z;
            if (dx * dx + dz * dz > 225.0f) continue;

            glm::mat4 m = glm::translate(glm::mat4(1.0f), {cx, -0.025f, cz});
            m = glm::scale(m, {tile, 0.05f, tile});
            float noise = std::sin(cx * 0.7f + cz * 1.3f) * 0.02f;
            glm::vec3 col = dark
                ? glm::vec3(0.32f + noise, 0.27f + noise, 0.21f + noise)
                : glm::vec3(0.38f + noise, 0.33f + noise, 0.26f + noise);
            DrawObj(m_shaders, m_cube, m, col);
        }
    }

    m_gbuffer.Unbind();
}

// ============================================================================
// PASS 2: Cel Shading
// ============================================================================

void Renderer::CelShadingPass() {
    m_cel_fbo.Bind();
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_shaders.Use("cel");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_gbuffer.position_tex);
    m_shaders.SetInt("cel", "u_position", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_gbuffer.normal_tex);
    m_shaders.SetInt("cel", "u_normal", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_gbuffer.albedo_tex);
    m_shaders.SetInt("cel", "u_albedo", 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m_shadow_map.depth_tex);
    m_shaders.SetInt("cel", "u_shadow_map", 3);

    m_shaders.SetVec3("cel", "u_light_dir", {0.3f, -0.65f, 0.4f});
    m_shaders.SetVec3("cel", "u_light_color", {1.1f, 0.95f, 0.82f});
    m_shaders.SetVec3("cel", "u_ambient_color", {0.4f, 0.35f, 0.3f});
    m_shaders.SetMat4("cel", "u_light_space", m_shadow_map.light_space_matrix);
    m_shaders.SetVec3("cel", "u_camera_pos", m_camera.GetPosition());
    m_shaders.SetInt("cel", "u_cel_steps", m_current_profile.cel_steps);
    m_shaders.SetVec3("cel", "u_dominant_hue", m_current_profile.dominant_hue);
    m_shaders.SetFloat("cel", "u_corruption", m_corruption);
    m_shaders.SetFloat("cel", "u_time", m_time);
    m_shaders.SetVec2("cel", "u_resolution", {static_cast<float>(m_width), static_cast<float>(m_height)});

    DrawFullscreenTriangle();
    m_cel_fbo.Unbind();
}

// ============================================================================
// PASS 3: Corruption Post-Process
// ============================================================================

void Renderer::CorruptionPass() {
    m_scene_fbo.Bind();
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_shaders.Use("corruption");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_cel_fbo.color_tex);
    m_shaders.SetInt("corruption", "u_scene", 0);
    m_shaders.SetFloat("corruption", "u_corruption", m_corruption);
    m_shaders.SetFloat("corruption", "u_time", m_time);
    m_shaders.SetVec2("corruption", "u_resolution", {static_cast<float>(m_width), static_cast<float>(m_height)});

    DrawFullscreenTriangle();
    m_scene_fbo.Unbind();
}

// ============================================================================
// PASS 4: Outline
// ============================================================================

void Renderer::OutlinePass() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_shaders.Use("outline");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_gbuffer.normal_tex);
    m_shaders.SetInt("outline", "u_normal", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_gbuffer.position_tex);
    m_shaders.SetInt("outline", "u_depth", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_scene_fbo.color_tex);
    m_shaders.SetInt("outline", "u_scene", 2);
    m_shaders.SetVec3("outline", "u_outline_color", m_current_profile.outline_color);
    m_shaders.SetFloat("outline", "u_thickness", 1.5f);
    m_shaders.SetVec2("outline", "u_resolution", {static_cast<float>(m_width), static_cast<float>(m_height)});

    DrawFullscreenTriangle();
}

// ============================================================================
// Model Instance Management
// ============================================================================

void Renderer::AddModelInstance(const ModelInstance& instance) {
    m_scene_models.push_back(instance);
    m_has_scene_models = true;
}

void Renderer::ClearModelInstances() {
    m_scene_models.clear();
    m_has_scene_models = false;
}

// ============================================================================
// Load Character Model
// ============================================================================

bool Renderer::LoadCharacterModel(const std::string& path, float scale) {
    if (!m_model_loader) return false;

    m_grak_model = m_model_loader->Load(path, scale, false, true);
    if (m_grak_model && m_grak_model->IsValid()) {
        // Auto-scale: TRELLIS models are often in [-1,1] normalized space.
        float model_height = m_grak_model->aabb_max.y - m_grak_model->aabb_min.y;
        constexpr float TARGET_HEIGHT = 1.6f;

        if (model_height > 0.01f && (model_height < 0.5f || model_height > 5.0f)) {
            float auto_scale = TARGET_HEIGHT / model_height;
            Logger::Info("Character model auto-scaling: raw height={:.2f}, target={:.2f}, scale={:.2f}",
                         model_height, TARGET_HEIGHT, auto_scale);
            m_grak_model_scale = auto_scale * scale;
        } else {
            m_grak_model_scale = scale;
        }

        // Compute ground offset: shift model so feet are at Y=0 (after scaling)
        float feet_y = m_grak_model->aabb_min.y * m_grak_model_scale;
        m_grak_ground_offset = -feet_y;

        // === COLORIZE: Apply concept-art palette ===
        // Check if the model actually has textures loaded successfully
        bool has_texture = false;
        for (const auto& sm : m_grak_model->meshes) {
            if (sm.material.diffuse_texture != 0) {
                has_texture = true;
                break;
            }
        }

        if (has_texture) {
            // Texture loaded — use actual texture, set warm tint as fallback color
            const glm::vec3 warm_tint = {0.85f, 0.70f, 0.50f};
            for (auto& sm : m_grak_model->meshes) {
                sm.material.diffuse_color = warm_tint;
            }
            Logger::Info("Character model has texture — using embedded texture with warm tint fallback");
        } else {
            // No texture — use concept-art palette color
            const glm::vec3 grak_main_color = {0.92f, 0.74f, 0.52f};
            for (auto& sm : m_grak_model->meshes) {
                sm.material.diffuse_color = grak_main_color;
            }
            Logger::Info("Applied concept-art color to {} submeshes", m_grak_model->meshes.size());
        }

        Logger::Info("Character model '{}' loaded: {} sub-meshes, height={:.2f}, ground_offset={:.2f}, auto_scale={:.2f}",
                     m_grak_model->name, m_grak_model->meshes.size(),
                     model_height * m_grak_model_scale,
                     m_grak_ground_offset, m_grak_model_scale);
        m_use_procedural_grak = false;
        return true;
    }

    Logger::Warn("Failed to load character model '{}', keeping procedural Grak", path);
    m_use_procedural_grak = true;
    return false;
}

// ============================================================================
// Load Scene Models from Directory
// ============================================================================

int Renderer::LoadSceneModels(const std::string& directory) {
    if (!m_model_loader) return 0;
    if (!std::filesystem::exists(directory)) return 0;

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".obj" && ext != ".gltf" && ext != ".glb" &&
            ext != ".fbx" && ext != ".dae") continue;

        auto model = m_model_loader->Load(entry.path().string());
        if (model) {
            ModelInstance inst;
            inst.model = model;
            inst.transform = glm::mat4(1.0f);
            inst.tag = entry.path().stem().string();
            m_scene_models.push_back(inst);
            loaded++;
            Logger::Info("Scene model loaded: '{}'", entry.path().string());
        }
    }

    if (loaded > 0) m_has_scene_models = true;
    return loaded;
}

// ============================================================================
// Draw Scene Models
// ============================================================================

void Renderer::DrawSceneModels() {
    for (const auto& inst : m_scene_models) {
        if (!inst.visible || !inst.model) continue;
        ModelLoader::DrawModel(m_shaders, *inst.model, inst.transform, inst.color_override);
    }
}

void Renderer::DrawSceneModelsShadow() {
    for (const auto& inst : m_scene_models) {
        if (!inst.cast_shadow || !inst.model) continue;
        ModelLoader::DrawModelShadow(m_shaders, *inst.model, inst.transform);
    }
}

} // namespace Unsuffered
