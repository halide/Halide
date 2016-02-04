include ../support/Makefile.inc

pipeline: pipeline.cpp
	$(CXX) $(CXXFLAGS) -Wall pipeline.cpp $(LDFLAGS) $(LIB_HALIDE) -o pipeline $(LDFLAGS) -g

pipeline_c.cpp: pipeline
	./pipeline

pipeline_c.h: pipeline
	./pipeline

pipeline_native.h: pipeline
	./pipeline

pipeline_native.o: pipeline
	./pipeline

run: run.cpp pipeline_native.h pipeline_c.cpp
	$(CXX) $(CXXFLAGS) -Wall run.cpp pipeline_c.cpp pipeline_native.o $(LDFLAGS) -o run

test: run
	./run

clean:
	rm -f run pipeline_native.{h,o} pipeline_c.{cpp,h} pipeline
