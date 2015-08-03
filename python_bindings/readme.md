
## Prerequisites ##


The bindings (and demonstration applications) should work well both for python2.7 and python3.4 (or higher),
and across platforms.

(Currently only tested on Ubuntu 14.04 and OS X 10.10 with python3.4 and a Halide source build).

#### Python requirements:
 See requirements.txt (to be used with `pip`: `pip install --user requirements.txt`)

#### C++ requirements:
 Halide compiled (assuming `halide_git_clone_path/build`).
 For a better python experience make sure Halide was compiled with `WITH_EXCEPTIONS`
 Boost.python (already available in most boost distributions)
 Recommended (used for I/O): Boost.Numpy https://github.com/ndarray/Boost.NumPy

## Compilation instructions ##

Build using:
```bash
  mkdir build
  cd build
  ccmake ..
  make 
```

## Documentation and Examples ##

The Python API reflects directly the [C++ Halide API](http://halide-lang.org/docs).
Consult the [module documentation](http://googledrive.com/host/0B6LzqcYZJN2cfnZKZno2MmI2TDFsWkh0M3pUOHNnaUdkV2l2dmR2eDlnV2JmeF9NeEI0cTA).

Check out the code for the example applications in the `apps/` and `tutorial/` subdirectory.

You can run them as a batch via:
```bash
    run_apps.sh
    run_tutorial.sh
```

To run these examples, make sure the `PYTHONPATH` environment variable points to your build directory (e.g. `export PYTHONPATH=halide_source/python_bindings/build:$PYTHONPATH`).

## License ##

The Python bindings use the same [MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014), Rodrigo Benenson (2015)
and the Halide open-source community.

## Why did we move from SWIG to Boost.Python ? ##

Problems with (old) SWIG bindings:

- P1) SWIG cannot parse C++15, and it will always lag
- P2) Python code is not compiled/tested together with C++ codebase
- P3) Albeit SWIG is ment to provide "automagic" conversion, current code base has explicit code for every class (and class member) of interest
- P4) Old swig bindings `__init__.py` file has a lot of "python magic" to get things running, code is spread around multiple pieces (`py_util.cpp`, `cHalide.i`, and `__init__.py`)
- P5) Old swig python bindings did not work anymore (swig issues, compilation issues)

The boost python approach helps because:
- a) P1 is solved out of the box, since no C++ parsing is involved
- b) P2 is mitigated since python bindings are also in C++. Any miss-match with generate a compilation error.
- c) P3, P4 and P5 are expected to be handled by having less spaghetti and aiming for tighter integration with rest of code-base (point b)


