all: halide/cHalide_wrap.cxx

halide/cHalide_wrap.cxx: halide/cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h ../include/*.h halide/py_util.cpp halide/py_util.h halide/expand_types.h
	cd ..; make
	swig -c++ -python -I../include halide/cHalide.i

build: halide/cHalide_wrap.cxx
	python setup.py build_ext --inplace

install: halide/cHalide_wrap.cxx
	python setup.py install

clean:
	rm -rf build
	rm -f halide/cHalide_wrap.cxx
	rm -f halide/halide.so
	rm -f halide/_cHalide.so
	rm -f halide/cHalide.py halide/*.pyc

run_apps:
	python apps/blur.py
	python apps/local_laplacian.py
	python apps/interpolate.py
	python apps/bilateral_grid.py

test:
	python test_halide.py
