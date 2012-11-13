GXX ?= g++-4.7

fast_interp: fast_interp.cpp load_save_png.cpp
	$(GXX) -O3 -DCOMPILE $^ -lpng -fopenmp -o $@

