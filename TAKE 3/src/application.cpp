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
Fully self-contained bar chart via ImGui DrawList.
Draws: title, Y-axis title (vertical chars), Y-axis ticks +
gridlines, X-axis labels ("S1"…), value labels above bars.
Hit-tests in the exact same pixel rects used for drawing so
hover is always accurate.
Returns hovered bar index, or -1.
-----------------------------------------------------------------*/
static int DrawBarChartImGui(ImDrawList* drawList,
ImVec2                    origin,
ImVec2                    size,
const std::vector<float>& values,
const std::vector<float>& progress,
ImVec2                    mousePos,
const float               colorBase[4],
const float               colorHL[4],
const std::string& columnName = "")
{
if (values.empty() || size.x <= 0.f || size.y <= 0.f)
return -1;

const int   n = static_cast<int>(values.size());
const float maxVal = *std::max_element(values.begin(), values.end());
if (maxVal <= 0.f) return -1;

// ── Layout margins ───────────────────────────────────────────
const float titleH = 26.f;   // top: chart title
const float xLabelH = 20.f;   // bottom: S1…Sn labels
const float yTitleW = 14.f;   // far-left: vertical "Value" title
const float yLabelW = 52.f;   // left: numeric tick labels

const float plotX = origin.x + yTitleW + yLabelW;
const float plotY = origin.y + titleH;
const float plotW = size.x - yTitleW - yLabelW;
const float plotH = size.y - titleH - xLabelH;

if (plotW <= 0.f || plotH <= 0.f) return -1;

// ── Palette ──────────────────────────────────────────────────
const ImU32 colAxis = IM_COL32(160, 100, 220, 200);
const ImU32 colTick = IM_COL32(200, 170, 255, 200);
const ImU32 colGrid = IM_COL32(160, 100, 255, 22);
const ImU32 colVal = IM_COL32(230, 210, 255, 255);
const ImU32 colTitle = IM_COL32(230, 210, 255, 255);

auto lerpColor = [](const float a[4], const float b[4], float t) -> ImU32 {
return IM_COL32(
static_cast<int>((a[0] + (b[0] - a[0]) * t) * 255.f),
static_cast<int>((a[1] + (b[1] - a[1]) * t) * 255.f),
static_cast<int>((a[2] + (b[2] - a[2]) * t) * 255.f),
static_cast<int>((a[3] + (b[3] - a[3]) * t) * 255.f));
};

// ── Chart title ──────────────────────────────────────────────
if (!columnName.empty())
{
ImVec2 tsz = ImGui::CalcTextSize(columnName.c_str());
drawList->AddText(
ImVec2(plotX + plotW * 0.5f - tsz.x * 0.5f, origin.y + 4.f),
colTitle, columnName.c_str());
}

// ── Y-axis title: "Value" printed vertically char-by-char ────
{
const char* ytitle = "Value";
const float chH = ImGui::GetTextLineHeight();
const int   len = static_cast<int>(std::strlen(ytitle));
const float startY = plotY + plotH * 0.5f - chH * len * 0.5f;
for (int ci = 0; ci < len; ++ci)
{
char ch[2] = { ytitle[ci], '\0' };
float chW = ImGui::CalcTextSize(ch).x;
drawList->AddText(
ImVec2(origin.x + 2.f + (yTitleW - chW) * 0.5f, startY + ci * chH),
colTick, ch);
}
}

// ── Y ticks + grid lines ─────────────────────────────────────
const int kTicks = 5;
for (int i = 0; i <= kTicks; ++i)
{
const float t = static_cast<float>(i) / kTicks;
const float yPx = plotY + plotH - t * plotH;
const float tickVal = t * maxVal;

drawList->AddLine(ImVec2(plotX, yPx),
ImVec2(plotX + plotW, yPx), colGrid, 1.f);
drawList->AddLine(ImVec2(plotX - 5.f, yPx),
ImVec2(plotX, yPx), colAxis, 1.5f);

char buf[24];
std::snprintf(buf, sizeof(buf), "%.1f", tickVal);
ImVec2 tsz = ImGui::CalcTextSize(buf);
drawList->AddText(
ImVec2(plotX - 8.f - tsz.x, yPx - tsz.y * 0.5f),
colTick, buf);
}

// ── Axis lines ───────────────────────────────────────────────
drawList->AddLine(ImVec2(plotX, plotY),
ImVec2(plotX, plotY + plotH), colAxis, 2.f);
drawList->AddLine(ImVec2(plotX, plotY + plotH),
ImVec2(plotX + plotW, plotY + plotH), colAxis, 2.f);

// ── Bars, value labels, X-axis labels ────────────────────────
const float barSlot = plotW / static_cast<float>(n);
const float pad = barSlot * 0.12f;
int hovered = -1;

for (int i = 0; i < n; ++i)
{
const float norm = values[i] / maxVal;
const float barHeight = norm * plotH;

const float x0 = plotX + i * barSlot + pad;
const float x1 = plotX + (i + 1) * barSlot - pad;
const float y0 = plotY + plotH - barHeight;
const float y1 = plotY + plotH;

if (mousePos.x >= x0 && mousePos.x <= x1 &&
mousePos.y >= y0 && mousePos.y <= y1)
hovered = i;

const float hp = (i < static_cast<int>(progress.size())) ? progress[i] : 0.f;
drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
lerpColor(colorBase, colorHL, hp), 3.f);

// Value label above bar
{
char buf[16];
std::snprintf(buf, sizeof(buf), "%.1f", values[i]);
ImVec2 tsz = ImGui::CalcTextSize(buf);
const float tx = x0 + (x1 - x0) * 0.5f - tsz.x * 0.5f;
const float ty = y0 - tsz.y - 2.f;
if (ty > plotY)
drawList->AddText(ImVec2(tx, ty), colVal, buf);
}

// X-axis label
{
char buf[20];
std::snprintf(buf, sizeof(buf), " R-%d", i + 1);
ImVec2 tsz = ImGui::CalcTextSize(buf);
drawList->AddText(
ImVec2(x0 + (x1 - x0) * 0.5f - tsz.x * 0.5f, plotY + plotH + 4.f),
colTick, buf);
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
io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/arial.ttf", 14.0f);
ImGui::StyleColorsDark();
// NOTE: No widget calls (TextUnformatted, TextDisabled, etc.) here —
// they require an active frame and will crash with g.CurrentWindow == nullptr.
// ----------------------------------------------------------------
//  STATIX Vivid Theme — warm violet → cyan gradient palette
//  Background: deep indigo-plum  |  Accent: electric violet + cyan
// ----------------------------------------------------------------


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

float colorBase[4] = { 0.42f, 0.14f, 0.82f, 1.0f };  // electric violet
float colorHighlight[4] = { 0.10f, 0.88f, 0.88f, 1.0f };  // vivid cyan

std::vector<std::string> metricNames = Statix::DataManager::GetMetricNames();

// -----------------------------------------------------------------
// Cached analytics – computed once on load, never inside the render
// loop.  Recomputed by RebuildCache() whenever dataManager changes.
// -----------------------------------------------------------------
struct MetricCache {
std::string name;
std::vector<float> values;
float mean = 0.f, median = 0.f, variance = 0.f, stddev = 0.f;
float minV = 0.f, maxV = 0.f;
};
struct GenderCache {
int nMale = 0, nFemale = 0, nOther = 0;
float avgMale = 0.f, avgFemale = 0.f;
};
// Full 5x5 Pearson matrix [row][col]
struct CorrCache {
float r[5][5] = {};
};

std::vector<MetricCache> metricCache;
GenderCache              genderCache;
CorrCache                corrCache;

// Call this once after every successful LoadTable().
auto RebuildCache = [&]()
{
metricCache.clear();
for (const auto& mName : metricNames)
{
MetricCache mc;
mc.name = mName;
mc.values = dataManager.GetMetricColumn(mName);
if (!mc.values.empty())
{
ComputeStats(mc.values, mc.mean, mc.median, mc.variance, mc.stddev);
mc.minV = *std::min_element(mc.values.begin(), mc.values.end());
mc.maxV = *std::max_element(mc.values.begin(), mc.values.end());
}
metricCache.push_back(std::move(mc));
}

// Gender breakdown
genderCache = {};
for (const auto& r : dataManager.GetRespondents())
{
std::string g = r.gender;
std::transform(g.begin(), g.end(), g.begin(), ::tolower);
if (g == "male") { ++genderCache.nMale;   genderCache.avgMale += r.TotalScore(); }
else if (g == "female") { ++genderCache.nFemale; genderCache.avgFemale += r.TotalScore(); }
else { ++genderCache.nOther; }
}
if (genderCache.nMale > 0) genderCache.avgMale /= genderCache.nMale;
if (genderCache.nFemale > 0) genderCache.avgFemale /= genderCache.nFemale;

// Pearson matrix
int nm = static_cast<int>(metricCache.size());
for (int row = 0; row < nm && row < 5; ++row)
for (int col = 0; col < nm && col < 5; ++col)
corrCache.r[row][col] = (row == col) ? 1.f
: PearsonCorrelation(metricCache[row].values, metricCache[col].values);
};

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
RebuildCache();

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
// OpenPopup deferred to root scope via flag — do NOT call here
}
else {
Statix::Table rawTable;
std::string   parseError;
if (!Statix::FileImporter::LoadFile(tmpPath.string(), rawTable, parseError))
{
ImGui::SetClipboardText(parseError.c_str());
openPopup_ParseError = true;
// OpenPopup deferred to root scope via flag — do NOT call here
}
else
{
std::string loadError;
if (!dataManager.LoadTable(rawTable, loadError))
{
ImGui::SetClipboardText(loadError.c_str());
openPopup_DataValidationError = true;
// OpenPopup deferred to root scope via flag — do NOT call here
}
else
{
metricNames = Statix::DataManager::GetMetricNames();
metricComboIdx = 0;
xMetricIdx = 0;
yMetricIdx = 1;
selectedTab = 1;
dataLoaded = true;
RebuildCache();

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
// OpenPopup deferred to root scope via flag — do NOT call here
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

if (!dataLoaded)
{
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"No dataset loaded. Use File > Load Dataset to begin.");
}
else
{
// ── Title ────────────────────────────────────────────────
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"SURVEY DASHBOARD - %zu respondents", dataManager.RowCount());
ImGui::Separator();
ImGui::Spacing();

// ── Per-metric summary cards (read from cache, no computation) ──
const float cardW = 230.f;
const float cardH = 110.f;
const float padX = 12.f;
int cardCol = 0;

for (const auto& mc : metricCache)
{
if (mc.values.empty()) continue;

if (cardCol > 0) ImGui::SameLine(0.f, padX);

ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.10f, 0.26f, 1.f));
// Use a stable ID — metric name is unique and never changes mid-frame
ImGui::BeginChild(mc.name.c_str(), ImVec2(cardW, cardH), true);

ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "%s", mc.name.c_str());
ImGui::Separator();
ImGui::Text("Mean   : %.3f", mc.mean);
ImGui::Text("Std Dev: %.3f", mc.stddev);
ImGui::Text("Median : %.3f", mc.median);
ImGui::Text("Range  : %.2f - %.2f", mc.minV, mc.maxV);

ImGui::EndChild();
ImGui::PopStyleColor();

// Wrap after every 4 cards
++cardCol;
if (cardCol >= 4) { cardCol = 0; }
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Gender breakdown (read from cache) ───────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "GENDER BREAKDOWN");
ImGui::Spacing();

ImGui::Columns(3, "##gender_cols", true);
ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "Male");
ImGui::Text("n = %d", genderCache.nMale);
if (genderCache.nMale > 0)
ImGui::Text("Avg Total: %.3f", genderCache.avgMale);
ImGui::NextColumn();

ImGui::TextColored(ImVec4(1.f, 0.6f, 0.8f, 1.f), "Female");
ImGui::Text("n = %d", genderCache.nFemale);
if (genderCache.nFemale > 0)
ImGui::Text("Avg Total: %.3f", genderCache.avgFemale);
ImGui::NextColumn();

ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.f), "Other / N/A");
ImGui::Text("n = %d", genderCache.nOther);
ImGui::Columns(1);

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Full respondent table ────────────────────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "RAW RECORD MATRIX");
ImGui::Spacing();

if (ImGui::BeginTable("##raw_table", 9,
ImGuiTableFlags_Borders |
ImGuiTableFlags_RowBg |
ImGuiTableFlags_ScrollX |
ImGuiTableFlags_ScrollY |
ImGuiTableFlags_SizingFixedFit,
ImVec2(0.f, 300.f)))
{
ImGui::TableSetupScrollFreeze(0, 1);
ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40.f);
ImGui::TableSetupColumn("Gender", ImGuiTableColumnFlags_WidthFixed, 70.f);
ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("Education", ImGuiTableColumnFlags_WidthFixed, 110.f);
ImGui::TableSetupColumn("Exposure", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("Normaliz.", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("RealWorld", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 70.f);
ImGui::TableHeadersRow();

int rowN = 0;
for (const auto& r : dataManager.GetRespondents())
{
++rowN;
ImGui::TableNextRow();
ImGui::TableSetColumnIndex(0); ImGui::Text("%d", rowN);
ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r.gender.c_str());
ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.ageGroup.c_str());
ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(r.education.c_str());
ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", r.ExposureScore());
ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", r.NormalizationScore());
ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", r.PlatformAttitude());
ImGui::TableSetColumnIndex(7); ImGui::Text("%.2f", r.RealWorldScore());
ImGui::TableSetColumnIndex(8); ImGui::Text("%.2f", r.TotalScore());
}
ImGui::EndTable();
}
}

ImGui::EndChild();
}
else if (selectedTab == 1) // CHARTS
{
if (!metricNames.empty())
{
ImGui::BeginChild("##full_chart_area", ImVec2(0, 0), false);
if (!dataLoaded || metricNames.empty() || metricCache.empty())
{
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"No dataset loaded. Use File > Load Dataset to begin.");
ImGui::EndChild();
}
else
{
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

// Read from pre-built cache — no allocation per frame.
if (metricComboIdx >= static_cast<int>(metricCache.size()))
metricComboIdx = 0;
const auto& activeVals = metricCache[metricComboIdx].values;

// ---- chart body ----
switch (Statix::UI::g_chartType)
{
// --------------------------------------------------------
// B-31 FIX: Bar chart now drawn via ImGui DrawList inside
// this child window, not via raw OpenGL outside it.
// --------------------------------------------------------
case 0: // Bar Chart (ImGui DrawList, self-contained labels)
{
const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

// Reserve canvas area for hover input.
ImGui::InvisibleButton("##bar_canvas", canvasSize);

const ImVec2 mousePos = ImGui::GetIO().MousePos;
hoverDetector.resize(activeVals.size());

ImDrawList* dl = ImGui::GetWindowDrawList();

// DrawBarChartImGui draws title, axes, ticks, gridlines,
// value labels, and X labels — no external AxisRenderer needed.
const int hoveredBar = DrawBarChartImGui(
dl, canvasPos, canvasSize,
activeVals, hoverDetector.progress(),
mousePos, colorBase, colorHighlight,
metricNames[metricComboIdx]);   // <-- chart title

hoverDetector.tick(ImGui::GetIO().DeltaTime, hoveredBar);
Statix::UI::draw_bar_tooltip(hoveredBar, activeVals);
break;
}

case 1: // Pie Chart
Statix::UI::render_pie_chart(activeVals, { "Low", "Medium", "High" });
break;

case 2: // Scatter Plot
{
if (xMetricIdx >= static_cast<int>(metricCache.size())) xMetricIdx = 0;
if (yMetricIdx >= static_cast<int>(metricCache.size())) yMetricIdx = 0;

// ── Axis selectors ────────────────────────────────────────
std::vector<const char*> scatterCombo;
for (const auto& n : metricNames)
scatterCombo.push_back(n.c_str());

ImGui::Text("X Axis:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160.f);
ImGui::Combo("##scatterX", &xMetricIdx,
scatterCombo.data(), static_cast<int>(scatterCombo.size()));
ImGui::SameLine(0.f, 20.f);
ImGui::Text("Y Axis:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160.f);
ImGui::Combo("##scatterY", &yMetricIdx,
scatterCombo.data(), static_cast<int>(scatterCombo.size()));

ImGui::Spacing();

Statix::UI::render_scatter_plot(
metricCache[xMetricIdx].values,
metricCache[yMetricIdx].values);
break;
}

case 3: // Histogram
Statix::UI::render_histogram(activeVals, 10);
break;
}

ImGui::EndChild();
}
}
}
else if (selectedTab == 2) // ANALYTICS SETTINGS
{
ImGui::BeginChild("##modeling", ImVec2(0, 0), false,
ImGuiWindowFlags_AlwaysVerticalScrollbar);

if (!dataLoaded)
{
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"No dataset loaded. Use File > Load Dataset to begin.");
}
else
{
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "ANALYTICS SETTINGS");
ImGui::Separator();
ImGui::Spacing();

// ── Pearson correlation matrix ────────────────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"PEARSON CORRELATION MATRIX");
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"r values across all five sub-scores (n = %zu)", dataManager.RowCount());
ImGui::Spacing();


// Correlation matrix reads entirely from corrCache — zero
// computation per frame.
const int nm = static_cast<int>(metricCache.size());
if (ImGui::BeginTable("##corr_matrix", nm + 1,
ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
ImGuiTableFlags_SizingFixedFit))
{
ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130.f);
for (const auto& mc : metricCache)
ImGui::TableSetupColumn(mc.name.c_str(),
ImGuiTableColumnFlags_WidthFixed, 100.f);
ImGui::TableHeadersRow();

for (int row = 0; row < nm; ++row)
{
ImGui::TableNextRow();
ImGui::TableSetColumnIndex(0);
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"%s", metricCache[row].name.c_str());

for (int col = 0; col < nm; ++col)
{
ImGui::TableSetColumnIndex(col + 1);
float r = corrCache.r[row][col];
if (row == col) {
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.f), "1.000");
}
else {
ImVec4 rCol;
if (r > 0.5f) rCol = ImVec4(0.10f, 0.92f, 0.88f, 1.f);
else if (r > 0.2f) rCol = ImVec4(0.65f, 0.40f, 1.00f, 1.f);
else if (r < -0.5f) rCol = ImVec4(1.0f, 0.25f, 0.65f, 1.f);
else if (r < -0.2f) rCol = ImVec4(1.0f, 0.55f, 0.85f, 1.f);
else                rCol = ImVec4(0.75f, 0.68f, 0.95f, 1.f);
ImGui::TextColored(rCol, "%.3f", r);
}
}
}
ImGui::EndTable();
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Linear Regression ────────────────────────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"LINEAR REGRESSION");
ImGui::Spacing();

// X / Y metric selectors
std::vector<const char*> mCombo;
for (const auto& n : metricNames) mCombo.push_back(n.c_str());

ImGui::Text("Independent (X):");
ImGui::SameLine();
ImGui::SetNextItemWidth(180.f);
ImGui::Combo("##regX", &xMetricIdx,
mCombo.data(), static_cast<int>(mCombo.size()));

ImGui::Text("Dependent   (Y):");
ImGui::SameLine();
ImGui::SetNextItemWidth(180.f);
ImGui::Combo("##regY", &yMetricIdx,
mCombo.data(), static_cast<int>(mCombo.size()));

ImGui::Spacing();

if (xMetricIdx != yMetricIdx)
{
// Cache lookup — no allocation per frame.
const std::vector<float>& X = metricCache[xMetricIdx].values;
const std::vector<float>& Y = metricCache[yMetricIdx].values;

auto reg = LinearFit(X, Y);
float r = PearsonCorrelation(X, Y);

ImGui::PushStyleColor(ImGuiCol_ChildBg,
ImVec4(0.12f, 0.09f, 0.22f, 1.f));
ImGui::BeginChild("##reg_result",
ImVec2(0.f, 110.f), true);

ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"%s  →  %s",
metricNames[xMetricIdx].c_str(),
metricNames[yMetricIdx].c_str());
ImGui::Separator();

ImGui::Text("Formula : Y = %.4f * X  +  %.4f",
reg.slope, reg.intercept);
ImGui::Text("Pearson r: %.4f", r);
ImGui::Text("R^2      : %.4f", reg.rSquared);

// Verbal strength
float absR = std::fabs(r);
const char* strength =
absR >= 0.7f ? "Strong" :
absR >= 0.4f ? "Moderate" :
absR >= 0.2f ? "Weak" : "Negligible";
const char* direction = r >= 0.f ? "positive" : "negative";
ImGui::TextColored(ImVec4(0.45f, 0.90f, 1.00f, 1.f),
"Interpretation: %s %s correlation", strength, direction);

ImGui::EndChild();
ImGui::PopStyleColor();

// Inline scatter with regression line overlay
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.00f, 1.f),
"Scatter with regression line:");
ImGui::Spacing();

const float plotW = ImGui::GetContentRegionAvail().x;
const float plotH = 200.f;
const float margin = 10.f;

float minX = *std::min_element(X.begin(), X.end());
float maxX = *std::max_element(X.begin(), X.end());
float minY = *std::min_element(Y.begin(), Y.end());
float maxY = *std::max_element(Y.begin(), Y.end());
if (maxX - minX < 1e-5f) { minX -= .5f; maxX += .5f; }
if (maxY - minY < 1e-5f) { minY -= .5f; maxY += .5f; }

ImVec2 origin = ImGui::GetCursorScreenPos();
ImDrawList* dl = ImGui::GetWindowDrawList();

dl->AddRectFilled(origin,
ImVec2(origin.x + plotW, origin.y + plotH),
IM_COL32(18, 12, 38, 220));
dl->AddRect(origin,
ImVec2(origin.x + plotW, origin.y + plotH),
IM_COL32(100, 60, 180, 160));

auto toPixel = [&](float x, float y) -> ImVec2 {
float px = origin.x + margin
+ (x - minX) / (maxX - minX) * (plotW - margin * 2.f);
float py = origin.y + (plotH - margin)
- (y - minY) / (maxY - minY) * (plotH - margin * 2.f);
return ImVec2(px, py);
};

// Data points
for (size_t i = 0; i < X.size(); ++i)
dl->AddCircleFilled(toPixel(X[i], Y[i]), 4.f,
IM_COL32(220, 80, 255, 220));

// Regression line from minX to maxX
float yAtMin = reg.slope * minX + reg.intercept;
float yAtMax = reg.slope * maxX + reg.intercept;
dl->AddLine(toPixel(minX, yAtMin),
toPixel(maxX, yAtMax),
IM_COL32(0, 230, 230, 220), 2.f);

// X-axis min/max labels
char buf[24];
std::snprintf(buf, sizeof(buf), "%.1f", minX);
dl->AddText(ImVec2(origin.x + margin, origin.y + plotH - 16.f),
IM_COL32(180, 140, 255, 200), buf);
std::snprintf(buf, sizeof(buf), "%.1f", maxX);
ImVec2 tsz = ImGui::CalcTextSize(buf);
dl->AddText(ImVec2(origin.x + plotW - margin - tsz.x, origin.y + plotH - 16.f),
IM_COL32(180, 140, 255, 200), buf);

ImGui::Dummy(ImVec2(plotW, plotH + 8.f));
}
else
{
ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.3f, 1.f),
"Select different metrics for X and Y.");
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Descriptive stats table ───────────────────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"DESCRIPTIVE STATISTICS");
ImGui::Spacing();

if (ImGui::BeginTable("##desc_stats", 7,
ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
ImGuiTableFlags_SizingFixedFit))
{
ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 140.f);
ImGui::TableSetupColumn("Mean", ImGuiTableColumnFlags_WidthFixed, 70.f);
ImGui::TableSetupColumn("Median", ImGuiTableColumnFlags_WidthFixed, 70.f);
ImGui::TableSetupColumn("Std Dev", ImGuiTableColumnFlags_WidthFixed, 70.f);
ImGui::TableSetupColumn("Variance", ImGuiTableColumnFlags_WidthFixed, 80.f);
ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 60.f);
ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 60.f);
ImGui::TableHeadersRow();

// Read from cache — no computation per frame
for (const auto& mc : metricCache)
{
if (mc.values.empty()) continue;
ImGui::TableNextRow();
ImGui::TableSetColumnIndex(0);
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f),
"%s", mc.name.c_str());
ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", mc.mean);
ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", mc.median);
ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", mc.stddev);
ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", mc.variance);
ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", mc.minV);
ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", mc.maxV);
}
ImGui::EndTable();
}
}

ImGui::EndChild();
}

// -----------------------------------------------------------
// Theme Settings Tab (selectedTab == 3)
// -----------------------------------------------------------
else if (selectedTab == 3)
{
ImGui::BeginChild("##theme_settings", ImVec2(0, 0), false,
ImGuiWindowFlags_AlwaysVerticalScrollbar);

ImGui::Spacing();
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "THEME SETTINGS");
ImGui::Separator();
ImGui::Spacing();

// ── Bar Chart Colors ──────────────────────────────────────
ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.00f, 1.f), "Bar Chart Colors");
ImGui::Spacing();

ImGui::ColorEdit4("Bar Base Color", colorBase);
ImGui::ColorEdit4("Bar Highlight Color", colorHighlight);

// Live preview of the gradient blend
ImGui::Spacing();
ImGui::Text("Gradient Preview:");
ImGui::SameLine();
{
ImDrawList* dl = ImGui::GetWindowDrawList();
ImVec2 p = ImGui::GetCursorScreenPos();
const float pw = 200.f, ph = 20.f;
ImU32 left = IM_COL32(
(int)(colorBase[0] * 255), (int)(colorBase[1] * 255),
(int)(colorBase[2] * 255), (int)(colorBase[3] * 255));
ImU32 right = IM_COL32(
(int)(colorHighlight[0] * 255), (int)(colorHighlight[1] * 255),
(int)(colorHighlight[2] * 255), (int)(colorHighlight[3] * 255));
dl->AddRectFilledMultiColor(p, ImVec2(p.x + pw, p.y + ph),
left, right, right, left);
dl->AddRect(p, ImVec2(p.x + pw, p.y + ph),
IM_COL32(160, 100, 220, 180), 4.f);
ImGui::Dummy(ImVec2(pw + 4.f, ph + 4.f));
}

// Reset button
ImGui::Spacing();
if (ImGui::Button("Reset Bar Colors to Default"))
{
colorBase[0] = 0.42f; colorBase[1] = 0.14f;
colorBase[2] = 0.82f; colorBase[3] = 1.0f;
colorHighlight[0] = 0.10f; colorHighlight[1] = 0.88f;
colorHighlight[2] = 0.88f; colorHighlight[3] = 1.0f;
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Window / UI Colors ────────────────────────────────────
ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.00f, 1.f), "Window & UI Colors");
ImGui::Spacing();

ImGuiStyle& style = ImGui::GetStyle();

// Expose the most impactful colors as pickers
static float winBg[4] = { 0.09f, 0.07f, 0.16f, 1.f };
static float accentViolet[4] = { 0.72f, 0.35f, 1.00f, 1.f };
static float accentCyan[4] = { 0.10f, 0.88f, 0.88f, 1.f };
static float textColor[4] = { 0.95f, 0.92f, 1.00f, 1.f };
static float frameBg[4] = { 0.18f, 0.12f, 0.32f, 1.f };
static float menuBarBg[4] = { 0.14f, 0.08f, 0.28f, 1.f };

bool changed = false;

if (ImGui::ColorEdit4("Window Background", winBg))     changed = true;
if (ImGui::ColorEdit4("Accent – Violet", accentViolet)) changed = true;
if (ImGui::ColorEdit4("Accent – Cyan", accentCyan))   changed = true;
if (ImGui::ColorEdit4("Text Color", textColor))    changed = true;
if (ImGui::ColorEdit4("Frame Background", frameBg))      changed = true;
if (ImGui::ColorEdit4("Menu Bar", menuBarBg))    changed = true;

if (changed)
{
// Window backgrounds
style.Colors[ImGuiCol_WindowBg] = ImVec4(winBg[0], winBg[1], winBg[2], winBg[3]);
style.Colors[ImGuiCol_ChildBg] = ImVec4(winBg[0] + 0.02f, winBg[1] + 0.02f, winBg[2] + 0.04f, 1.f);
style.Colors[ImGuiCol_PopupBg] = ImVec4(winBg[0] + 0.03f, winBg[1] + 0.02f, winBg[2] + 0.06f, 0.97f);
style.Colors[ImGuiCol_MenuBarBg] = ImVec4(menuBarBg[0], menuBarBg[1], menuBarBg[2], menuBarBg[3]);

// Text
style.Colors[ImGuiCol_Text] = ImVec4(textColor[0], textColor[1], textColor[2], textColor[3]);

// Frames
style.Colors[ImGuiCol_FrameBg] = ImVec4(frameBg[0], frameBg[1], frameBg[2], frameBg[3]);
style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(frameBg[0] + 0.12f, frameBg[1] + 0.03f, frameBg[2] + 0.23f, 1.f);
style.Colors[ImGuiCol_FrameBgActive] = ImVec4(frameBg[0] + 0.22f, frameBg[1] + 0.06f, frameBg[2] + 0.38f, 1.f);

// Buttons use cyan accent on hover
style.Colors[ImGuiCol_Button] = ImVec4(accentViolet[0] * 0.5f, accentViolet[1] * 0.4f, accentViolet[2] * 0.8f, 1.f);
style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accentCyan[0], accentCyan[1], accentCyan[2], 1.f);
style.Colors[ImGuiCol_ButtonActive] = ImVec4(accentCyan[0] * 0.8f, accentCyan[1], accentCyan[2] * 0.9f, 1.f);

// Headers
style.Colors[ImGuiCol_Header] = ImVec4(accentViolet[0] * 0.42f, accentViolet[1] * 0.2f, accentViolet[2] * 0.73f, 1.f);
style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accentViolet[0] * 0.58f, accentViolet[1] * 0.25f, accentViolet[2] * 0.97f, 1.f);
style.Colors[ImGuiCol_HeaderActive] = ImVec4(accentCyan[0], accentCyan[1] * 0.80f, accentCyan[2], 1.f);

// Separator & borders
style.Colors[ImGuiCol_Separator] = ImVec4(accentViolet[0] * 0.76f, accentViolet[1] * 0.35f, accentViolet[2], 0.55f);
style.Colors[ImGuiCol_Border] = ImVec4(accentViolet[0] * 0.76f, accentViolet[1] * 0.35f, accentViolet[2], 0.45f);

// Scrollbar
style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(accentViolet[0] * 0.63f, accentViolet[1] * 0.28f, accentViolet[2], 1.f);
style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(accentViolet[0] * 0.76f, accentViolet[1] * 0.39f, accentViolet[2], 1.f);
style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(accentCyan[0], accentCyan[1], accentCyan[2], 1.f);

// Table
style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(accentViolet[0] * 0.28f, accentViolet[1] * 0.14f, accentViolet[2] * 0.5f, 1.f);
style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(accentViolet[0] * 0.76f, accentViolet[1] * 0.35f, accentViolet[2], 0.80f);
style.Colors[ImGuiCol_TableBorderLight] = ImVec4(accentViolet[0] * 0.42f, accentViolet[1] * 0.21f, accentViolet[2] * 0.76f, 0.50f);
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Rounding ──────────────────────────────────────────────
ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.00f, 1.f), "Corner Rounding");
ImGui::Spacing();
ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.f, 20.f, "%.0f px");
ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.f, 12.f, "%.0f px");
ImGui::SliderFloat("Child Rounding", &style.ChildRounding, 0.f, 16.f, "%.0f px");
ImGui::SliderFloat("Popup Rounding", &style.PopupRounding, 0.f, 16.f, "%.0f px");
ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.f, 12.f, "%.0f px");
ImGui::SliderFloat("Tab Rounding", &style.TabRounding, 0.f, 12.f, "%.0f px");

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Spacing ───────────────────────────────────────────────
ImGui::TextColored(ImVec4(0.75f, 0.60f, 1.00f, 1.f), "Spacing & Padding");
ImGui::Spacing();
ImGui::SliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.f, 20.f, "%.0f");
ImGui::SliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.f, 16.f, "%.0f");

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Reset All ─────────────────────────────────────────────
if (ImGui::Button("Reset ALL Theme to Default", ImVec2(240.f, 0.f)))
{
ImGui::StyleColorsDark();
// restore geometry only:
ImGuiStyle& s = ImGui::GetStyle();
s.WindowRounding = 10.f; s.ChildRounding = 8.f;
s.FrameRounding = 6.f;  s.PopupRounding = 8.f;
s.ScrollbarRounding = 6.f; s.TabRounding = 6.f;
s.ItemSpacing = ImVec2(10.f, 6.f);
s.FramePadding = ImVec2(8.f, 4.f);
// reset bar colors
colorBase[0] = 0.42f; colorBase[1] = 0.14f; colorBase[2] = 0.82f; colorBase[3] = 1.0f;
colorHighlight[0] = 0.10f; colorHighlight[1] = 0.88f; colorHighlight[2] = 0.88f; colorHighlight[3] = 1.0f;
}

ImGui::EndChild();
}
else if (selectedTab == 4) // NEW DATA FROM TEXT
{
ImGui::BeginChild("##text_input_tab", ImVec2(0, 0), false,
ImGuiWindowFlags_AlwaysVerticalScrollbar);

ImGui::Spacing();
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "NEW DATA FROM TEXT");
ImGui::Separator();
ImGui::Spacing();

ImGui::TextWrapped(
"Paste raw CSV rows below (no header needed — the schema is applied automatically).\n"
"Format: Gender, AgeGroup, Education, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, Q10, Q11, Q12\n"
"Click \"Load from Text\" to replace the current dataset, or \"Append to Dataset\" to add rows.");

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Text area ─────────────────────────────────────────────────
static char textInputBuffer[131072] = "";  // 128 KB
ImGui::Text("Paste CSV rows:");
ImGui::InputTextMultiline("##text_input_area", textInputBuffer,
IM_ARRAYSIZE(textInputBuffer),
ImVec2(-1.f, 300.f),
ImGuiInputTextFlags_AllowTabInput);

ImGui::Spacing();

// ── Row count preview ─────────────────────────────────────────
{
int lineCount = 0;
for (char* p = textInputBuffer; *p; ++p)
if (*p == '\n') ++lineCount;
if (textInputBuffer[0] != '\0') ++lineCount; // count last line if no trailing newline
ImGui::TextDisabled("Lines detected: %d (each non-empty line = 1 respondent)", lineCount);
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Action buttons ────────────────────────────────────────────
bool doLoad = ImGui::Button("Load from Text", ImVec2(180.f, 0.f));
ImGui::SameLine(0.f, 16.f);
bool doClear = ImGui::Button("Clear Text Area", ImVec2(140.f, 0.f));

// Helper: shared parse + load logic
auto ParseAndLoad = [&](bool append) -> bool
{
if (textInputBuffer[0] == '\0') {
ImGui::SetClipboardText("Text area is empty.");
ImGui::OpenPopup("Parse Error");
return false;
}

const std::vector<std::string> colNames = Statix::DataManager::GetColumnNames();
std::string headerLine;
for (size_t i = 0; i < colNames.size(); ++i) {
if (i) headerLine += ',';
headerLine += colNames[i];
}
headerLine += '\n';

std::filesystem::path tmpPath =
std::filesystem::temp_directory_path() / "statix_textinput_data.csv";

bool writeOk = false;
{
std::ofstream tmp(tmpPath);
if (tmp.good()) {
tmp << headerLine << textInputBuffer;
writeOk = tmp.good();
}
}

if (!writeOk) {
ImGui::SetClipboardText("Failed to write temporary file.");
std::filesystem::remove(tmpPath);
ImGui::OpenPopup("Parse Error");
return false;
}

Statix::Table rawTable;
std::string   parseError;
if (!Statix::FileImporter::LoadFile(tmpPath.string(), rawTable, parseError)) {
ImGui::SetClipboardText(parseError.c_str());
std::filesystem::remove(tmpPath);
ImGui::OpenPopup("Parse Error");
return false;
}

std::string loadError;
bool ok = false;
if (append && dataLoaded) {
ok = dataManager.AppendTable(rawTable, loadError);
}
else {
ok = dataManager.LoadTable(rawTable, loadError);
}

std::filesystem::remove(tmpPath);

if (!ok) {
ImGui::SetClipboardText(loadError.c_str());
ImGui::OpenPopup("Data Validation Error");
return false;
}

metricNames = Statix::DataManager::GetMetricNames();
metricComboIdx = 0;
xMetricIdx = 0;
yMetricIdx = 1;
dataLoaded = true;
RebuildCache();

if (!metricNames.empty()) {
auto vec = dataManager.GetMetricColumn(metricNames[0]);
chartRenderer.update_geometry(vec, hoverDetector.progress(),
colorBase, colorHighlight);
}

lastLoadedInfo = (append ? "Appended rows. Total: " : "Loaded ")
+ std::to_string(dataManager.RowCount()) + " respondents from text input.";
ImGui::OpenPopup("Data Loaded");
return true;
};

if (doLoad)  ParseAndLoad(false);  // replace
if (doClear) std::memset(textInputBuffer, 0, sizeof(textInputBuffer));

ImGui::Spacing();

// ── Append button (only shown when data already exists) ───────
if (dataLoaded)
{
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"Current dataset: %zu respondents", dataManager.RowCount());
ImGui::Spacing();
if (ImGui::Button("Append to Current Dataset", ImVec2(220.f, 0.f)))
ParseAndLoad(true);

ImGui::SameLine(0.f, 16.f);
ImGui::TextDisabled("(adds rows without discarding existing data)");
}
else
{
ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.75f, 1.f),
"No dataset loaded yet — \"Load from Text\" will create one.");
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// ── Quick format reference ─────────────────────────────────────
ImGui::TextColored(ImVec4(0.72f, 0.35f, 1.00f, 1.f), "FORMAT REFERENCE");
ImGui::Spacing();
ImGui::TextDisabled("One respondent per line, values comma-separated:");
ImGui::Spacing();
ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.07f, 0.20f, 1.f));
ImGui::BeginChild("##format_ref", ImVec2(0.f, 80.f), true);
ImGui::TextDisabled("Male,18-24,Bachelor,3,4,2,5,3,4,3,2,4,5,3,4");
ImGui::TextDisabled("Female,25-34,Master,4,5,4,3,5,4,5,4,3,4,5,4");
ImGui::TextDisabled("Other,35-44,High School,2,3,2,4,3,2,3,4,2,3,2,3");
ImGui::EndChild();
ImGui::PopStyleColor();

ImGui::EndChild();
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
ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
glClearColor(bg.x, bg.y, bg.z, bg.w);
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