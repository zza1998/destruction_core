@echo off
setlocal
set "DEMO_DIR=%~dp0"
if "%BLAST_ROOT%"=="" set "BLAST_ROOT=%DEMO_DIR%.."
set "OUT=%DEMO_DIR%build"
if not exist "%OUT%" mkdir "%OUT%"

REM Auto-load the Visual Studio compiler environment so this script works from
REM a plain cmd window (no need to open the Developer Command Prompt first).
where cl >nul 2>nul
if errorlevel 1 (
  if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)
where cl >nul 2>nul
if errorlevel 1 (
  echo No Visual Studio 2022 C++ toolchain found. Install VS2022 with the
  echo "Desktop development with C++" workload, or run this from a Developer Command Prompt.
  exit /b 1
)
set "SDK=%BLAST_ROOT%\_build\windows-x86_64\release\blast-sdk"
set "GLFW=%BLAST_ROOT%\..\flow\external\glfw"
set "IMGUI=%BLAST_ROOT%\..\flow\external\imgui"
set "PHYSX=%DEMO_DIR%..\..\physx"
set "PX_INC=%PHYSX%\include"
set "PX_LIB=%PHYSX%\bin\win.x86_64.vc143.mt\release"
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /Fe:"%OUT%\StructuralModelTests.exe" "%DEMO_DIR%tests\StructuralModelTests.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\StaticGravitySolver.cpp" /link /LIBPATH:"%SDK%\bin" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\ReducedStaticsTests.exe" "%DEMO_DIR%tests\ReducedStaticsTests.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\SparseCholeskyTests.exe" "%DEMO_DIR%tests\SparseCholeskyTests.cpp" "%DEMO_DIR%src\SparseCholeskySolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\choltest2.exe" "%DEMO_DIR%tests\choltest2.cpp" "%DEMO_DIR%src\SparseCholeskySolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /Fe:"%OUT%\StaticGravitySolverTests.exe" "%DEMO_DIR%tests\StaticGravitySolverTests.cpp" "%DEMO_DIR%src\StaticGravitySolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /DBLAST_3D /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /I"%PX_INC%" /Fe:"%OUT%\PhysicsSmokeTests.exe" "%DEMO_DIR%tests\PhysicsSmokeTests.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\StaticGravitySolver.cpp" "%DEMO_DIR%src\PhysicsWorld.cpp" /link /LIBPATH:"%SDK%\bin" /LIBPATH:"%PX_LIB%" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib PhysX_64.lib PhysXFoundation_64.lib PhysXCommon_64.lib PhysXCooking_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXTask_static_64.lib
if errorlevel 1 exit /b 1
copy /y "%SDK%\bin\*.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysX_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXFoundation_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCommon_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCooking_64.dll" "%OUT%" >nul
if exist "%PX_LIB%\PVDRuntime_64.dll" copy /y "%PX_LIB%\PVDRuntime_64.dll" "%OUT%" >nul
"%OUT%\StructuralModelTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\ReducedStaticsTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\SparseCholeskyTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\choltest2.exe"
if errorlevel 1 exit /b 1
"%OUT%\StaticGravitySolverTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\PhysicsSmokeTests.exe"
