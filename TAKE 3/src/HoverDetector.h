#pragma once
#include <vector>
#include "BarChartRenderer.h"  // for ChartBounds
#include "AxisRenderer.h"      // for ViewportInfo

namespace Statix {

// Manages per-bar hover detection and smooth animation progress vectors.
class HoverDetector {
public:
    explicit HoverDetector(const ChartBounds& bounds = ChartBounds{});

    // Hit-tests mouse position against each bar in pixel space.
    // Returns the index of the hovered bar, or -1 if none.
    int hit_test(double mouseX, double mouseY,
                 const ViewportInfo&       vp,
                 const std::vector<float>& values) const;

    // Advances each bar's hoverProgress toward 1 (hovered) or 0 (idle).
    // Call once per frame with the result of hit_test() and elapsed deltaTime.
    void tick(float deltaTime, int hoveredIndex);

    // Resizes the progress vector to match a new bar count, resetting animation.
    void resize(size_t count);

    const std::vector<float>& progress() const { return m_progress; }
    size_t size() const { return m_progress.size(); }

private:
    ChartBounds          m_bounds;
    std::vector<float>   m_progress;

    static constexpr float kHoverSpeed = 7.0f;
};

} // namespace Statix
