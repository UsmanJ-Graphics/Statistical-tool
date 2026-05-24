#include "Window.h"
#include <iostream>
#include <stdexcept>

namespace Statix {

Window::Window(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        throw std::runtime_error("CRITICAL: Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_handle) {
        glfwTerminate();
        throw std::runtime_error("CRITICAL: Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_handle);
    glfwSetFramebufferSizeCallback(m_handle, framebuffer_size_callback);
    glfwSetKeyCallback(m_handle, key_callback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        throw std::runtime_error("CRITICAL: Failed to initialize GLAD");
    }
}

Window::~Window() {
    if (m_handle) {
        glfwDestroyWindow(m_handle);
    }
    glfwTerminate();
}

bool Window::should_close() const {
    return glfwWindowShouldClose(m_handle);
}

void Window::poll_events() {
    glfwPollEvents();
}

void Window::swap_buffers() {
    glfwSwapBuffers(m_handle);
}

void Window::get_size(int& w, int& h) const {
    glfwGetWindowSize(m_handle, &w, &h);
}

void Window::get_framebuffer_size(int& w, int& h) const {
    glfwGetFramebufferSize(m_handle, &w, &h);
}

void Window::get_cursor_pos(double& x, double& y) const {
    glfwGetCursorPos(m_handle, &x, &y);
}

// --- Static Callbacks ---

void Window::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void Window::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

} // namespace Statix
