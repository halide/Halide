CXX ?= g++

pipeline: pipeline.cpp
	$(CXX) -Wall pipeline.cpp -I ../../include ../../bin/libHalide.a -o pipeline -lpthread -ldl

pipeline_c.c: pipeline
	./pipeline

pipeline_c.h: pipeline
	./pipeline

pipeline_native.h: pipeline
	./pipeline

pipeline_native.o: pipeline
	./pipeline

run: run.cpp pipeline_native.h pipeline_c.c
	$(CXX) -Wall run.cpp pipeline_c.c pipeline_native.o -lpthread -o run

test: run
	./run

clean:
	rm -f run pipeline_native.{h,o} pipeline_c.{c,h} pipeline