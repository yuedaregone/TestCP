#!/usr/bin/env bash
cd ./script
../tools/bin/darwin/genie --gcc=osx gmake

cd ../build/projects/gmake-osx
make

