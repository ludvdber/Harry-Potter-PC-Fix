@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem pad.cpp without a pad, then the built data\dinput8.dll loaded by the test (build it first).
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source dinput8_test.cpp ..\source\dinput8\pad.cpp /Feout\dinput8_test.exe /Foout\ user32.lib ole32.lib >out\build_dinput8.txt
if errorlevel 1 (type out\build_dinput8.txt & exit /b 1)
out\dinput8_test.exe "%CD%\..\data\dinput8.dll"
