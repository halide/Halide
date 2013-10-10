CXX ?= g++

ifdef BUILD_PREFIX
LIB_HALIDE = bin/$(BUILD_PREFIX)/libHalide.a
else
LIB_HALIDE = bin/libHalide.a
endif

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif
ifeq ($(HL_TARGET),ptx-debug)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

pipeline: pipeline.cpp
	$(CXX) -Wall pipeline.cpp -I ../../include ../../$(LIB_HALIDE) -o pipeline -lpthread -ldl

pipeline_c.c: pipeline
	./pipeline

pipeline_c.h: pipeline
	./pipeline

pipeline_native.h: pipeline
	./pipeline

pipeline_native.o: pipeline
	./pipeline

run: run.cpp pipeline_native.h pipeline_c.c
	$(CXX) -Wall run.cpp pipeline_c.c pipeline_native.o -lpthread -o run $(CUDA_LDFLAGS)

test: run
	./run

clean:
	rm -f run pipeline_native.{h,o} pipeline_c.{c,h} pipeline