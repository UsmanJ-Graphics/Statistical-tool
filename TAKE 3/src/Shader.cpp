#include "Shader.h"
#include <iostream>
#include <stdexcept>

namespace Statix {

Shader::Shader(const char* vertSrc, const char* fragSrc) {
    unsigned int vert = compile_stage(GL_VERTEX_SHADER,   vertSrc);
    unsigned int frag = compile_stage(GL_FRAGMENT_SHADER, fragSrc);

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!check_status(m_id, "PROGRAM")) {
        glDeleteProgram(m_id);
        m_id = 0;
        throw std::runtime_error("CRITICAL: Shader program linking failed");
    }
}

Shader::~Shader() {
    if (m_id != 0) {
        glDeleteProgram(m_id);
    }
}

Shader::Shader(Shader&& other) noexcept : m_id(other.m_id) {
    other.m_id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_id != 0) glDeleteProgram(m_id);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(m_id);
}

// --- Private Helpers ---

unsigned int Shader::compile_stage(GLenum type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    const std::string label = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
    if (!check_status(shader, label)) {
        glDeleteShader(shader);
        throw std::runtime_error("CRITICAL: " + label + " shader compilation failed");
    }
    return shader;
}

bool Shader::check_status(unsigned int object, const std::string& stage) {
    int success;
    char infoLog[1024];

    if (stage != "PROGRAM") {
        glGetShaderiv(object, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(object, 1024, nullptr, infoLog);
            std::cerr << "Shader Compile Error (" << stage << "):\n"
                      << infoLog << "\n";
            return false;
        }
    } else {
        glGetProgramiv(object, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(object, 1024, nullptr, infoLog);
            std::cerr << "Shader Link Error:\n" << infoLog << "\n";
            return false;
        }
    }
    return true;
}

} // namespace Statix
