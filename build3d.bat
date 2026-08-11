@echo off
rem Backward-compatible alias for the unified build (2D + 3D in one exe).
call "%~dp0build.bat"
exit /b %errorlevel%
