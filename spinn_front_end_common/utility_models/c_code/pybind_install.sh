#!/bin/bash

git clone --recursive https://github.com/bast/pybind11-demo
cd pybind11-demo
mkdir build
rm -f CMakeLists.txt
mv ../CMakeLists.txt ./
rm -f example.cpp
cd build
cmake ..
make
