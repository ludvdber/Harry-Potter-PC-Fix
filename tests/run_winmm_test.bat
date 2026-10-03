@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem remap.cpp without a pad, then the built data\winmm.dll loaded by the test (build it first).
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source winmm_test.cpp ..\source\winmm\remap.cpp /Feout\winmm_test.exe /Foout\ user32.lib >out\build_winmm.txt
if errorlevel 1 (type out\build_winmm.txt & exit /b 1)
out\winmm_test.exe "%CD%\..\data\winmm.dll"
