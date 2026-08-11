@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
echo === build_tests.bat ===
call build_tests.bat
if errorlevel 1 exit /b 1
echo === build.bat ===
call build.bat
if errorlevel 1 exit /b 1
echo === smoke-test ===
build\BlastSupportGraphDemo.exe --smoke-test
if errorlevel 1 exit /b 1
echo === smoke-test --3d ===
build\BlastSupportGraphDemo.exe --smoke-test --3d
if errorlevel 1 exit /b 1
echo === _verify_fragments ===
call _verify_build.bat
if errorlevel 1 exit /b 1
build\_verify_fragments.exe
if errorlevel 1 exit /b 1
echo ALL_CHECKS_PASSED
