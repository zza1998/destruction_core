@echo off
setlocal
set "DEMO_DIR=%~dp0"
if "%BLAST_ROOT%"=="" set "BLAST_ROOT=%DEMO_DIR%.."
set "PHYSX=%DEMO_DIR%..\..\physx"
set "OUT=%DEMO_DIR%build"
if not exist "%OUT%" mkdir "%OUT%"
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from a Visual Studio Developer Command Prompt.
  exit /b 1
)
set "SDK=%BLAST_ROOT%\_build\windows-x86_64\release\blast-sdk"
set "GLFW=%BLAST_ROOT%\..\flow\external\glfw"
set "IMGUI=%BLAST_ROOT%\..\flow\external\imgui"
set "PX_INC=%PHYSX%\include"
set "PX_LIB=%PHYSX%\bin\win.x86_64.vc143.mt\release"
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /DBLAST_3D /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\extensions\shaders" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /I"%GLFW%\include" /I"%IMGUI%" /I"%PX_INC%" /Fe:"%OUT%\BlastSupportGraphDemo.exe" "%DEMO_DIR%src\main.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\LoadPathSolver.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp" "%DEMO_DIR%src\GLFunctions.cpp" "%DEMO_DIR%src\ImGuiOpenGLBackend.cpp" "%DEMO_DIR%src\OpenGLScene.cpp" "%DEMO_DIR%src\Scene3D.cpp" "%DEMO_DIR%src\PhysicsWorld.cpp" "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_widgets.cpp" /link /LIBPATH:"%SDK%\bin" /LIBPATH:"%GLFW%\win64" /LIBPATH:"%PX_LIB%" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib NvBlastExtShaders.lib glfw3dll.lib opengl32.lib user32.lib shell32.lib PhysX_64.lib PhysXFoundation_64.lib PhysXCommon_64.lib PhysXCooking_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXTask_static_64.lib
if errorlevel 1 exit /b 1
copy /y "%SDK%\bin\*.dll" "%OUT%" >nul
copy /y "%GLFW%\win64\glfw3.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysX_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXFoundation_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCommon_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCooking_64.dll" "%OUT%" >nul
if exist "%PX_LIB%\PVDRuntime_64.dll" copy /y "%PX_LIB%\PVDRuntime_64.dll" "%OUT%" >nul
echo Built %OUT%\BlastSupportGraphDemo.exe
echo Usage: BlastSupportGraphDemo.exe [--smoke-test] [--3d]
