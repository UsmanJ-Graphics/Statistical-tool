#pragma once

namespace Statix {

// Vertex shader: interleaved position (loc=0) + per-vertex color (loc=1)
inline constexpr const char* kVertexShaderSrc = R"glsl(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec4 aColor;
    out vec4 vColor;
    void main() {
        gl_Position = vec4(aPos, 1.0);
        vColor = aColor;
    }
)glsl";

// Fragment shader: pass-through vertex color
inline constexpr const char* kFragmentShaderSrc = R"glsl(
    #version 330 core
    in vec4 vColor;
    out vec4 FragColor;
    void main() {
        FragColor = vColor;
    }
)glsl";

} // namespace Statix
