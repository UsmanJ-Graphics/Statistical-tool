#include "AxisRenderer.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace Statix {

    ViewportInfo AxisRenderer::compute(int winWidth, int winHeight,
        int sidebarWidth,
        const ChartBounds& b)
    {
        int activeCanvasWidth = winWidth - sidebarWidth;

        ViewportInfo vp;
        vp.sidebarWidth = sidebarWidth;
        vp.activeCanvasWidth = activeCanvasWidth;
        vp.winHeight = winHeight;

        vp.leftXPixel = sidebarWidth + (b.xMin + 1.0f) * 0.5f * static_cast<float>(activeCanvasWidth);
        vp.rightXPixel = sidebarWidth + (b.xMax + 1.0f) * 0.5f * static_cast<float>(activeCanvasWidth);
        vp.bottomYPixel = (1.0f - b.yMin) * 0.5f * static_cast<float>(winHeight);
        vp.topYPixel = (1.0f - b.yMax) * 0.5f * static_cast<float>(winHeight);

        return vp;
    }

    void AxisRenderer::draw(ImDrawList* drawList,
        const ViewportInfo& vp,
        const std::vector<float>& values,
        const std::string& columnName,
        const ChartBounds& b) const
    {
        if (values.empty() || !drawList) return;

        const ImColor axisColor = ImColor(0.5f, 0.6f, 0.7f, 0.8f);
        const ImColor tickLabelColor = ImColor(0.8f, 0.8f, 0.8f, 0.9f);

        const float fCanvas = static_cast<float>(vp.activeCanvasWidth);
        const float fHeight = static_cast<float>(vp.winHeight);

        // A. Axis lines
        drawList->AddLine(ImVec2(vp.leftXPixel, vp.bottomYPixel),
            ImVec2(vp.rightXPixel, vp.bottomYPixel), axisColor, 2.0f);
        drawList->AddLine(ImVec2(vp.leftXPixel, vp.bottomYPixel),
            ImVec2(vp.leftXPixel, vp.topYPixel), axisColor, 2.0f);

        // B. Y-axis ticks
        float maxVal = *std::max_element(values.begin(), values.end());
        if (maxVal <= 0.0f) maxVal = 1.0f;

        const bool isGPA = (columnName == "GPA");

        for (int i = 0; i <= 4; ++i) {
            float t = i / 4.0f;
            float tickValue = t * maxVal;
            float yNDC = b.yMin + t * (b.yMax - b.yMin);
            float yPixel = (1.0f - yNDC) * 0.5f * fHeight;

            drawList->AddLine(ImVec2(vp.leftXPixel - 6.0f, yPixel),
                ImVec2(vp.leftXPixel, yPixel), axisColor, 1.5f);

            char buf[32];
            if (isGPA) snprintf(buf, sizeof(buf), "%.2f", tickValue);
            else       snprintf(buf, sizeof(buf), "%.1f", tickValue);

            ImVec2 textSz = ImGui::CalcTextSize(buf);
            drawList->AddText(
                ImVec2(vp.leftXPixel - 12.0f - textSz.x, yPixel - textSz.y * 0.5f),
                tickLabelColor, buf);
        }

        // C. X-axis labels
        int numBars = static_cast<int>(values.size());
        if (numBars == 0) return;

        const float totalWidth = b.xMax - b.xMin;
        const float barSlotWidth = totalWidth / static_cast<float>(numBars);
        const float barWidth = barSlotWidth * (1.0f - b.spacingRatio);
        const float barGap = barSlotWidth * b.spacingRatio;

        for (int i = 0; i < numBars; ++i) {
            float xStartNDC = b.xMin + i * barSlotWidth + barGap * 0.5f;
            float xEndNDC = xStartNDC + barWidth;
            float xCenterNDC = (xStartNDC + xEndNDC) * 0.5f;
            float xCenterPx = static_cast<float>(vp.sidebarWidth)
                + (xCenterNDC + 1.0f) * 0.5f * fCanvas;

            char buf[16];
            snprintf(buf, sizeof(buf), "S%d", i + 1);

            ImVec2 textSz = ImGui::CalcTextSize(buf);
            drawList->AddText(
                ImVec2(xCenterPx - textSz.x * 0.5f, vp.bottomYPixel + 8.0f),
                tickLabelColor, buf);
        }
    }

}