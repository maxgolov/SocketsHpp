#!/bin/sh
mkdir -p out
cd out
cmake -GNinja ..
ninja
cd ..
