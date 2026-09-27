@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem The mipmap filter and pixel formats of mipmaps.cpp, without a game or a GPU.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source mipmaps_test.cpp ..\source\mipmaps.cpp /Feout\mipmaps_test.exe /Foout\ >out\build_mipmaps.txt
if errorlevel 1 (type out\build_mipmaps.txt & exit /b 1)
out\mipmaps_test.exe
