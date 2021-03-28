#! /usr/bin/env python3

import os

CC = 'clang++ -std=c++17 -O3'

GTEST = "/usr/local/scaligent/toolchain/local"

INCLUDES = f"-I{GTEST}/include"

GTEST_LIB = f"{GTEST}/lib/libgtest.a"

OUTPUT_BIN = "/tmp/lazy_map_test"

run_command = lambda c : (print(c), os.system(c))

COMPILE = f"{CC} lazy_map_test.cpp {INCLUDES} {GTEST_LIB} -o {OUTPUT_BIN}"

run_command(f"{COMPILE} && time {OUTPUT_BIN}")

