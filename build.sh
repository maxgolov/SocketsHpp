#!/bin/sh
mkdir -p out
cd out
cmake -GNinja -DSOCKETSHPP_BUILD_TESTS=ON ..
ninja
cd ..
