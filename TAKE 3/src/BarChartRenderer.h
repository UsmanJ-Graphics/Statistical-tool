#pragma once
#include <glad/glad.h>
#include <vector>

namespace Statix {

// NDC layout constants shared with hover detection and axis rendering
struct ChartBounds {
    float xMin        = -0.85f;
    float xMax        =  0.85f;
    float yMin        = -0.70f;
    float yMax        =  0.70f;
    float spacingRatio =  0.20f;
};

class BarChartRenderer {
public:
    explicit BarChartRenderer(unsigned int shaderProgramId);
    ~BarChartRenderer();

    // Non-copyable
    BarChartRenderer(const BarChartRenderer&) = delete;
    BarChartRenderer& operator=(const BarChartRenderer&) = delete;

    // Movable
    BarChartRenderer(BarChartRenderer&& other) noexcept;
    BarChartRenderer& operator=(BarChartRenderer&& other) noexcept;

    // Rebuilds interleaved vertex/index buffers on GPU each frame
    // data          - raw bar heights
    // hoverProgress - per-bar lerp factor in [0,1] for hover animation
    // baseColor     - RGBA float[4] rest color
    // highlightColor- RGBA float[4] hover color
    void update_geometry(const std::vector<float>& data,
                         const std::vector<float>& hoverProgress,
                         const float baseColor[4],
                         const float highlightColor[4]);

    // Issues a single glDrawElements call
    void draw() const;

    static ChartBounds default_bounds() { return ChartBounds{}; }

private:
    unsigned int m_VAO          = 0;
    unsigned int m_VBO          = 0;
    unsigned int m_EBO          = 0;
    int          m_numBars      = 0;
    unsigned int m_shaderProgram;

    void cleanup();
};

} // namespace Statix
