@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /W3 main.cpp /Fe:gsmenu.exe user32.lib gdi32.lib /link /SUBSYSTEM:CONSOLE
