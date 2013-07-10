all: halide/cHalide_wrap.cxx

halide/cHalide_wrap.cxx: halide/cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h ../include/*.h halide/py_util.cpp halide/py_util.h halide/expand_types.h
	make -C ..
	swig -c++ -python -w362,325,314,389,381,382,361,401,503,509 -I../include halide/cHalide.i

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
	python apps/local_laplacian.py
	python apps/interpolate.py
	python apps/bilateral_grid.py
	python apps/erode.py
	python apps/blur.py

run_apps_headless:
	python apps/local_laplacian.py apps/out_local_laplacian.png
	python apps/interpolate.py apps/out_interpolate.png
	python apps/bilateral_grid.py apps/out_bilateral_grid.png
	python apps/erode.py apps/out_erode.png
	python apps/blur.py apps/out_blur.png

test:
	python test_halide.py
