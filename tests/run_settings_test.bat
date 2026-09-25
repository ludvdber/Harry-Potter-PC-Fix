@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem Reads data\HP4, data\HP5 and data\HP6 with the DLL's own settings reader.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source settings_test.cpp ..\source\settings.cpp /Feout\settings_test.exe /Foout\ >out\build_settings.txt
if errorlevel 1 (type out\build_settings.txt & exit /b 1)
out\settings_test.exe "%~dp0..\data" "%~dp0out"
