#!/bin/bash

mkdir -p .bin/

time clang++ main.cpp -o .bin/editor -O0 -lX11 -lGL -lXrandr -g  -std=c++11
