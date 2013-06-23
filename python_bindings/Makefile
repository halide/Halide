all: halide/cHalide_wrap.cxx

halide/cHalide_wrap.cxx: halide/cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h ../include/*.h halide/py_util.cpp halide/py_util.h halide/expand_types.h
	cd ..; make
	swig -c++ -python -I../include halide/cHalide.i
	CXX=g++ CC=g++ python setup.py build_ext --inplace

install:
	python setup.py install

clean:
	rm -rf build
	rm -f halide/cHalide_wrap.cxx
	rm -f halide/halide.so
	rm -f halide/_cHalide.so
	rm -f halide/cHalide.pyc halide/cHalide.py halide/halide.pyc

test:
	python -c "import halide; halide.test()"
