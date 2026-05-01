#!/bin/bash

source ~/vulkan/1.4.341.1/setup-env.sh


rm -rf build
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

cmake --build . -j$(nproc)
