#pragma once
#include <vector>
#include <string>
#include "BarChartRenderer.h"

struct ImDrawList;

namespace Statix {

    struct ViewportInfo {
        float leftXPixel, rightXPixel;
        float topYPixel, bottomYPixel;
        int   activeCanvasWidth;
        int   sidebarWidth;
        int   winHeight;
    };

    class AxisRenderer {
    public:
        AxisRenderer() = default;

        // B-07 FIX: removed 'static' — the .cpp defines this as a regular
        // member function; having 'static' here was an ODR violation that
        // prevented compilation.  application.cpp already calls it via an
        // instance (axisRenderer.compute(...)), which is correct.
        ViewportInfo compute(int winWidth, int winHeight,
            int sidebarWidth,
            const ChartBounds& b = ChartBounds{});

        void draw(ImDrawList* drawList,
            const ViewportInfo& vp,
            const std::vector<float>& values,
            const std::string& columnName,
            const ChartBounds& b = ChartBounds{}) const;
    };

} // namespace Statix