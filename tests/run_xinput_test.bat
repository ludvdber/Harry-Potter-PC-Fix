@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem PlayStation reports read as an Xbox pad (source/xinput/pad.cpp), without a controller.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source xinput_test.cpp ..\source\xinput\pad.cpp /Feout\xinput_test.exe /Foout\ >out\build_xinput.txt
if errorlevel 1 (type out\build_xinput.txt & exit /b 1)
out\xinput_test.exe
