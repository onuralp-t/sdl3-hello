@echo off
set sdl3=-I./thirdparty/SDL3-3.4.18-windows-vc/include -L./thirdparty/SDL3-3.4.18-windows-vc/lib/x64 -lSDL3
@echo on
clang -o main.exe main.c %sdl3% -O2
