@echo **************************
@echo *** Use MinGW compiler ***
@echo **************************
@set PATH=D:\compilers\MinGW\bin;%PATH%
@set CC=gcc
mingw32-make.exe -f make-cygwin
@echo off
if exist pic32prog.exe @echo COMPILE SUCCESS!
pause