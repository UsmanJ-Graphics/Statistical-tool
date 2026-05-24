/*=====================================================================
   Full application entry point – OpenGL + Dear ImGui + Statistical UI.
   ------------------------------------------------------------------
   This file:
   * Opens a native Windows file-picker when the user clicks "Load Dataset".
   * Parses CSV/XLSX via FileImporter -> DataManager.
   * Computes all sub-scores automatically.
   * Provides three ImGui tabs:
        1) Raw Record Matrix (ImGui table)
        2) Descriptive Math Dashboard (statistics + bar chart)
        3) Predictive Analytics (Pearson r, regression line, R^2)
   * Updates the bar-chart renderer whenever the selected metric changes.

   Bug fixes applied in this revision
   -----------------------------------
   B-03  OpenPopup called inside BeginMenu scope – moved all OpenPopup
         calls to after EndMainMenuBar so they share the root ID stack
         with BeginPopupModal.  Deferred via bool flags.
   B-04  metricNames index accessed without empty-check – all accesses
         now guarded by !metricNames.empty().
   B-05  dataLoaded assigned twice in succession – duplicate removed.
   B-12  temp_pasted_data.csv written to cwd without cleanup – now uses
         std::filesystem::temp_directory_path(), checks stream health,
         and deletes the file after use.
   B-14  "Load Status" window shown forever with no dismiss path –
         replaced with a one-shot display that clears lastLoadedInfo
         after the "Data Loaded" popup is dismissed via its OK button.
   B-24  Hard-coded column header string duplicated schema knowledge –
         replaced with DataManager::GetColumnNames() so the schema
         lives in one place.
   B-31  Bar chart rendered directly to the OpenGL framebuffer while
         all other chart types rendered inside an ImGui child window,
         causing z-order conflicts and inconsistent layout. All chart
         types – including bar – now render through ImGui's DrawList
         API inside the unified ##full_chart_area child window.  The
         raw OpenGL clear + chartRenderer.draw() path is removed from
         the per-frame tail; the sidebar viewport split is kept only
         for background clearing.
=====================================================================*/
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <numeric>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Window.h"
#include "Shader.h"
#include "FileImporter.h"
#include "UI.h"
#include "SurveyRespondent.h"
#include "DataManager.h"
#include "Stats.h"
#include "BarChartRenderer.h"
#include "AxisRenderer.h"
#include "HoverDetector.h"

/*-----------------------------------------------------------------
   Helper – native Windows Open File dialog.
   Returns empty string if the user cancels.
-----------------------------------------------------------------*/
static std::string TriggerNativeFileBrowser()
{
    char szFile[260] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "CSV and Excel Files\0*.csv;*.xlsx\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn))
        return std::string(szFile);
    return std::string();   // cancelled
}

/*-----------------------------------------------------------------
   Simple statistical helpers (mean, median, variance, stddev)
-----------------------------------------------------------------*/
static void ComputeStats(const std::vector<float>& data,
    float& outMean,
    float& outMedian,
    float& outVariance,
    float& outStdDev)
{
    using namespace Statix::Stats;
    outMean = Mean(data);
    outMedian = Median(data);
    outVariance = Variance(data);
    outStdDev = StdDev(data);
}

/*-----------------------------------------------------------------
   Pearson correlation helper (wraps Statix::Stats::Pearson)
-----------------------------------------------------------------*/
static float PearsonCorrelation(const std::vector<float>& X,
    const std::vector<float>& Y)
{
    return Statix::Stats::Pearson(X, Y);
}

/*-----------------------------------------------------------------
   Linear regression wrapper
-----------------------------------------------------------------*/
static Statix::Stats::RegressionResult LinearFit(const std::vector<float>& X,
    const std::vector<float>& Y)
{
    return Statix::Stats::LinearRegression(X, Y);
}

/*-----------------------------------------------------------------
   Draw a bar chart entirely through ImGui's DrawList and hit-test
   the mouse in the exact same pixel coordinate space used to draw.

   Returns the index of the bar currently under the mouse, or -1.
   The progress vector from HoverDetector is used to lerp bar colour
   for smooth fade-in/out — same visual as the original OpenGL path.

   Parameters
   ----------
   drawList   – ImGui draw list (GetWindowDrawList)
   origin     – top-left of the drawing area in screen space
   size       – width × height of the drawing area
   values     – metric values (one bar per element)
   progress   – per-bar hover progress [0..1] from HoverDetector
   mousePos   – current mouse position in screen space (ImGui coords)
   colorBase  – RGBA [0-1] for fully-idle bars
   colorHL    – RGBA [0-1] for fully-hovered bars
-----------------------------------------------------------------*/
static int DrawBarChartImGui(ImDrawList* drawList,
    ImVec2                    origin,
    ImVec2                    size,
    const std::vector<float>& values,
    const std::vector<float>& progress,
    ImVec2                    mousePos,
    const float               colorBase[4],
    const float               colorHL[4])
{
    if (values.empty() || size.x <= 0.f || size.y <= 0.f)
        return -1;

    const int   n = static_cast<int>(values.size());
    const float maxVal = *std::max_element(values.begin(), values.end());
    if (maxVal <= 0.f) return -1;

    const float labelH = 18.f;
    const float chartH = size.y - labelH;
    const float barW = size.x / static_cast<float>(n);
    const float pad = barW * 0.12f;

    // Lerp between base and highlight colour using per-bar progress.
    auto lerpColor = [](const float a[4], const float b[4], float t) -> ImU32 {
        return IM_COL32(
            static_cast<int>((a[0] + (b[0] - a[0]) * t) * 255.f),
            static_cast<int>((a[1] + (b[1] - a[1]) * t) * 255.f),
            static_cast<int>((a[2] + (b[2] - a[2]) * t) * 255.f),
            static_cast<int>((a[3] + (b[3] - a[3]) * t) * 255.f));
        };
    const ImU32 colText = IM_COL32(220, 220, 220, 255);

    int hovered = -1;

    for (int i = 0; i < n; ++i)
    {
        const float norm = values[i] / maxVal;
        const float barHeight = norm * chartH;

        const float x0 = origin.x + i * barW + pad;
        const float x1 = origin.x + (i + 1) * barW - pad;
        const float y0 = origin.y + chartH - barHeight;
        const float y1 = origin.y + chartH;

        // Hit-test: same rects used for drawing, so they always match.
        if (mousePos.x >= x0 && mousePos.x <= x1 &&
            mousePos.y >= y0 && mousePos.y <= y1)
        {
            hovered = i;
        }

        const float t = (i < static_cast<int>(progress.size())) ? progress[i] : 0.f;
        const ImU32 col = lerpColor(colorBase, colorHL, t);
        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 3.f);

        if (barW > 24.f)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", values[i]);
            const ImVec2 tp(x0 + (x1 - x0) * 0.5f - ImGui::CalcTextSize(buf).x * 0.5f,
                y0 - 14.f);
            if (tp.y > origin.y)
                drawList->AddText(tp, colText, buf);
        }
    }

    return hovered;
}

/*-----------------------------------------------------------------
   Main
-----------------------------------------------------------------*/
int main()
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // -----------------------------------------------------------------
    // 1  Central data manager (holds all respondents)
    // -----------------------------------------------------------------
    Statix::DataManager dataManager;

    // -----------------------------------------------------------------
    // 2  OpenGL / GLFW window & Shaders
    // -----------------------------------------------------------------
    Statix::Window window(1280, 720, "STATIX Engine - Sociology Dashboard");

    // Shader is still compiled – BarChartRenderer owns OpenGL buffers
    // that are constructed once; we simply no longer call draw() in the
    // per-frame loop.  Keeping the object alive avoids touching its dtor.
    Statix::Shader shader(Statix::Shader::kVertexShaderSrc,
        Statix::Shader::kFragmentShaderSrc);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window.get_handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // -----------------------------------------------------------------
    // 3  Rendering helpers
    // -----------------------------------------------------------------
    Statix::BarChartRenderer chartRenderer(shader.get_id()); // kept for geometry / hover data
    Statix::AxisRenderer     axisRenderer;
    Statix::HoverDetector    hoverDetector;

    // -----------------------------------------------------------------
    // 4  UI state variables & Colors
    // -----------------------------------------------------------------
    int   selectedTab = 0;
    int   metricComboIdx = 0;
    int   xMetricIdx = 0;
    int   yMetricIdx = 1;
    bool  dataLoaded = false;
    std::string lastLoadedInfo;

    float colorBase[4] = { 0.192f, 0.592f, 0.584f, 1.0f };
    float colorHighlight[4] = { 0.25f,  0.88f,  0.82f,  1.0f };

    std::vector<std::string> metricNames = Statix::DataManager::GetMetricNames();

    // -----------------------------------------------------------------
    // B-03 FIX: popup-open requests deferred to root ID-stack scope.
    // -----------------------------------------------------------------
    bool openPopup_FileLoadError = false;
    bool openPopup_DataValidationError = false;
    bool openPopup_ParseError = false;
    bool openPopup_DataLoaded = false;

    // -----------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------
    while (!window.should_close())
    {
        window.poll_events();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Reset deferred-popup flags each frame.
        openPopup_FileLoadError = false;
        openPopup_DataValidationError = false;
        openPopup_ParseError = false;
        openPopup_DataLoaded = false;

        // -----------------------------------------------------------
        // 5  Top menu bar
        // -----------------------------------------------------------
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Load Dataset..."))
                {
                    std::string path = TriggerNativeFileBrowser();

                    if (!path.empty())
                    {
                        std::cout << "[LoadDataset] Selected file: " << path << std::endl;
                        Statix::Table rawTable;
                        std::string   parseError;

                        if (!Statix::FileImporter::LoadFile(path, rawTable, parseError))
                        {
                            ImGui::SetClipboardText(parseError.c_str());
                            openPopup_FileLoadError = true;
                        }
                        else
                        {
                            std::string loadError;
                            if (!dataManager.LoadTable(rawTable, loadError))
                            {
                                ImGui::SetClipboardText(loadError.c_str());
                                std::cout << "[LoadDataset] LoadTable error: " << loadError << std::endl;
                                openPopup_DataValidationError = true;
                            }
                            else
                            {
                                metricNames = Statix::DataManager::GetMetricNames();
                                metricComboIdx = 0;
                                xMetricIdx = 0;
                                yMetricIdx = 1;
                                selectedTab = 1;
                                dataLoaded = true;   // B-05: assigned exactly once

                                std::cout << "[LoadDataset] Rows loaded: " << dataManager.RowCount()
                                    << ", metrics: " << metricNames.size() << std::endl;

                                // B-04: guard metricNames access
                                if (!metricNames.empty())
                                {
                                    auto vec = dataManager.GetMetricColumn(metricNames[0]);
                                    chartRenderer.update_geometry(vec, hoverDetector.progress(),
                                        colorBase, colorHighlight);
                                }
                                else
                                {
                                    std::cout << "[LoadDataset] WARNING: No metric names." << std::endl;
                                }

                                lastLoadedInfo = "Loaded " + std::to_string(dataManager.RowCount())
                                    + " rows from " + path;
                                openPopup_DataLoaded = true;
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // -----------------------------------------------------------
        // B-03 FIX: all OpenPopup calls at root scope.
        // -----------------------------------------------------------
        if (openPopup_FileLoadError)       ImGui::OpenPopup("File Load Error");
        if (openPopup_DataValidationError) ImGui::OpenPopup("Data Validation Error");
        if (openPopup_ParseError)          ImGui::OpenPopup("Parse Error");
        if (openPopup_DataLoaded)          ImGui::OpenPopup("Data Loaded");

        // =====================================================
        // POPUPS
        // =====================================================

        if (ImGui::BeginPopupModal("Data Loaded", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Dataset loaded successfully.\nRows: %zu\n%s",
                dataManager.RowCount(), lastLoadedInfo.c_str());
            if (ImGui::Button("OK"))
            {
                lastLoadedInfo.clear();   // B-14 FIX
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("File Load Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                "Could not read the selected file.\n"
                "Error copied to clipboard.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Parse Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                "Failed to parse pasted text.\n"
                "Error copied to clipboard.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Data Validation Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                "The file contents were invalid.\n"
                "Details copied to clipboard.");
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // -----------------------------------------------------------
        // 6  No-data splash / paste panel
        // -----------------------------------------------------------
        if (!dataLoaded)
        {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(860, 420), ImGuiCond_Always);
            ImGui::Begin("No Data Loaded", nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::TextWrapped(
                "Load a CSV/XLSX using the \"Load Dataset\" button in the top menu,\n"
                "or paste raw CSV rows below and click \"Parse Pasted Text\".");

            static char pasteBuffer[65536] = "";
            ImGui::InputTextMultiline("##paste", pasteBuffer,
                IM_ARRAYSIZE(pasteBuffer),
                ImVec2(-1, 260),
                ImGuiInputTextFlags_AllowTabInput);

            if (ImGui::Button("Parse Pasted Text"))
            {
                // B-24 + B-12 FIX: schema-driven header, temp dir, stream check, cleanup.
                const std::vector<std::string> colNames = Statix::DataManager::GetColumnNames();
                std::string headerLine;
                for (size_t i = 0; i < colNames.size(); ++i) {
                    if (i) headerLine += ',';
                    headerLine += colNames[i];
                }
                headerLine += '\n';

                std::filesystem::path tmpPath =
                    std::filesystem::temp_directory_path() / "statix_pasted_data.csv";

                bool writeOk = false;
                {
                    std::ofstream tmp(tmpPath);
                    if (tmp.good()) {
                        tmp << headerLine << pasteBuffer;
                        writeOk = tmp.good();
                    }
                }

                if (!writeOk) {
                    ImGui::SetClipboardText("Failed to write temporary file for pasted data.");
                    openPopup_ParseError = true;
                    ImGui::OpenPopup("Parse Error");
                }
                else {
                    Statix::Table rawTable;
                    std::string   parseError;
                    if (!Statix::FileImporter::LoadFile(tmpPath.string(), rawTable, parseError))
                    {
                        ImGui::SetClipboardText(parseError.c_str());
                        openPopup_ParseError = true;
                        ImGui::OpenPopup("Parse Error");
                    }
                    else
                    {
                        std::string loadError;
                        if (!dataManager.LoadTable(rawTable, loadError))
                        {
                            ImGui::SetClipboardText(loadError.c_str());
                            openPopup_DataValidationError = true;
                            ImGui::OpenPopup("Data Validation Error");
                        }
                        else
                        {
                            metricNames = Statix::DataManager::GetMetricNames();
                            metricComboIdx = 0;
                            xMetricIdx = 0;
                            yMetricIdx = 1;
                            selectedTab = 1;
                            dataLoaded = true;

                            std::cout << "[ParsePasted] Rows loaded: " << dataManager.RowCount()
                                << ", metrics: " << metricNames.size() << std::endl;

                            // B-04 FIX: guard metricNames access
                            if (!metricNames.empty()) {
                                auto vec = dataManager.GetMetricColumn(metricNames[0]);
                                chartRenderer.update_geometry(vec, hoverDetector.progress(),
                                    colorBase, colorHighlight);
                            }

                            lastLoadedInfo = "Loaded " + std::to_string(dataManager.RowCount())
                                + " rows from pasted text.";
                            openPopup_DataLoaded = true;
                            ImGui::OpenPopup("Data Loaded");
                        }
                    }
                    std::filesystem::remove(tmpPath);   // B-12 FIX: always clean up
                }
            }
            ImGui::End();
        }

        // -----------------------------------------------------------
        // 7  Main dashboard layout (sidebar + central view)
        // -----------------------------------------------------------
        int winW, winH;
        window.get_size(winW, winH);
        int sidebarWidth = Statix::UI::draw_sidebar(selectedTab, winH);

        int fbW, fbH;
        window.get_framebuffer_size(fbW, fbH);
        const int sidebarPixelWidth =
            static_cast<int>(sidebarWidth * (static_cast<float>(fbW) / static_cast<float>(winW)));

        // -----------------------------------------------------------
        // 8  TAB-specific UI
        // -----------------------------------------------------------

        if (selectedTab == 0) // DASHBOARD
        {
            ImGui::BeginChild("##dashboard", ImVec2(0, 0), false,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            // ... dashboard code ...
            ImGui::EndChild();
        }
        else if (selectedTab == 1) // CHARTS
        {
            if (!metricNames.empty())
            {
                ImGui::BeginChild("##full_chart_area", ImVec2(0, 0), false);

                // ---- controls row ----
                ImGui::Text("Metric:");
                ImGui::SameLine();
                std::vector<const char*> comboItems;
                for (const auto& n : metricNames)
                    comboItems.push_back(n.c_str());

                if (metricComboIdx >= static_cast<int>(metricNames.size()))
                    metricComboIdx = 0;

                ImGui::Combo("##metric_combo", &metricComboIdx,
                    comboItems.data(), static_cast<int>(comboItems.size()));
                ImGui::SameLine();
                ImGui::Combo("Chart Type", &Statix::UI::g_chartType,
                    Statix::UI::chartTypeNames, Statix::UI::chartTypeCount);

                ImGui::Separator();
                ImGui::Spacing();

                const auto activeVals =
                    dataManager.GetMetricColumn(metricNames[metricComboIdx]);

                // ---- chart body ----
                switch (Statix::UI::g_chartType)
                {
                    // --------------------------------------------------------
                    // B-31 FIX: Bar chart now drawn via ImGui DrawList inside
                    // this child window, not via raw OpenGL outside it.
                    // --------------------------------------------------------
                case 0: // Bar Chart (ImGui DrawList)
                {
                    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

                    // Reserve the canvas area for input (hover, tooltip).
                    ImGui::InvisibleButton("##bar_canvas", canvasSize);

                    // Mouse position in screen space — same space DrawBarChartImGui
                    // uses, so hit-testing is exact.
                    const ImVec2 mousePos = ImGui::GetIO().MousePos;

                    // Ensure progress vector matches bar count.
                    hoverDetector.resize(activeVals.size());

                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    // Draw bars AND hit-test in one pass; returns hovered index.
                    const int hoveredBar = DrawBarChartImGui(
                        dl, canvasPos, canvasSize,
                        activeVals, hoverDetector.progress(),
                        mousePos, colorBase, colorHighlight);

                    // Advance smooth animation toward/away from hovered bar.
                    hoverDetector.tick(ImGui::GetIO().DeltaTime, hoveredBar);

                    // Axis ticks & labels overlay (AxisRenderer still uses its
                    // own ViewportInfo; pass it the same canvas region).
                    Statix::ChartBounds  bounds;
                    Statix::ViewportInfo viewport =
                        axisRenderer.compute(winW, winH, sidebarWidth, bounds);
                    axisRenderer.draw(dl, viewport,
                        activeVals, metricNames[metricComboIdx], bounds);

                    // Tooltip.
                    Statix::UI::draw_bar_tooltip(hoveredBar, activeVals);
                    break;
                }

                case 1: // Pie Chart
                    Statix::UI::render_pie_chart(activeVals, { "Low", "Medium", "High" });
                    break;

                case 2: // Scatter Plot
                {
                    if (xMetricIdx >= static_cast<int>(metricNames.size())) xMetricIdx = 0;
                    if (yMetricIdx >= static_cast<int>(metricNames.size())) yMetricIdx = 0;
                    const auto X = dataManager.GetMetricColumn(metricNames[xMetricIdx]);
                    const auto Y = dataManager.GetMetricColumn(metricNames[yMetricIdx]);
                    Statix::UI::render_scatter_plot(X, Y);
                    break;
                }

                case 3: // Histogram
                    Statix::UI::render_histogram(activeVals, 10);
                    break;
                }

                ImGui::EndChild();
            }
        }
        else if (selectedTab == 2)
        {
            if (!metricNames.empty())
            {
                ImGui::BeginChild("##modeling", ImVec2(0, 0), false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
                // ... modeling code ...
                ImGui::EndChild();
            }
        }

        // -----------------------------------------------------------
        // 9  OpenGL background clear only – no chart draw call here.
        //
        //    B-31 FIX: chartRenderer.draw() has been removed from this
        //    section entirely.  All chart rendering now happens inside
        //    the ImGui child window in step 8 above, so every chart
        //    type is on the same surface and respects ImGui's clipping,
        //    z-order, and layout.
        //
        //    The sidebar-aware viewport split is kept so the background
        //    colour matches the dark theme to the right of the sidebar.
        // -----------------------------------------------------------
        glViewport(sidebarPixelWidth, 0, fbW - sidebarPixelWidth, fbH);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Restore full viewport before ImGui render pass.
        glViewport(0, 0, fbW, fbH);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window.swap_buffers();
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return 0;
}