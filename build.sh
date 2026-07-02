#!/bin/bash

source ~/vulkan/1.4.341.1/setup-env.sh


rm -rf build
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)

# Copy compiled shaders to project root so the binary can be run from there
cd ..
cp build/shaders/*.spv shaders/
