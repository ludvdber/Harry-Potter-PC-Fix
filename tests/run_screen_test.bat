@echo off
rem Local test: needs the built DLL (..\data\dinput8.dll) and a graphics card (Windows' d3d8.dll
rem runs on Direct3D 9). The window is never shown. Every overlay line is switched on so the
rem panel is drawn from the first frame. To test against the game's own D3D8.dll (its D3D8 translator)
rem instead of Windows', copy it next to out\screen_test.exe as d3d8.dll before running the exe, then
rem delete the copy (done by hand on 2026-10-21: same log, all checks passed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out\screen mkdir out\screen
copy /y ..\data\dinput8.dll out\screen\ >nul || (echo FAIL no ..\data\dinput8.dll, build first & exit /b 1)
> out\screen\dinput8.ini (
	echo [Accio.Overlay]
	echo ShowFPS=1
	echo ShowFrameTime=1
	echo ShowGraph=1
)
cl /nologo /std:c++20 /EHsc /W4 /WX screen_test.cpp /Feout\screen_test.exe /Foout\ user32.lib >out\screen_build.txt
if errorlevel 1 (type out\screen_build.txt & exit /b 1)
out\screen_test.exe "%~dp0out\screen" 120
