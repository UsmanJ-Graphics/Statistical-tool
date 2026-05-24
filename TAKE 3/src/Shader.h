#pragma once
#include <glad/glad.h>
#include <string>

namespace Statix {

    class Shader {
    public:
        // ── Shader sources defined directly here ──────────────────────
        static constexpr const char* kVertexShaderSrc = R"glsl(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec4 aColor;
        out vec4 vColor;
        void main() {
            gl_Position = vec4(aPos, 1.0);
            vColor = aColor;
        }
    )glsl";

        static constexpr const char* kFragmentShaderSrc = R"glsl(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )glsl";
        // ──────────────────────────────────────────────────────────────

        Shader(const char* vertSrc, const char* fragSrc);
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        void use() const;
        unsigned int get_id() const { return m_id; }

    private:
        unsigned int m_id = 0;

        static unsigned int compile_stage(GLenum type, const char* src);
        static bool check_status(unsigned int object, const std::string& stage);
    };

} // namespace Statix