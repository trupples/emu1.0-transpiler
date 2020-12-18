#!/bin/bash

python3 emu1c.py samples/$1.rom $2 > samples/$1.cpp
g++ samples/$1.cpp -lpthread $(sdl2-config --cflags --libs) -o samples/$1-compiled
samples/$1-compiled

