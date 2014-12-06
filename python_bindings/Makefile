all: halide/cHalide_wrap.cxx halide/_cHalide.so

HALIDE_BUILD_PATH = ../build2
PYTHON = python3
#PYTHON = python

halide/_cHalide.so: halide/cHalide_wrap.cxx halide/*.py halide/cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h $(HALIDE_BUILD_PATH)/include/*.h halide/py_util.cpp halide/py_util.h halide/expand_types.h setup.py
	HALIDE_ROOT=$(HALIDE_BUILD_PATH) BUILD_PREFIX=../lib $(PYTHON) setup.py build_ext --inplace

halide/cHalide_wrap.cxx: halide/cHalide.i ../src/*.cpp ../src/runtime/*.cpp ../src/*.h $(HALIDE_BUILD_PATH)/include/*.h halide/py_util.cpp halide/py_util.h halide/expand_types.h setup.py
#make -C ..
	swig -c++ -python -w362,325,314,389,381,382,361,401,503,509 -I$(HALIDE_BUILD_PATH)/include halide/cHalide.i

install: all
	HALIDE_ROOT=$(HALIDE_BUILD_PATH) BUILD_PREFIX=../lib $(PYTHON) setup.py install

clean:
	rm -rf build
	rm -f halide/cHalide_wrap.cxx
	rm -f halide/halide.so
	rm -f halide/_cHalide.so
	rm -f halide/cHalide.py halide/*.pyc

run_apps:
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/local_laplacian.py
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/interpolate.py
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/bilateral_grid.py
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/erode.py
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/blur.py

run_apps_headless:
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/local_laplacian.py apps/out_local_laplacian.png
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/interpolate.py apps/out_interpolate.png
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/bilateral_grid.py apps/out_bilateral_grid.png
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/erode.py apps/out_erode.png
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) apps/blur.py apps/out_blur.png

test:
	PYTHONPATH=.:$(PYTHONPATH) $(PYTHON) test_halide.py
