#!/bin/bash

mkdir -p ./bin

time {
	clang++ entrypoint.cpp -o bin/editor -std=c++11 -O0 -g -lX11 -lGL -lXrandr
}
