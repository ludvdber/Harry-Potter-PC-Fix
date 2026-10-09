@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem The byte search of hooks.cpp (a sequence found twice is refused), without a game.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source hooks_test.cpp ..\source\hooks.cpp /Feout\hooks_test.exe /Foout\ >out\build_hooks.txt
if errorlevel 1 (type out\build_hooks.txt & exit /b 1)
out\hooks_test.exe
