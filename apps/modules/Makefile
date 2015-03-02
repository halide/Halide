include ../support/Makefile.inc

HALIDE_LD_FLAGS=../../$(LIB_HALIDE) -lpthread -ldl -lz $(LDFLAGS)

all: out.png

# The implementations of the library functions
my_library_impl.a: vignette_impl.o flip_impl.o
	ar rvs my_library_impl.a vignette_impl.o flip_impl.o

# A library that contains the stub functions the generator uses.
my_library.a: my_library/my_library.cpp
	$(CXX) $(CXXFLAGS) -c my_library/my_library.cpp -o my_library.o
	ar q my_library.a my_library.o

# A generated library function
vignette_impl.o: my_library/vignette
	DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./my_library/vignette

# A library function generator
my_library/vignette: my_library/vignette.cpp
	$(CXX) $(CXXFLAGS) my_library/vignette.cpp -o my_library/vignette $(HALIDE_LD_FLAGS)

# A generated library function
flip_impl.o: my_library/flip
	DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./my_library/flip

# A library function generator
my_library/flip: my_library/flip.cpp
	$(CXX) $(CXXFLAGS) my_library/flip.cpp -o my_library/flip $(HALIDE_LD_FLAGS)

# The generator for the output pipeline
generate_pipeline: generate_pipeline.cpp my_library.a
	$(CXX) $(CXXFLAGS) generate_pipeline.cpp my_library.a -o generate_pipeline $(HALIDE_LD_FLAGS)

# The output pipeline
pipeline.o: generate_pipeline
	DYLD_LIBRARY_PATH=../../bin LD_LIBRARY_PATH=../../bin ./generate_pipeline

# A library containing the output pipeline and also the library implementation
pipeline.a: pipeline.o my_library_impl.a
	$(LD) -r -o final.o pipeline.o my_library_impl.a
	ar rvs pipeline.a final.o

# A binary that runs the output pipeline on an image
run_pipeline: run_pipeline.cpp pipeline.a
	$(CXX) $(CXXFLAGS) run_pipeline.cpp pipeline.a -o run_pipeline -I ../support -lpthread $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

out.png: run_pipeline
	./run_pipeline ../images/gray.png out.png

clean:
	rm -f *.o *.a *_impl.h out.png pipeline.h generate_pipeline run_pipeline my_library/vignette my_library/flip
