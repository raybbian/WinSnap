@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo
cl /nologo /W4 /O2 winsnap.c /link user32.lib shell32.lib /SUBSYSTEM:WINDOWS
