@echo off
rem Local test: needs the built DLL (..\data\d3d9.dll) and a Direct3D 9 graphics card.
rem The window is never shown. Every overlay line is switched on: text drawn with D3DX is what
rem used to make Direct3D take the Present slot back.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out\present mkdir out\present
copy /y ..\data\d3d9.dll out\present\ >nul || (echo FAIL no ..\data\d3d9.dll, build first & exit /b 1)
> out\present\d3d9.ini (
	echo [Accio.Overlay]
	echo ShowFPS=1
	echo ShowFrameTime=1
	echo ShowGraph=1
	echo ShowCPU=1
	echo ShowGPU=1
	echo ShowVRAM=1
	echo ShowRAM=1
	echo ShowLatency=1
)
cl /nologo /std:c++20 /EHsc /W4 /WX present_test.cpp /Feout\present_test.exe /Foout\ user32.lib >out\present_build.txt
if errorlevel 1 (type out\present_build.txt & exit /b 1)
out\present_test.exe "%~dp0out\present" 300
