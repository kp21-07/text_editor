#!/bin/bash

mkdir -p bin

clang++ ./build/build.cpp -o bin/editor -O0 -g3 -std=c++11 -lX11 -lGL -lXrandr
