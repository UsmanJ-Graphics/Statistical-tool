#include "UI.h"
#include "imgui.h"
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "imgui_internal.h"

namespace Statix {
    namespace UI {

        int g_chartType = 0;

        const char* chartTypeNames[] = { "Bar Chart", "Pie Chart", "Scatter Plot", "Histogram" };
        const int   chartTypeCount = IM_ARRAYSIZE(chartTypeNames);

        // ====================================================================
        // Sidebar
        // ====================================================================
        int draw_sidebar(int& selectedMenu, int winHeight)
        {
            const int sidebarWidth = 240;
            ImGuiStyle& style = ImGui::GetStyle();
            float  prevRounding = style.WindowRounding;
            float  prevBorderSize = style.WindowBorderSize;
            ImVec2 prevPadding = style.WindowPadding;
            style.WindowRounding = 0.f;
            style.WindowBorderSize = 1.f;
            style.WindowPadding = ImVec2(20.f, 20.f);

            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2((float)sidebarWidth, (float)winHeight), ImGuiCond_Always);
            ImGui::Begin("BrandSidebar", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoBringToFrontOnFocus);

            ImGui::TextColored(ImVec4(0.25f, 0.88f, 0.82f, 1.f), "STATIX ENGINE");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "v1.0.4 Developer Build");
            ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();
            if (ImGui::Selectable("[#] Dashboard", selectedMenu == 0, 0, ImVec2(0, 30))) selectedMenu = 0;
            ImGui::Spacing();
            if (ImGui::Selectable("[^] Load Dataset", selectedMenu == 1, 0, ImVec2(0, 30))) selectedMenu = 1;
            ImGui::Spacing();
            if (ImGui::Selectable("[*] Analytics Settings", selectedMenu == 2, 0, ImVec2(0, 30))) selectedMenu = 2;
            ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.f), "SYSTEM PROFILE");
            ImGui::Text("Core: OpenGL 3.3");
            ImGui::Text("Driver: Core Profile");
            ImGui::End();

            style.WindowRounding = prevRounding;
            style.WindowBorderSize = prevBorderSize;
            style.WindowPadding = prevPadding;
            return sidebarWidth;
        }

        // ====================================================================
        // Overlay HUD
        // ====================================================================
        void draw_hud(const char* columnName, int dataCount,
            float mean, float median, float stdDev, int sidebarWidth)
        {
            ImGui::SetNextWindowPos(ImVec2((float)sidebarWidth + 20.f, 20.f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.65f);
            ImGui::Begin("Overlay HUD", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.f), "DATA METRICS MONITOR");
            ImGui::Separator();
            ImGui::Text("Column:         %s", columnName);
            ImGui::Text("Data Nodes:     %d students", dataCount);
            ImGui::Text("Mean (Average): %.3f", mean);
            ImGui::Text("Median:         %.3f", median);
            ImGui::Text("Std Dev:        %.3f", stdDev);
            ImGui::End();
        }

        // ====================================================================
        // Pie chart
        // Labels: chart title, per-slice percentage inside/outside each wedge,
        // legend with colour swatch + label + value.
        // ====================================================================
        void render_pie_chart(const std::vector<float>& values,
            const std::vector<std::string>& /*labels*/)
        {
            if (values.empty()) return;

            // ── Bin raw respondent scores into 4 meaningful groups ────────
            // Likert scores range 1-5. Bins: Low <2.5, Medium 2.5-3.5,
            // High 3.5-4.25, Very High >=4.25
            struct Bin { const char* label; float lo; float hi; ImU32 color; int count; float sum; };
            Bin bins[4] = {
                { "Low (1.0-2.5)",       1.f,  2.5f, IM_COL32(235, 87, 52,255), 0, 0.f },
                { "Medium (2.5-3.5)",    2.5f, 3.5f, IM_COL32(250,200, 50,255), 0, 0.f },
                { "High (3.5-4.25)",     3.5f, 4.25f,IM_COL32(52,201,180,255), 0, 0.f },
                { "Very High (4.25-5)", 4.25f, 5.01f,IM_COL32(133, 92,224,255), 0, 0.f },
            };
            for (float v : values) {
                for (auto& b : bins)
                    if (v >= b.lo && v < b.hi) { ++b.count; b.sum += v; break; }
            }
            float total = (float)values.size();
            if (total <= 0.f) return;

            // ── Persistent hover animation state ─────────────────────────
            // explodeProgress[i] in [0,1]: 1 = fully exploded outward.
            static float explodeProgress[4] = {};
            static int   lastHovered = -1;
            const  float kExplodeSpeed = 6.f;
            const  float kExplodeDist = 18.f;   // pixels outward when fully hovered
            float        dt = ImGui::GetIO().DeltaTime;

            // ── Title ────────────────────────────────────────────────────
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
            {
                const char* title = "Score Distribution (Respondent Groups)";
                float tw = ImGui::CalcTextSize(title).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (ImGui::GetContentRegionAvail().x - tw) * 0.5f);
                ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.95f, 1.f), "%s", title);
            }
            ImGui::Spacing();

            ImDrawList* draw = ImGui::GetWindowDrawList();
            float       avail = ImGui::GetContentRegionAvail().x;
            float       radius = std::min(avail * 0.32f, 160.f);

            // Centre — leave room for explode offset on all sides
            ImVec2 baseCentre = ImGui::GetCursorScreenPos();
            baseCentre.x += avail * 0.5f;
            baseCentre.y += radius + kExplodeDist + 14.f;

            ImVec2 mousePos = ImGui::GetMousePos();

            // ── Pass 1: hit-test to find hovered slice ────────────────────
            // Uses proper angle-based test: check distance <= radius AND
            // angle falls within the slice's arc.
            int  hoveredSlice = -1;
            {
                float dx = mousePos.x - baseCentre.x;
                float dy = mousePos.y - baseCentre.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist <= radius + kExplodeDist)
                {
                    float mouseAngle = atan2f(dy, dx);   // [-pi, pi]
                    float sa = -IM_PI * 0.5f;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (bins[i].count == 0) { continue; }
                        float sweep = (bins[i].count / total) * IM_PI * 2.f;
                        float ea = sa + sweep;

                        // Normalise mouseAngle into [sa, ea] range
                        float a = mouseAngle;
                        while (a < sa)      a += IM_PI * 2.f;
                        while (a > sa + IM_PI * 2.f) a -= IM_PI * 2.f;

                        if (a >= sa && a <= ea)
                        {
                            // Also check against the exploded centre for accuracy
                            float mid = sa + sweep * 0.5f;
                            ImVec2 sliceCentre(
                                baseCentre.x + cosf(mid) * explodeProgress[i] * kExplodeDist,
                                baseCentre.y + sinf(mid) * explodeProgress[i] * kExplodeDist);
                            float sdx = mousePos.x - sliceCentre.x;
                            float sdy = mousePos.y - sliceCentre.y;
                            if (sdx * sdx + sdy * sdy <= (radius + kExplodeDist) * (radius + kExplodeDist))
                                hoveredSlice = i;
                        }
                        sa = ea;
                    }
                }
            }

            // ── Advance explode animation ─────────────────────────────────
            for (int i = 0; i < 4; ++i)
            {
                float target = (i == hoveredSlice) ? 1.f : 0.f;
                explodeProgress[i] += (target - explodeProgress[i]) * kExplodeSpeed * dt;
                explodeProgress[i] = std::max(0.f, std::min(1.f, explodeProgress[i]));
            }

            // ── Pass 2: draw slices ───────────────────────────────────────
            {
                float sa = -IM_PI * 0.5f;
                for (int i = 0; i < 4; ++i)
                {
                    if (bins[i].count == 0) continue;
                    float sweep = (bins[i].count / total) * IM_PI * 2.f;
                    float ea = sa + sweep;
                    float mid = sa + sweep * 0.5f;

                    // Offset centre for explode effect
                    ImVec2 c(
                        baseCentre.x + cosf(mid) * explodeProgress[i] * kExplodeDist,
                        baseCentre.y + sinf(mid) * explodeProgress[i] * kExplodeDist);

                    bool isHovered = (i == hoveredSlice);
                    ImU32 col = bins[i].color;

                    // Brighten hovered slice slightly
                    if (isHovered) {
                        int r = std::min(255, (int)(((col >> 0) & 0xFF) * 1.15f));
                        int g = std::min(255, (int)(((col >> 8) & 0xFF) * 1.15f));
                        int b = std::min(255, (int)(((col >> 16) & 0xFF) * 1.15f));
                        col = IM_COL32(r, g, b, 255);
                    }

                    // Filled wedge
                    draw->PathLineTo(c);
                    draw->PathArcTo(c, radius, sa, ea, 60);
                    draw->PathFillConvex(col);

                    // Crisp separator stroke (dark, thin)
                    draw->PathLineTo(c);
                    draw->PathArcTo(c, radius, sa, ea, 60);
                    draw->PathStroke(IM_COL32(15, 18, 24, 220), false, isHovered ? 3.f : 1.5f);

                    // Percentage label inside slice
                    {
                        float pct = 100.f * bins[i].count / total;
                        char  buf[16];
                        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
                        ImVec2 lsz = ImGui::CalcTextSize(buf);
                        float  lr = radius * 0.60f;
                        ImVec2 lp(c.x + cosf(mid) * lr - lsz.x * 0.5f,
                            c.y + sinf(mid) * lr - lsz.y * 0.5f);
                        draw->AddText(lp, IM_COL32(255, 255, 255, 240), buf);
                    }

                    // Respondent count label (closer to rim)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "n=%d", bins[i].count);
                        ImVec2 lsz = ImGui::CalcTextSize(buf);
                        float  lr = radius * 0.82f;
                        ImVec2 lp(c.x + cosf(mid) * lr - lsz.x * 0.5f,
                            c.y + sinf(mid) * lr - lsz.y * 0.5f);
                        draw->AddText(lp, IM_COL32(255, 255, 255, 170), buf);
                    }

                    sa = ea;
                }
            }

            // Advance cursor
            ImGui::Dummy(ImVec2(avail, (radius + kExplodeDist) * 2.f + 20.f));

            // ── Tooltip ───────────────────────────────────────────────────
            if (hoveredSlice >= 0 && bins[hoveredSlice].count > 0)
            {
                float pct = 100.f * bins[hoveredSlice].count / total;
                float avg = bins[hoveredSlice].sum / bins[hoveredSlice].count;
                ImGui::SetTooltip("%s\nn = %d  (%.1f%%)\nAvg score: %.2f",
                    bins[hoveredSlice].label,
                    bins[hoveredSlice].count, pct, avg);
            }

            // ── Legend ────────────────────────────────────────────────────
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.7f, 1.f), "LEGEND");
            ImGui::Spacing();

            for (int i = 0; i < 4; ++i)
            {
                ImU32  u32 = bins[i].color;
                ImVec4 cf(((u32 >> 0) & 0xFF) / 255.f,
                    ((u32 >> 8) & 0xFF) / 255.f,
                    ((u32 >> 16) & 0xFF) / 255.f, 1.f);

                ImVec2 swP = ImGui::GetCursorScreenPos();
                swP.y += 3.f;
                draw->AddRectFilled(swP, ImVec2(swP.x + 14.f, swP.y + 14.f), u32, 3.f);
                ImGui::Dummy(ImVec2(14.f, 14.f));
                ImGui::SameLine(0.f, 8.f);

                float pct = 100.f * bins[i].count / total;
                ImGui::TextColored(cf, "%s   n=%d  (%.1f%%)",
                    bins[i].label, bins[i].count, pct);
            }
        }

        // ====================================================================
        // Scatter plot
        // Labels: chart title, X-axis title, Y-axis title, min/max tick labels
        // on both axes, data-point count.
        // ====================================================================
        void render_scatter_plot(const std::vector<float>& xVals,
            const std::vector<float>& yVals)
        {
            if (xVals.size() != yVals.size() || xVals.empty()) return;

            // ── Title ────────────────────────────────────────────────────
            ImGui::Spacing();
            {
                const char* title = "Scatter Plot";
                float titleW = ImGui::CalcTextSize(title).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
                ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.95f, 1.f), "%s", title);
            }
            ImGui::Spacing();

            // ── Layout ───────────────────────────────────────────────────
            const float availW = ImGui::GetContentRegionAvail().x;
            const float yTitleW = 14.f;   // vertical Y-axis title
            const float yLabelW = 46.f;   // numeric Y labels
            const float xLabelH = 36.f;   // X labels + X-axis title
            const float plotH = 220.f;

            const float plotX = ImGui::GetCursorScreenPos().x + yTitleW + yLabelW;
            const float plotY = ImGui::GetCursorScreenPos().y;
            const float plotW = availW - yTitleW - yLabelW;

            float minX = *std::min_element(xVals.begin(), xVals.end());
            float maxX = *std::max_element(xVals.begin(), xVals.end());
            float minY = *std::min_element(yVals.begin(), yVals.end());
            float maxY = *std::max_element(yVals.begin(), yVals.end());
            if (maxX - minX < 1e-5f) { minX -= .5f; maxX += .5f; }
            if (maxY - minY < 1e-5f) { minY -= .5f; maxY += .5f; }

            ImVec2      origin(plotX, plotY);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2      mouse = ImGui::GetMousePos();

            // Background
            draw->AddRectFilled(origin, ImVec2(plotX + plotW, plotY + plotH),
                IM_COL32(30, 35, 45, 220));
            draw->AddRect(origin, ImVec2(plotX + plotW, plotY + plotH),
                IM_COL32(80, 90, 110, 180));

            const ImU32 colAxis = IM_COL32(130, 150, 170, 200);
            const ImU32 colTick = IM_COL32(180, 190, 200, 200);
            const ImU32 colGrid = IM_COL32(255, 255, 255, 15);
            const ImU32 colTitle = IM_COL32(220, 230, 240, 255);
            const float pad = 12.f;

            auto toPixel = [&](float x, float y) -> ImVec2 {
                float px = plotX + pad + (x - minX) / (maxX - minX) * (plotW - pad * 2.f);
                float py = plotY + (plotH - pad) - (y - minY) / (maxY - minY) * (plotH - pad * 2.f);
                return ImVec2(px, py);
                };

            // Y-axis ticks + grid
            const int kTY = 4;
            for (int i = 0; i <= kTY; ++i)
            {
                float t = (float)i / kTY;
                float val = minY + t * (maxY - minY);
                float py = plotY + (plotH - pad) - t * (plotH - pad * 2.f);
                draw->AddLine(ImVec2(plotX, py), ImVec2(plotX + plotW, py), colGrid, 1.f);
                draw->AddLine(ImVec2(plotX - 5.f, py), ImVec2(plotX, py), colAxis, 1.5f);
                char buf[24]; std::snprintf(buf, sizeof(buf), "%.1f", val);
                ImVec2 tsz = ImGui::CalcTextSize(buf);
                draw->AddText(ImVec2(plotX - 8.f - tsz.x, py - tsz.y * 0.5f), colTick, buf);
            }
            // X-axis ticks + grid
            const int kTX = 4;
            for (int i = 0; i <= kTX; ++i)
            {
                float t = (float)i / kTX;
                float val = minX + t * (maxX - minX);
                float px = plotX + pad + t * (plotW - pad * 2.f);
                draw->AddLine(ImVec2(px, plotY), ImVec2(px, plotY + plotH), colGrid, 1.f);
                draw->AddLine(ImVec2(px, plotY + plotH), ImVec2(px, plotY + plotH + 5.f), colAxis, 1.5f);
                char buf[24]; std::snprintf(buf, sizeof(buf), "%.1f", val);
                ImVec2 tsz = ImGui::CalcTextSize(buf);
                draw->AddText(ImVec2(px - tsz.x * 0.5f, plotY + plotH + 7.f), colTick, buf);
            }

            // Axis lines
            draw->AddLine(ImVec2(plotX, plotY), ImVec2(plotX, plotY + plotH), colAxis, 2.f);
            draw->AddLine(ImVec2(plotX, plotY + plotH), ImVec2(plotX + plotW, plotY + plotH), colAxis, 2.f);

            // Y-axis title (vertical chars)
            {
                const char* yt = "Y Value";
                const float chH = ImGui::GetTextLineHeight();
                int len = (int)std::strlen(yt);
                float sy = plotY + plotH * 0.5f - chH * len * 0.5f;
                for (int ci = 0; ci < len; ++ci) {
                    char ch[2] = { yt[ci],'\0' };
                    float chW = ImGui::CalcTextSize(ch).x;
                    draw->AddText(
                        ImVec2(origin.x - yTitleW - yLabelW + 2.f + (yTitleW - chW) * 0.5f,
                            sy + ci * chH),
                        colTick, ch);
                }
            }

            // X-axis title
            {
                const char* xt = "X Value";
                ImVec2      tsz = ImGui::CalcTextSize(xt);
                draw->AddText(
                    ImVec2(plotX + plotW * 0.5f - tsz.x * 0.5f, plotY + plotH + xLabelH - tsz.y),
                    colTitle, xt);
            }

            // Data points
            int hoveredPoint = -1;
            for (size_t i = 0; i < xVals.size(); ++i)
            {
                ImVec2 p = toPixel(xVals[i], yVals[i]);
                float  dx = mouse.x - p.x, dy = mouse.y - p.y;
                bool   isH = (dx * dx + dy * dy) < 36.f;
                draw->AddCircleFilled(p, isH ? 7.f : 5.f,
                    isH ? IM_COL32(255, 240, 100, 255) : IM_COL32(37, 220, 200, 240));
                draw->AddCircle(p, 6.f, IM_COL32(10, 80, 75, 200), 16, 1.5f);
                if (isH) hoveredPoint = (int)i;
            }

            // Advance cursor past the whole block
            ImGui::Dummy(ImVec2(availW, plotH + xLabelH + 8.f));

            // Data count label
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.f),
                "n = %d data points", (int)xVals.size());

            if (hoveredPoint >= 0)
                ImGui::SetTooltip("Respondent %d\nX: %.2f\nY: %.2f",
                    hoveredPoint + 1, xVals[hoveredPoint], yVals[hoveredPoint]);
        }

        // ====================================================================
        // Histogram
        // Labels: chart title, Y-axis ("Frequency") with ticks, X-axis bin
        // range labels, bin count labels above each bar.
        // ====================================================================
        void render_histogram(const std::vector<float>& values, int bins)
        {
            if (values.empty() || bins <= 0) return;

            float minV = *std::min_element(values.begin(), values.end());
            float maxV = *std::max_element(values.begin(), values.end());

            std::vector<float> hist(bins, 0.f);
            float range = maxV - minV;
            if (range <= 1e-6f) {
                hist[bins / 2] = (float)values.size();
                minV -= 0.5f; maxV += 0.5f; range = maxV - minV;
            }
            else {
                for (float v : values) {
                    int idx = (int)((v - minV) / range * (bins - 1));
                    idx = std::max(0, std::min(idx, bins - 1));
                    hist[idx] += 1.f;
                }
            }

            float maxHist = *std::max_element(hist.begin(), hist.end());
            if (maxHist <= 0.f) return;

            // ── Title ────────────────────────────────────────────────────
            ImGui::Spacing();
            {
                const char* title = "Frequency Distribution (Histogram)";
                float titleW = ImGui::CalcTextSize(title).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
                ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.95f, 1.f), "%s", title);
            }
            ImGui::Spacing();

            const float availW = ImGui::GetContentRegionAvail().x;
            const float yTitleW = 14.f;
            const float yLabelW = 46.f;
            const float xLabelH = 36.f;
            const float plotH = 180.f;
            const float barPad = 2.f;

            ImVec2      origin = ImGui::GetCursorScreenPos();
            const float plotX = origin.x + yTitleW + yLabelW;
            const float plotY = origin.y;
            const float plotW = availW - yTitleW - yLabelW;

            ImDrawList* draw = ImGui::GetWindowDrawList();

            const ImU32 colBar = IM_COL32(52, 180, 160, 220);
            const ImU32 colBarHL = IM_COL32(100, 230, 210, 255);
            const ImU32 colAxis = IM_COL32(130, 150, 170, 200);
            const ImU32 colTick = IM_COL32(180, 190, 200, 200);
            const ImU32 colGrid = IM_COL32(255, 255, 255, 15);
            const ImU32 colVal = IM_COL32(220, 230, 240, 220);
            const ImU32 colTitle = IM_COL32(220, 230, 240, 255);

            ImVec2 mouse = ImGui::GetMousePos();
            float  barW = plotW / (float)bins;

            // Background
            draw->AddRectFilled(ImVec2(plotX, plotY),
                ImVec2(plotX + plotW, plotY + plotH),
                IM_COL32(25, 30, 40, 200));

            // Y ticks + grid
            const int kTY = 4;
            for (int i = 0; i <= kTY; ++i) {
                float t = (float)i / kTY;
                float val = t * maxHist;
                float py = plotY + plotH - t * plotH;
                draw->AddLine(ImVec2(plotX, py), ImVec2(plotX + plotW, py), colGrid, 1.f);
                draw->AddLine(ImVec2(plotX - 5.f, py), ImVec2(plotX, py), colAxis, 1.5f);
                char buf[24]; std::snprintf(buf, sizeof(buf), "%.0f", val);
                ImVec2 tsz = ImGui::CalcTextSize(buf);
                draw->AddText(ImVec2(plotX - 8.f - tsz.x, py - tsz.y * 0.5f), colTick, buf);
            }

            // Axis lines
            draw->AddLine(ImVec2(plotX, plotY), ImVec2(plotX, plotY + plotH), colAxis, 2.f);
            draw->AddLine(ImVec2(plotX, plotY + plotH), ImVec2(plotX + plotW, plotY + plotH), colAxis, 2.f);

            // Y-axis title
            {
                const char* yt = "Frequency";
                const float chH = ImGui::GetTextLineHeight();
                int len = (int)std::strlen(yt);
                float sy = plotY + plotH * 0.5f - chH * len * 0.5f;
                for (int ci = 0; ci < len; ++ci) {
                    char ch[2] = { yt[ci],'\0' };
                    float chW = ImGui::CalcTextSize(ch).x;
                    draw->AddText(
                        ImVec2(origin.x + 2.f + (yTitleW - chW) * 0.5f, sy + ci * chH),
                        colTick, ch);
                }
            }

            // X-axis title
            {
                const char* xt = "Value Range";
                ImVec2 tsz = ImGui::CalcTextSize(xt);
                draw->AddText(
                    ImVec2(plotX + plotW * 0.5f - tsz.x * 0.5f, plotY + plotH + xLabelH - tsz.y),
                    colTitle, xt);
            }

            // Bars + count labels + X labels
            for (int i = 0; i < bins; ++i)
            {
                float norm = hist[i] / maxHist;
                float bh = norm * plotH;
                float x0 = plotX + i * barW + barPad;
                float x1 = plotX + (i + 1) * barW - barPad;
                float y0 = plotY + plotH - bh;
                float y1 = plotY + plotH;

                float dx = mouse.x - (x0 + (x1 - x0) * 0.5f), dy = mouse.y - (y0 + (y1 - y0) * 0.5f);
                bool  isH = (dx * dx + dy * dy < 900.f && mouse.x >= x0 && mouse.x <= x1
                    && mouse.y >= y0 && mouse.y <= y1);

                draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                    isH ? colBarHL : colBar, 2.f);

                // Count label above bar
                if (hist[i] > 0.f) {
                    char buf[12]; std::snprintf(buf, sizeof(buf), "%.0f", hist[i]);
                    ImVec2 tsz = ImGui::CalcTextSize(buf);
                    float tx = x0 + (x1 - x0) * 0.5f - tsz.x * 0.5f;
                    float ty = y0 - tsz.y - 2.f;
                    if (ty > plotY)
                        draw->AddText(ImVec2(tx, ty), colVal, buf);
                }

                // X bin-range label (every bin or every other if crowded)
                if (bins <= 12 || i % 2 == 0) {
                    float binStart = minV + i * (range / (float)bins);
                    char buf[16]; std::snprintf(buf, sizeof(buf), "%.1f", binStart);
                    ImVec2 tsz = ImGui::CalcTextSize(buf);
                    draw->AddText(
                        ImVec2(x0 + (x1 - x0) * 0.5f - tsz.x * 0.5f, plotY + plotH + 7.f),
                        colTick, buf);
                }

                if (isH)
                    ImGui::SetTooltip("Range: %.2f - %.2f\nCount: %.0f",
                        minV + i * (range / (float)bins),
                        minV + (i + 1) * (range / (float)bins),
                        hist[i]);
            }

            ImGui::Dummy(ImVec2(availW, plotH + xLabelH + 8.f));
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.f),
                "n = %d  |  bins = %d  |  range [%.2f, %.2f]",
                (int)values.size(), bins, minV, maxV);
        }

        // ====================================================================
        // Control panel (legacy)
        // ====================================================================
        bool draw_control_panel(int& currentColumnIndex,
            const char** columns, int columnCount,
            float baseColor[4], float highlightColor[4], float fps,
            DataProcessor& processor, BarChartRenderer& chart,
            const std::vector<float>& hoverProgress)
        {
            bool columnChanged = false;
            ImGui::SetNextWindowPos(ImVec2(740.f, 20.f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(264.f, 680.f), ImGuiCond_FirstUseEver);
            ImGui::Begin("Engine Control Panel");
            ImGui::Combo("Chart Type", &g_chartType, chartTypeNames, IM_ARRAYSIZE(chartTypeNames));
            ImGui::Separator(); ImGui::Text("Visual Settings"); ImGui::Separator();
            ImGui::ColorEdit4("Base Color", baseColor);
            ImGui::ColorEdit4("Hover Highlight", highlightColor);
            ImGui::Separator(); ImGui::Text("DATA CONTROLLER:");
            if (ImGui::Combo("Select Column", &currentColumnIndex, columns, columnCount)) {
                auto newData = processor.get_column_values(columns[currentColumnIndex]);
                chart.update_geometry(newData, hoverProgress, baseColor, highlightColor);
                columnChanged = true;
            }
            ImGui::Separator();
            if (ImGui::Button("Add Random Student Entry")) {
                float randAge = (float)(18 + (std::rand() % 15));
                float randHours = (float)(5 + (std::rand() % 25));
                float randGPA = 2.f + (float)(std::rand() % 200) / 100.f;
                processor.add_row({ randAge,randHours,randGPA });
                auto updatedData = processor.get_column_values(columns[currentColumnIndex]);
                chart.update_geometry(updatedData, hoverProgress, baseColor, highlightColor);
            }
            ImGui::Separator(); ImGui::Text("GPU Diagnostics:");
            ImGui::Text("Frame Rate: %.1f FPS", fps);
            ImGui::Text("Frame Time: %.3f ms", 1000.f / fps);
            ImGui::End();
            return columnChanged;
        }

        // ====================================================================
        // Bar tooltip
        // ====================================================================
        void draw_bar_tooltip(int hoveredIndex, const std::vector<float>& values)
        {
            if (hoveredIndex < 0 || hoveredIndex >= (int)values.size()) return;
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.25f, 0.88f, 0.82f, 1.f),
                "Student Data Node %d", hoveredIndex + 1);
            ImGui::Separator();
            ImGui::Text("Val:  %.2f", values[hoveredIndex]);
            ImGui::EndTooltip();
        }

    } // namespace UI
} // namespace Statix