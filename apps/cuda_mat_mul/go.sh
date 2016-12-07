#!/bin/bash
g++ mat_mul.cpp -std=c++11 -I ../../include -L ../../bin -lHalide -fno-rtti -o generate
LD_LIBRARY_PATH=../../bin ./generate
g++ runner.cpp -I ../support/ -std=c++11 -I ../../include/ cuda_mat_mul.s -ldl -lpthread -o runner -lcublas -lcudart
HL_CUDA_JIT_MAX_REGISTERS=256 ./runner
