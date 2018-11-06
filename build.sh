#!/bin/bash
clang "engine.c" -I/usr/local/include -L/usr/local/lib -F./frameworks -framework SDL -o build/engine -O0 -g
#cd build
#open ./engine