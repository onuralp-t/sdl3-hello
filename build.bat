@echo off
set sdl3=-I./thirdparty/SDL3-devel-3.4.18-VC/include -L./thirdparty/SDL3-devel-3.4.18-VC/lib/x64 -lSDL3
set vulkan=-IC:/VulkanSDK/1.4.363.0/Include -LC:/VulkanSDK/1.4.363.0/Lib -lvulkan-1

if not exist build mkdir build
copy /y .\thirdparty\SDL3-devel-3.4.18-VC\lib\x64\SDL3.dll .\build\
@echo on
glslc ./src/ui.vert -o ./build/ui.vert.spv  && ^
glslc ./src/ui.frag -o ./build/ui.frag.spv  && ^
clang -o ./build/main.exe ./src/main.c %sdl3% %vulkan% -Wall -Wextra -Wpedantic -Og -g3 -Wl,/STACK:16777216,/SUBSYSTEM:WINDOWS,/ENTRY:mainCRTStartup
