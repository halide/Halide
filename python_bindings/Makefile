all: cHalide_wrap.cxx

cHalide_wrap.cxx: cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h ../include/*.h py_util.cpp py_util.h expand_types.h
	cd ..; make
	swig -c++ -python -I../include cHalide.i
	CXX=g++ CC=g++ python setup.py build_ext --inplace

install:
	python setup.py install

clean:
	rm -rf build
	rm -f cHalide_wrap.cxx
	rm -f halide.so
	rm -f _cHalide.so
	rm -f cHalide.pyc cHalide.py halide.pyc

test:
	python -c "import halide; halide.test()"
