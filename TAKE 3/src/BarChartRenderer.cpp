#include "BarChartRenderer.h"
#include <algorithm>
#include <cstddef>

namespace Statix {

// ============================================================================
// Lifecycle
// ============================================================================

BarChartRenderer::BarChartRenderer(unsigned int shaderProgramId)
    : m_shaderProgram(shaderProgramId)
{
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);
}

BarChartRenderer::~BarChartRenderer() {
    cleanup();
}

BarChartRenderer::BarChartRenderer(BarChartRenderer&& other) noexcept
    : m_VAO(other.m_VAO), m_VBO(other.m_VBO), m_EBO(other.m_EBO),
      m_numBars(other.m_numBars), m_shaderProgram(other.m_shaderProgram)
{
    other.m_VAO = other.m_VBO = other.m_EBO = 0;
    other.m_numBars = 0;
}

BarChartRenderer& BarChartRenderer::operator=(BarChartRenderer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_VAO           = other.m_VAO;
        m_VBO           = other.m_VBO;
        m_EBO           = other.m_EBO;
        m_numBars       = other.m_numBars;
        m_shaderProgram = other.m_shaderProgram;
        other.m_VAO = other.m_VBO = other.m_EBO = 0;
        other.m_numBars = 0;
    }
    return *this;
}

// ============================================================================
// Geometry Update  (called every frame)
// ============================================================================

void BarChartRenderer::update_geometry(const std::vector<float>& data,
                                       const std::vector<float>& hoverProgress,
                                       const float baseColor[4],
                                       const float highlightColor[4])
{
    m_numBars = static_cast<int>(data.size());
    if (m_numBars == 0) return;

    const ChartBounds b = default_bounds();
    const float totalWidth  = b.xMax - b.xMin;
    const float totalHeight = b.yMax - b.yMin;
    const float barSlotWidth = totalWidth / m_numBars;
    const float barWidth     = barSlotWidth * (1.0f - b.spacingRatio);
    const float barGap       = barSlotWidth * b.spacingRatio;

    float maxVal = *std::max_element(data.begin(), data.end());
    if (maxVal <= 0.0f) maxVal = 1.0f;

    // Interleaved layout: [ x, y, z,  r, g, b, a ]  = 7 floats per vertex
    std::vector<float>        vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(m_numBars * 4 * 7);
    indices .reserve(m_numBars * 6);

    for (int i = 0; i < m_numBars; ++i) {
        // --- Geometry ---
        float xStart      = b.xMin + i * barSlotWidth + (barGap / 2.0f);
        float xEnd        = xStart + barWidth;
        float heightScale = data[i] / maxVal;

        // --- Hover lerp factor ---
        float t = (i < static_cast<int>(hoverProgress.size())) ? hoverProgress[i] : 0.0f;

        // --- Color lerp ---
        float r = baseColor[0] + t * (highlightColor[0] - baseColor[0]);
        float g = baseColor[1] + t * (highlightColor[1] - baseColor[1]);
        float bC= baseColor[2] + t * (highlightColor[2] - baseColor[2]);
        float a = baseColor[3] + t * (highlightColor[3] - baseColor[3]);

        // --- Scale swell on hover (up to 6% larger) ---
        float xCenter       = (xStart + xEnd) / 2.0f;
        float scaleM        = 1.0f + t * 0.06f;
        float xStartAnim    = xCenter - (barWidth / 2.0f) * scaleM;
        float xEndAnim      = xCenter + (barWidth / 2.0f) * scaleM;
        float yEndAnim      = b.yMin + (heightScale * totalHeight) * scaleM;

        // 4 corners: Top-Right, Bottom-Right, Bottom-Left, Top-Left
        auto push_vertex = [&](float vx, float vy) {
            vertices.push_back(vx); vertices.push_back(vy); vertices.push_back(0.0f);
            vertices.push_back(r);  vertices.push_back(g);  vertices.push_back(bC); vertices.push_back(a);
        };

        push_vertex(xEndAnim,   yEndAnim);   // 0 TR
        push_vertex(xEndAnim,   b.yMin);     // 1 BR
        push_vertex(xStartAnim, b.yMin);     // 2 BL
        push_vertex(xStartAnim, yEndAnim);   // 3 TL

        // Two triangles per bar: (0,1,3) and (1,2,3)
        unsigned int base = i * 4;
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 3);
        indices.push_back(base + 1); indices.push_back(base + 2); indices.push_back(base + 3);
    }

    // --- Upload to VRAM ---
    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_DYNAMIC_DRAW);

    // layout = 0 : position (3 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          7 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // layout = 1 : color (4 floats)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ============================================================================
// Draw
// ============================================================================

void BarChartRenderer::draw() const {
    if (m_numBars == 0) return;
    glUseProgram(m_shaderProgram);
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_numBars * 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ============================================================================
// Private
// ============================================================================

void BarChartRenderer::cleanup() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO != 0) glDeleteBuffers(1, &m_VBO);
    if (m_EBO != 0) glDeleteBuffers(1, &m_EBO);
}

} // namespace Statix
