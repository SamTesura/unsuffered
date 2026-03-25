#pragma once
// ============================================================================
// Platform Layer - SDL2 Window, Input, Timer
// ============================================================================

#include <SDL2/SDL.h>
#include <string>
#include <array>
#include <glm/glm.hpp>

namespace Unsuffered {

// --- Timer ---
class Timer {
public:
    static double Now() {
        return static_cast<double>(SDL_GetPerformanceCounter()) /
               static_cast<double>(SDL_GetPerformanceFrequency());
    }
};

// --- Input ---
class Input {
public:
    void Poll() {
        m_mouse_delta = {0, 0};
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    m_quit_requested = true;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode < 512)
                        m_keys[event.key.keysym.scancode] = true;
                    break;
                case SDL_KEYUP:
                    if (event.key.keysym.scancode < 512)
                        m_keys[event.key.keysym.scancode] = false;
                    break;
                case SDL_MOUSEMOTION:
                    m_mouse_pos = {event.motion.x, event.motion.y};
                    m_mouse_delta = {event.motion.xrel, event.motion.yrel};
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button <= 5)
                        m_mouse_buttons[event.button.button] = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button <= 5)
                        m_mouse_buttons[event.button.button] = false;
                    break;
            }
        }
    }

    bool IsKeyDown(SDL_Scancode key) const { return m_keys[key]; }
    bool IsMouseDown(uint8_t button) const { return m_mouse_buttons[button]; }
    glm::ivec2 GetMousePos() const { return m_mouse_pos; }
    glm::ivec2 GetMouseDelta() const { return m_mouse_delta; }
    bool QuitRequested() const { return m_quit_requested; }

private:
    std::array<bool, 512> m_keys{};
    std::array<bool, 6> m_mouse_buttons{};
    glm::ivec2 m_mouse_pos{0, 0};
    glm::ivec2 m_mouse_delta{0, 0};
    bool m_quit_requested = false;
};

// --- Window ---
class Window {
public:
    bool Create(const std::string& title, int width, int height) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
            return false;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        m_window = SDL_CreateWindow(title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!m_window) return false;

        m_gl_context = SDL_GL_CreateContext(m_window);
        if (!m_gl_context) return false;

        SDL_GL_SetSwapInterval(1); // VSync
        m_width = width;
        m_height = height;
        return true;
    }

    void SwapBuffers() { SDL_GL_SwapWindow(m_window); }

    void Destroy() {
        if (m_gl_context) SDL_GL_DeleteContext(m_gl_context);
        if (m_window) SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    float GetAspect() const { return static_cast<float>(m_width) / m_height; }
    SDL_Window* GetHandle() { return m_window; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    int m_width = 0, m_height = 0;
};

} // namespace Unsuffered
