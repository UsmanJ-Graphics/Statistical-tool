#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

namespace Statix {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    // Non-copyable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    void poll_events();
    void swap_buffers();

    void get_size(int& w, int& h) const;
    void get_framebuffer_size(int& w, int& h) const;
    void get_cursor_pos(double& x, double& y) const;

    GLFWwindow* get_handle() const { return m_handle; }

private:
    GLFWwindow* m_handle = nullptr;

    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
};

} // namespace Statix
