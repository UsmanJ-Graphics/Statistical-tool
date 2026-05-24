#include "HoverDetector.h"
#include <algorithm>
#include <cmath>

namespace Statix {

HoverDetector::HoverDetector(const ChartBounds& bounds)
    : m_bounds(bounds)
{}

// ============================================================================
// Hit test: mouse (px) → bar index
// ============================================================================

int HoverDetector::hit_test(double mouseX, double mouseY,
                             const ViewportInfo&       vp,
                             const std::vector<float>& values) const
{
    int numBars = static_cast<int>(values.size());
    if (numBars == 0) return -1;

    const ChartBounds& b     = m_bounds;
    const float totalWidth   = b.xMax - b.xMin;
    const float totalHeight  = b.yMax - b.yMin;
    const float barSlotWidth = totalWidth  / static_cast<float>(numBars);
    const float barWidth     = barSlotWidth * (1.0f - b.spacingRatio);
    const float barGap       = barSlotWidth * b.spacingRatio;
    const float fCanvas      = static_cast<float>(vp.activeCanvasWidth);
    const float fHeight      = static_cast<float>(vp.winHeight);

    float maxVal = *std::max_element(values.begin(), values.end());
    if (maxVal <= 0.0f) maxVal = 1.0f;

    for (int i = 0; i < numBars; ++i) {
        float xStartNDC = b.xMin + i * barSlotWidth + barGap * 0.5f;
        float xEndNDC   = xStartNDC + barWidth;

        float heightScale = values[i] / maxVal;
        float yEndNDC     = b.yMin + heightScale * totalHeight;

        // NDC → pixel
        float leftPx   = static_cast<float>(vp.sidebarWidth)
                       + (xStartNDC + 1.0f) * 0.5f * fCanvas;
        float rightPx  = static_cast<float>(vp.sidebarWidth)
                       + (xEndNDC   + 1.0f) * 0.5f * fCanvas;
        float topPx    = (1.0f - yEndNDC) * 0.5f * fHeight;
        float bottomPx = (1.0f - b.yMin)  * 0.5f * fHeight;

        if (mouseX >= leftPx && mouseX <= rightPx &&
            mouseY >= topPx  && mouseY <= bottomPx)
        {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Animation tick
// ============================================================================

void HoverDetector::tick(float deltaTime, int hoveredIndex) {
    int n = static_cast<int>(m_progress.size());
    for (int i = 0; i < n; ++i) {
        if (i == hoveredIndex) {
            m_progress[i] = std::min(1.0f, m_progress[i] + deltaTime * kHoverSpeed);
        } else {
            m_progress[i] = std::max(0.0f, m_progress[i] - deltaTime * kHoverSpeed);
        }
    }
}

void HoverDetector::resize(size_t count) {
    if (m_progress.size() != count) {
        m_progress.assign(count, 0.0f);
    }
}

} // namespace Statix
