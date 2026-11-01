@echo off
set sdl3=-I./thirdparty/SDL3-devel-3.4.18-VC/include -L./thirdparty/SDL3-devel-3.4.18-VC/lib/x64 -lSDL3
@echo on
clang -o main.exe main.c %sdl3% -O2
