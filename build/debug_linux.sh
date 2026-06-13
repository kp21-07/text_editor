#!/bin/bash

mkdir -p bin

clang++ ./build/build.cpp -o bin/editor -O0 -g -std=c++11 -fsanitize=address -fno-omit-frame-pointer -lX11 -lGL -lXrandr
