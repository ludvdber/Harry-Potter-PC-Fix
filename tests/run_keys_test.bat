@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
rem Named like HP4's executable: the action names (MoveUp, Charm...) are HP4's.
cl /nologo /std:c++20 /EHsc /W4 /WX /I..\source keys_test.cpp ..\source\keys.cpp /Feout\gof_f.exe /Foout\ user32.lib dxguid.lib >out\build.txt
if errorlevel 1 (type out\build.txt & exit /b 1)
out\gof_f.exe "%~dp0out\keys_test.ini"
