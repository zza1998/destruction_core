@echo off
setlocal
set "DEMO_DIR=%~dp0"
if "%BLAST_ROOT%"=="" set "BLAST_ROOT=%DEMO_DIR%.."
set "OUT=%DEMO_DIR%build"
if not exist "%OUT%" mkdir "%OUT%"
set "SDK=%BLAST_ROOT%\_build\windows-x86_64\release\blast-sdk"
set "GLFW=%BLAST_ROOT%\..\flow\external\glfw"
set "IMGUI=%BLAST_ROOT%\..\flow\external\imgui"
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /Fe:"%OUT%\StructuralModelTests.exe" "%DEMO_DIR%tests\StructuralModelTests.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\LoadPathSolver.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp" /link /LIBPATH:"%SDK%\bin" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /DBLAST_SCENE_HEADLESS /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /I"%GLFW%\include" /I"%IMGUI%" /Fe:"%OUT%\OpenGLSceneTests.exe" "%DEMO_DIR%tests\OpenGLSceneTests.cpp" "%DEMO_DIR%src\OpenGLScene.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\LoadPathSolver.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp" /link /LIBPATH:"%SDK%\bin" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\ReducedStaticsTests.exe" "%DEMO_DIR%tests\ReducedStaticsTests.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\SparseCholeskyTests.exe" "%DEMO_DIR%tests\SparseCholeskyTests.cpp" "%DEMO_DIR%src\SparseCholeskySolver.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /Fe:"%OUT%\choltest2.exe" "%DEMO_DIR%tests\choltest2.cpp" "%DEMO_DIR%src\SparseCholeskySolver.cpp"
if errorlevel 1 exit /b 1
copy /y "%SDK%\bin\*.dll" "%OUT%" >nul
"%OUT%\StructuralModelTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\OpenGLSceneTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\ReducedStaticsTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\SparseCholeskyTests.exe"
if errorlevel 1 exit /b 1
"%OUT%\choltest2.exe"
