@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
set "DEMO_DIR=%~dp0"
if "%BLAST_ROOT%"=="" set "BLAST_ROOT=%DEMO_DIR%.."
set "PHYSX=%DEMO_DIR%..\..\physx"
set "OUT=%DEMO_DIR%build"
set "SDK=%BLAST_ROOT%\_build\windows-x86_64\release\blast-sdk"
set "PX_INC=%PHYSX%\include"
set "PX_LIB=%PHYSX%\bin\win.x86_64.vc143.mt\release"
cl /nologo /std:c++14 /EHsc /W4 /DNDEBUG /I"%DEMO_DIR%include" /I"%DEMO_DIR%src" /I"%BLAST_ROOT%\include" /I"%BLAST_ROOT%\include\toolkit" /I"%BLAST_ROOT%\include\extensions\shaders" /I"%BLAST_ROOT%\include\globals" /I"%BLAST_ROOT%\include\lowlevel" /I"%BLAST_ROOT%\include\shared\NvFoundation" /I"%PX_INC%" /Fe:"%OUT%\_verify_fragments.exe" "%DEMO_DIR%_verify_fragments.cpp" "%DEMO_DIR%src\BlastSupportModel.cpp" "%DEMO_DIR%src\BlastRuntime.cpp" "%DEMO_DIR%src\SupportGraphSolver.cpp" "%DEMO_DIR%src\LoadPathSolver.cpp" "%DEMO_DIR%src\ReducedStaticsSolver.cpp" "%DEMO_DIR%src\PhysicsWorld.cpp" /link /LIBPATH:"%SDK%\bin" /LIBPATH:"%PX_LIB%" NvBlastTk.lib NvBlast.lib NvBlastGlobals.lib NvBlastExtShaders.lib PhysX_64.lib PhysXFoundation_64.lib PhysXCommon_64.lib PhysXCooking_64.lib PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXTask_static_64.lib
if errorlevel 1 exit /b 1
copy /y "%SDK%\bin\*.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysX_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXFoundation_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCommon_64.dll" "%OUT%" >nul
copy /y "%PX_LIB%\PhysXCooking_64.dll" "%OUT%" >nul
echo Built %OUT%\_verify_fragments.exe
exit /b 0
