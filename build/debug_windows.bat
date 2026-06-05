@echo off

if not exist bin mkdir bin

zig c++ .\build\build.cpp -o .\bin\editor.exe -O0 -g -std=c++11 -fsanitize=undefined -lgdi32 -lopengl32
