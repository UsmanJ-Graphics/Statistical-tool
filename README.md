STATIX Engine
A real-time sociology survey analytics dashboard built with OpenGL + Dear ImGui
STATIX Engine is a native Windows desktop application for loading, visualizing, and statistically analyzing Likert-scale survey data. It was built to support sociology research into social media's effects on society, providing researchers with an interactive, GPU-accelerated dashboard that turns raw CSV/XLSX datasets into actionable insights — no spreadsheet software required.

Features
Data Ingestion

Native Windows file picker for loading CSV and XLSX datasets
Paste-and-parse text input for rapid data entry without a file
Append mode to merge new rows into an existing loaded dataset
Schema-driven column mapping with case-insensitive header matching

Dashboard (Tab 1)

Per-metric summary cards: mean, median, std dev, min/max
Gender breakdown with average total scores per group
Full scrollable raw record matrix of all respondents

Charts (Tab 2)

Bar chart with animated hover highlights (ImGui DrawList, fully integrated)
Pie chart with explode-on-hover slice animation and legend
Scatter plot with per-point hover tooltips
Frequency histogram with configurable bins
Live metric selector — switch charts without reloading data

Analytics (Tab 3)

Full 5×5 Pearson correlation matrix across all sub-scores
Linear regression with slope, intercept, R², and verbal strength interpretation
Inline scatter plot with regression line overlay
Descriptive statistics table: mean, median, std dev, variance, min, max

Theme Settings (Tab 4)

Live color pickers for bar base and highlight gradient
Full ImGui style customization: window/frame/popup/menu colors
Corner rounding and spacing sliders
One-click reset to defaults


Tech Stack
LayerTechnologyRenderingOpenGL 3.3 Core ProfileUI FrameworkDear ImGuiWindowingGLFWGL LoaderGLADFile I/OCustom FileImporter (CSV + XLSX)StatisticsCustom Statix::Stats (mean, median, variance, Pearson, linear regression)LanguageC++17PlatformWindows (native file dialog via Comdlg32)

Sub-scores Computed
Each survey respondent is automatically scored across five dimensions derived from their Likert responses (Q7–Q21):

Exposure Score — frequency and depth of social media consumption
Normalization Score — acceptance of harmful content as normal
Platform Attitude — trust and sentiment toward social platforms
RealWorld Score — perceived real-world impact of online content
Total Score — composite across all dimensions


Getting Started
bash# Clone the repo
git clone https://github.com/your-username/statix-engine.git

# Build with your preferred CMake/MSVC setup
cmake -B build -S .
cmake --build build --config Release

# Run
./build/Release/StatixEngine.exe
Requirements: Windows 10+, OpenGL 3.3-capable GPU, MSVC or MinGW with C++17 support.
Font: Loads C:/Windows/Fonts/arial.ttf — present on all standard Windows installations.

Data Format
One respondent per row, comma-separated:
Timestamp, Gender, Age, Education, Hours, Platform, Content,
Q7, Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15, Q16, Q17r, Q18, Q19, Q20, Q21
Likert values accepted as integers (1–5) or text (Strongly Agree, Agree, Neutral, Disagree, Strongly Disagree). Q17r is reverse-coded automatically.

License
MIT License — free for academic and personal use.
