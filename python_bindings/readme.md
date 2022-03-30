## Differences from C++ API

The Python bindings attempt to mimic the Halide C++ API as closely as possible,
with some differences where the C++ idiom is either inappropriate or impossible:

- Most APIs that take a variadic argumentlist of ints in C++ take an explicit
  list in Python. For instance, the usual version of the `Buffer` ctor in C++
  offers variadic and list versions:
  `Buffer<>(Type t, int extent_dim_0, int extent_dim_1, ...., extent_dim_N, string name = "") Buffer<>(Type t, vector<int> extents, string name = "")`
  in Python, only the second variant is provided.
- `Func` and `Buffer` access is done using `[]` rather than `()`.
  - For zero-dimensional `Func` and `Buffer`, you must explicitly specify `[()]` -- that is, use an empty tuple as the index" --
    as `[]` is not syntactically acceptable in Python.
- Some classes in the Halide API aren't provided because they are 'wrapped' with
  standard Python idioms:
  - `Halide::Tuple` doesn't exist in the Python bindings; an ordinary Python
    tuple of `Halide::Expr` is used instead.
  - `Halide::Realization` doesn't exist in the Python bindings; an ordinary
    Python tuple of `Halide::Buffer` is used instead.
  - `Halide::Error` and friends don't exist; standard Python error handling is
    used instead.
- static and instance method overloads with the same name in the same class
  aren't allowed, so some convenience methods are missing from `Halide::Var`
- Templated types (notably `Halide::Buffer<>` and `Halide::Param<>`) aren't
  provided, for obvious reasons; only the equivalents of `Halide::Buffer<void>`
  and `Halide::Param<void>` are supported.
- Only things in the `Halide` namespace are supported; classes and methods that
  involve using the `Halide::Internal` namespace are not provided.
- The functions in `Halide::ConciseCasts` are present in the toplevel Halide
  module in Python, rather than a submodule: e.g., use `hl.i8_sat()`, not
  `hl.ConciseCasts.i8_sat()`.
- No mechanism is provided for overriding any runtime functions from Python.
- No mechanism is provided for supporting `Func::define_extern`.
- `Buffer::for_each_value()` is hard to implement well in Python; it's omitted
  entirely for now.
- `Func::in` becomes `Func.in_` because `in` is a Python keyword.
- `Func::async` becomes `Func.async_` because `async` is a Python keyword.
- The `not` keyword cannot be used to negate boolean Halide expressions. Instead, the `logical_not` function can be used and is equivalent to using `operator!` in C++.

## Enhancements to the C++ API

- The `Buffer` supports the Python Buffer Protocol
  (https://www.python.org/dev/peps/pep-3118/) and thus is easily and cheaply
  converted to and from other compatible objects (e.g., NumPy's `ndarray`), with
  storage being shared.

## Prerequisites

The bindings (and demonstration applications) should work well for Python 3.4
(or higher), on Linux and OSX platforms. Windows support is experimental, and
available through the CMake build.

#### Python requirements:

The best way to get set up is to use a virtual environment:

```console
$ python3 -m venv venv
$ . venv/bin/activate
$ pip install -r requirements.txt 
```

#### C++ requirements:

- Halide compiled to a distribution (e.g. `make distrib` or similar), with the
  `HALIDE_DISTRIB_PATH` env var pointing to it
- If using CMake, simply set `-DWITH_PYTHON_BINDINGS=ON` from the main build. 

## Compilation instructions

Build using: `make`

## Documentation and Examples

The Python API reflects directly the
[C++ Halide API](http://halide-lang.org/docs).

Check out the code for the example applications in the `apps/` and `tutorial/`
subdirectory.

You can run them as a batch via `make test_apps` or `make test_tutorial`.

To run these examples, make sure the `PYTHONPATH` environment variable points to
your build directory (e.g.
`export PYTHONPATH=halide_source/python_bindings/bin:$PYTHONPATH`).

## License

The Python bindings use the same
[MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as
Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014),
Rodrigo Benenson (2015) and the Halide open-source community.
