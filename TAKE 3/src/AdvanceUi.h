#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "imgui.h"
#include "SurveyRespondent.h"
#include "StatisticsEngine.h"
namespace Statix {
    namespace UI {
        /*=====================================================================
          Advanced Control‑Panel UI
          – Tab bar with three distinct views:
             1️⃣ Raw spreadsheet (scrollable table)
             2️⃣ Descriptive statistics (combo → card widget)
             3️⃣ Correlation & regression (matrix + formula)
          The functions are deliberately **stateless** (no blocking calls) and
          expect the caller to pass a reference to the already‑populated
          `std::vector<SurveyRespondent>` data store.
        =====================================================================*/
        class ControlPanel
        {
        public:
            explicit ControlPanel(std::vector<SurveyRespondent>& data)
                : respondents_(data), currentTab_(0), selectedStat_(0)
            {
                // Populate the combo list with the variables we can calculate.
                statNames_ = {
                    "Age",
                    "Hours",               // raw float hour value
                    "Exposure Score",
                    "Normalization Score",
                    "Platform Attitude",
                    "RealWorld Score",
                    "Total Score"
                };
            }
            /** Call once per ImGui frame (e.g. from UI::draw_control_panel). */
            void Render()
            {
                // --------------------------------------------------------------
                // Tab Bar – three tabs, style friendly for a side‑bar layout.
                // --------------------------------------------------------------
                if (ImGui::BeginTabBar("##ControlPanelTabs", ImGuiTabBarFlags_None))
                {
                    if (ImGui::BeginTabItem("Raw Spreadsheet"))
                    {
                        RenderSpreadsheetTab();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Descriptive Stats"))
                    {
                        RenderStatsTab();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Correlation & Regression"))
                    {
                        RenderCorrelationTab();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        private:
            // -----------------------------------------------------------------
            // 1️⃣ Raw Spreadsheet view – a scrollable ImGui table.
            // -----------------------------------------------------------------
            void RenderSpreadsheetTab()
            {
                constexpr int NUM_COLUMNS = 24; // same as CSV column count
                static ImGuiTableFlags flags = ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingFixedFit;
                // Header titles (must stay in sync with SurveyRespondent order)
                static const char* columnHeaders[NUM_COLUMNS] = {
                    "Timestamp","Gender","Age","Education","Hours","Platform","Content",
                    // Q7‑Q21 (15 values)
                    "Q7","Q8","Q9","Q10","Q11","Q12","Q13","Q14","Q15","Q16","Q17","Q18","Q19","Q20","Q21",
                    "Comment1","Comment2"
                };
                ImGui::BeginChild("##SpreadsheetScroll", ImVec2(0, 0), false);
                if (ImGui::BeginTable("##SpreadsheetTable", NUM_COLUMNS, flags))
                {
                    // Header row
                    for (int c = 0; c < NUM_COLUMNS; ++c)
                        ImGui::TableSetupColumn(columnHeaders[c], ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    // Data rows – **no heavy work**, just direct access.
                    for (const auto& r : respondents_)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.timestamp.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.gender.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%d", r.age);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.education.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.2f", r.hours);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.platform.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.content.c_str());
                        // Q7‑Q21
                        for (size_t i = 0; i < r.likertResponses.size(); ++i)
                        {
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", r.likertResponses[i]);
                        }
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.comment1.c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.comment2.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();
            }
            // -----------------------------------------------------------------
            // 2️⃣ Descriptive statistics tab – combo + card widget.
            // -----------------------------------------------------------------
            void RenderStatsTab()
            {
                // Combo to choose the variable we want to analyse.
                ImGui::PushItemWidth(-1);
                if (ImGui::Combo("##VariableCombo", &selectedStat_, statNames_.data(),
                    static_cast<int>(statNames_.size())))
                {
                    // user changed selection – clear cached results so they are recomputed.
                    cacheValid_ = false;
                }
                ImGui::PopItemWidth();
                // -----------------------------------------------------------------
                // Compute stats **once per selection** (cached) – cheap O(N).
                // -----------------------------------------------------------------
                if (!cacheValid_)
                {
                    const auto accessor = GetAccessorForCurrentStat();
                    cachedStats_ = Statix::StatisticsEngine::Compute(
                        respondents_,
                        accessor,
                        statNames_[selectedStat_]);   // returns a formatted ASCII table string
                    cacheValid_ = true;
                }
                // Card‑style rendering (rounded frame, subtle shadow)
                ImGui::BeginChild("##StatsCard", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollbar);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.13f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
                ImGui::TextUnformatted(cachedStats_.c_str());
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::EndChild();
            }
            // -----------------------------------------------------------------
            // 3️⃣ Correlation & regression tab.
            // -----------------------------------------------------------------
            void RenderCorrelationTab()
            {
                // -----------------------------------------------------------------
                // UI – two combo boxes to pick X and Y variables.
                // -----------------------------------------------------------------
                static int comboX = 0, comboY = 1; // default to first two entries
                ImGui::PushItemWidth(-1);
                ImGui::Combo("##XVariable", &comboX, statNames_.data(),
                    static_cast<int>(statNames_.size()));
                ImGui::Combo("##YVariable", &comboY, statNames_.data(),
                    static_cast<int>(statNames_.size()));
                ImGui::PopItemWidth();
                // -----------------------------------------------------------------
                // Compute Pearson correlation (once per combo change)
                // -----------------------------------------------------------------
                if (comboX != lastComboX_ || comboY != lastComboY_)
                {
                    auto xAcc = GetAccessorForStat(comboX);
                    auto yAcc = GetAccessorForStat(comboY);
                    corrResult_ = Statix::StatisticsEngine::Pearson(
                        respondents_, xAcc, yAcc);
                    regResult_ = Statix::StatisticsEngine::LinearRegression(
                        respondents_, xAcc, yAcc);
                    // Create a tiny printable formula for the UI.
                    if (regResult_.valid)
                        regressionFormula_ = statNames_[comboY] + " = " +
                        std::to_string(regResult_.slope) + " * " +
                        statNames_[comboX] + " + " +
                        std::to_string(regResult_.intercept);
                    else
                        regressionFormula_ = "Regression not available";
                    lastComboX_ = comboX;
                    lastComboY_ = comboY;
                }
                // -----------------------------------------------------------------
                // Display correlation matrix cell (1x1 for pair) – colour‑coded.
                // -----------------------------------------------------------------
                ImGui::BeginChild("##CorrelationBox", ImVec2(0, 150), true);
                const float r = corrResult_.r;
                ImVec4 bg = (r > 0.0f) ? ImVec4(0.2f, 0.8f, 0.2f, 0.4f)   // positive = green‑ish
                    : ImVec4(0.8f, 0.2f, 0.2f, 0.4f);  // negative = red‑ish
                ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
                ImGui::BeginChild("##CorrInner", ImVec2(0, 0));
                if (corrResult_.valid)
                    ImGui::Text("Pearson r (%s ↔ %s) = %.4f",
                        statNames_[comboX].c_str(),
                        statNames_[comboY].c_str(),
                        r);
                else
                    ImGui::Text("Correlation undefined (constant data)");
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::EndChild();
                // -----------------------------------------------------------------
                // Show regression formula.
                // -----------------------------------------------------------------
                ImGui::Separator();
                ImGui::TextUnformatted("Linear Regression");
                ImGui::Separator();
                ImGui::Text("%s", regressionFormula_.c_str());
                if (regResult_.valid)
                    ImGui::Text("R² = %.4f", regResult_.rSquared);
            }
            // -----------------------------------------------------------------
            // Helper: return a lambda extracting the currently selected metric.
            // -----------------------------------------------------------------
            auto GetAccessorForCurrentStat()
            {
                return GetAccessorForStat(selectedStat_);
            }
            std::function<float(const SurveyRespondent&)> GetAccessorForStat(int idx)
            {
                // Map indices to concrete getters – keep in sync with `statNames_`.
                switch (idx)
                {
                case 0:  return [](const SurveyRespondent& r) { return static_cast<float>(r.age); };
                case 1:  return [](const SurveyRespondent& r) { return r.hours; };
                case 2:  return [](const SurveyRespondent& r) { return r.exposureScore(); };
                case 3:  return [](const SurveyRespondent& r) { return r.normalizationScore(); };
                case 4:  return [](const SurveyRespondent& r) { return r.platformAttitude(); };
                case 5:  return [](const SurveyRespondent& r) { return r.realWorldScore(); };
                case 6:  return [](const SurveyRespondent& r) { return r.totalScore(); };
                default: return [](const SurveyRespondent&) { return 0.0f; };
                }
            }
            // -----------------------------------------------------------------
            // Member data
            // -----------------------------------------------------------------
            std::vector<SurveyRespondent>& respondents_;
            int   currentTab_;                 // ImGui selected tab (unused – ImGui manages it)
            int   selectedStat_;               // index into `statNames_`
            std::vector<std::string> statNames_;
            // Cached descriptive‑stats ASCII output (re‑computed only when combo changes)
            std::string cachedStats_;
            bool        cacheValid_ = false;
            // Correlation / regression state
            int   comboX = 0, comboY = 1;
            int   lastComboX_ = -1, lastComboY_ = -1;
            Statix::StatisticsEngine::CorrelationResult corrResult_{ 0.0f,false };
            Statix::StatisticsEngine::RegressionResult   regResult_{ 0.0f,0.0f,0.0f,false };
            std::string regressionFormula_;
        };