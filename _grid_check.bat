@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "DEMO_DIR=%~dp0"
if "%BLAST_ROOT%"=="" set "BLAST_ROOT=%DEMO_DIR%.."
set "OUT=%DEMO_DIR%build"
set "SDK=%BLAST_ROOT%\_build\windows-x86_64\release\blast-sdk"
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /Fe:"%OUT%\_grid_layout_check.exe" "%DEMO_DIR%_grid_layout_check.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\LoadPathSolver.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp" /link /LIBPATH:"%SDK%\bin" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib
if errorlevel 1 exit /b 1
"%OUT%\_grid_layout_check.exe"
