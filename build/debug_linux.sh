#!/bin/bash

mkdir -p bin

zig c++ ./build/build.cpp -o bin/editor -O0 -g -std=c++11 -lX11 -lGL -lXrandr -fsanitize=undefined
