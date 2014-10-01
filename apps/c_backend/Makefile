include ../support/Makefile.inc

pipeline: pipeline.cpp
	$(CXX) -Wall pipeline.cpp -I ../../include $(LDFLAGS) ../../$(LIB_HALIDE) -o pipeline -lpthread -ldl -lz -g

pipeline_c.c: pipeline
	./pipeline

pipeline_c.h: pipeline
	./pipeline

pipeline_native.h: pipeline
	./pipeline

pipeline_native.o: pipeline
	./pipeline

run: run.cpp pipeline_native.h pipeline_c.c
	$(CXX) -Wall -Wno-unused-function run.cpp pipeline_c.c pipeline_native.o -lpthread -o run $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

test: run
	./run

clean:
	rm -f run pipeline_native.{h,o} pipeline_c.{c,h} pipeline
