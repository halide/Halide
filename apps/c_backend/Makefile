include ../support/Makefile.inc

test: run run_cpp
	./run
	./run_cpp

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

pipeline_cpp: pipeline_cpp.cpp
	$(CXX) $(CXXFLAGS) -Wall pipeline_cpp.cpp $(LDFLAGS) $(LIB_HALIDE) -o pipeline_cpp $(LDFLAGS) -g

pipeline_cpp_cpp.cpp: pipeline_cpp
	./pipeline_cpp

pipeline_cpp_cpp.h: pipeline_cpp
	./pipeline_cpp

pipeline_cpp_native.h: pipeline_cpp
	./pipeline_cpp

pipeline_cpp_native.o: pipeline_cpp
	./pipeline_cpp

run_cpp: run_cpp.cpp pipeline_cpp_native.h pipeline_cpp_cpp.cpp
	$(CXX) $(CXXFLAGS) -Wall run_cpp.cpp pipeline_cpp_cpp.cpp pipeline_cpp_native.o $(LDFLAGS) -o run_cpp

clean:
	rm -f run{,_cpp} pipeline{,_cpp}_native.{h,o} pipeline{,_cpp}_c.{cpp,h} pipeline{,_cpp}
