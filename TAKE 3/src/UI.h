// d:\STATS APP\TAKE 3\TAKE 3\src\UI.h
#pragma once
#include <vector>
#include "DataProcessor.h"
#include "BarChartRenderer.h"

namespace Statix {
    namespace UI {

        extern int g_chartType;                    // 0=Bar, 1=Pie, 2=Scatter, 3=Histogram

        // === CHART TYPE NAMES (exposed for application.cpp) ===
        extern const char* chartTypeNames[];
        extern const int   chartTypeCount;

        // ============================================================================
        // Core UI Functions
        // ============================================================================

        int  draw_sidebar(int& selectedMenu, int winHeight);

        void draw_hud(const char* columnName, int dataCount,
            float mean, float median, float stdDev, int sidebarWidth);

        void draw_bar_tooltip(int hoveredIndex, const std::vector<float>& values);

        // Modern Chart Renderers
        void render_pie_chart(const std::vector<float>& values,
            const std::vector<std::string>& labels);

        void render_scatter_plot(const std::vector<float>& xVals,
            const std::vector<float>& yVals);

        void render_histogram(const std::vector<float>& values, int bins = 10);

        // Legacy
        bool draw_control_panel(...);   // your existing declaration
        

    } // namespace UI
} // namespace Statix