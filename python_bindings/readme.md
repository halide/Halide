## Differences from C++ API

The Python bindings attempt to mimic the Halide C++ API as closely as possible, with some differences where the C++ idiom is either inappropriate or impossible:

- `Func` and `Buffer` access is done using `[]` rather than `()`
- Some classes in the Halide API aren't provided because they are 'wrapped' with standard Python idioms:
    - `Halide::Tuple` doesn't exist in the Python bindings; an ordinary Python tuple of `Halide::Expr` is used instead.
    - `Halide::Realization` doesn't exist in the Python bindings; an ordinary Python tuple of `Halide::Buffer` is used instead.
    - `Halide::Error` and friends don't exist; standard Python error handling is used instead.
- static and instance method overloads with the same name in the same class aren't allowed, so some convenience methods are missing from `Halide::Var`
- Templated types (notably `Halide::Buffer<>` and `Halide::Param<>`) aren't provided, for obvious reasons; only the equivalents of `Halide::Buffer<void>` and `Halide::Param<void>` are supported.
- Only things in the `Halide` namespace are supported; classes and methods that involve using the `Halide::Internal` namespace are not provided.
- No mechanism is provided for overriding any runtime functions from Python.
- No mechanism is provided for supporting `Func::define_extern`.

## Enhancements to the C++ API

- A `Buffer` object can be constructed from most array-like objects, and will share the underlying storage with the object in question. This is especially useful with `numpy.ndarray`.
- Similarly, most array-like objects can be created from a `Buffer`, with underlying storage being shared.

## Prerequisites ##

The bindings (and demonstration applications) should work well both for python2.7 and python3.4 (or higher), on Linux and OSX platforms. Windows is not yet supported, but could be with CMake work. (The Makefile defaults to using Python 3.x; to use Python 2, set `PYTHON = python` before building.)


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

To run these examples, make sure the `PYTHONPATH` environment variable points to your build directory (e.g. `export PYTHONPATH=halide_source/python_bindings/bin:$PYTHONPATH`).

## License ##

The Python bindings use the same [MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014), Rodrigo Benenson (2015) and the Halide open-source community.
