#!/bin/sh
export LD_LIBRARY_PATH=../../src:../../audio/src
export DYLD_FALLBACK_LIBRARY_PATH=../../src:../../audio/src
./mediaassets_test
