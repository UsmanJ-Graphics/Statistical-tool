@echo off
rem --------------------------------------------------------------
rem Build script for StatixEngine (Visual Studio 2022)
rem --------------------------------------------------------------

rem ==== 1. Set up a dedicated build folder =====================
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%build"

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

rem ==== 2. Run CMake to generate a VS2022 solution ==========
pushd "%BUILD_DIR%"
echo Configuring solution with CMake...
cmake -G "Visual Studio 17 2022" -A x64 ".."
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    popd
    exit /b 1
)

rem ==== 3. Build the Release configuration ====================
echo Building StatixEngine (Release)...
cmake --build . --config Release --target StatixEngine
if %errorlevel% neq 0 (
    echo [ERROR] Build failed.
    popd
    exit /b 1
)

popd

rem ==== 4. Run the generated executable =========================
set "EXE=%BUILD_DIR%\\Release\\StatixEngine.exe"
if exist "%EXE%" (
    echo ---------------------------------------------------------
    echo Build succeeded! Running the application now...
    echo ---------------------------------------------------------
    "%EXE%"
) else (
    echo [ERROR] Executable not found: %EXE%
)
