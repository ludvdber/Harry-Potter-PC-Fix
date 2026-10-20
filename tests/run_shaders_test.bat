@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
if exist out\shaders_scratch rmdir /s /q out\shaders_scratch
rem The shader extraction and replacement logic (fingerprint, dump, load), without a game.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source shaders_test.cpp ..\source\shaderswap.cpp /Feout\shaders_test.exe /Foout\ >out\build_shaders.txt
if errorlevel 1 (type out\build_shaders.txt & exit /b 1)
out\shaders_test.exe "%CD%\out"
