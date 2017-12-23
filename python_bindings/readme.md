## PYBIND TODO:

- should Func::realize() just return a Tuple, rather than Realization?
- BoundaryConditions isn't in a sub-name. Should it be?
- We're using [] rather than () for Func access. () should be possible.
- should we define __len__ for everything that implements size()?
- Some API classes missing entirely: e.g., Pipeline, Generators, etc
- Some API classes have many missing-but-needed methods (e.g. Func)
- Some API classes have many deliberately-missing methods (e.g. Argument) because the methods (or field-accessor wrappers) shouldn't be necessary for Python user code (ie, they)
- Should the Buffer<->ndarray conversion routines be standalone, or ctor+instancemethod, or both?
- 'throw' usage mixes C++11 and PyBind types; unify for consistency

## Differences from C++ API

The Python bindings attempt to mimic the Halide C++ API as closely as possible, with some differences where the C++ idiom is either inappropriate or impossible:
- `Halide::Tuple` doesn't exist in the Python bindings; an ordinary Python tuple of `Halide::Expr` is used instead.
- static and instance method overloads with the same name in the same class aren't allowed, so some convenience methods are missing.
- Some implicit conversions in C++ can't be reliably inferred in Python, due to the lack of static typing necessary to correct make the inference. Of particular note:
    - The result of `Func::realize` is a `Halide::Realization`, but is often implicitly converted to a `Halide::Buffer<>`; in Python, there's no obvious way to determing whether the caller wants to keep the `Realization` or extract a `Buffer`, so explicit extraction is needed via `[0]` or similar. TODO: should `realize()` just return a Python tuple and elide `Realization` entirely?

## Prerequisites ##

The bindings (and demonstration applications) should work well both for python2.7 and python3.4 (or higher), and across platforms. (The Makefile defaults to using Python 3.x; to use Python 2, set `PYTHON = python` before building.)

(Currently only tested on Ubuntu 14.04 and OS X 10.10 with python3.4 and a Halide source build).

#### Python requirements:
 See requirements.txt (to be used with `pip`: `pip install --user requirements.txt`)

#### C++ requirements:
- Halide compiled to a distribution (e.g. `make distrib` or similar), with the `HALIDE_DISTRIB_PATH` env var pointing to it
- The PyBind11 package (https://github.com/pybind/pybind11), v2.2.1 or later, with the `PYBIND11_PATH` env var pointing to it


## Compilation instructions ##

Build using:
```bash
  make 
```

## Documentation and Examples ##

The Python API reflects directly the [C++ Halide API](http://halide-lang.org/docs).

Check out the code for the example applications in the `apps/` and `tutorial/` subdirectory.

You can run them as a batch via `make test_apps` or `make test_tutorial`.

To run these examples, make sure the `PYTHONPATH` environment variable points to your build directory (e.g. `export PYTHONPATH=halide_source/python_bindings/build:$PYTHONPATH`).

## License ##

The Python bindings use the same [MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014), Rodrigo Benenson (2015) and the Halide open-source community.
