@echo off
rem Builds cshell.dll. Run from this folder in an x86 Developer Command Prompt.
rem Includes are written from this folder ("core/log.h"), hence /I.
cl /nologo /LD /O2 /EHsc /I. ^
   core\*.cpp settings\*.cpp layout\*.cpp rendering\*.cpp ui\*.cpp input\*.cpp ^
   camera\*.cpp media\*.cpp options\*.cpp diagnostics\*.cpp ^
   user32.lib /link /DEF:cshell_hud.def /BASE:0x5A000000 /OUT:cshell.dll
