# Halide Bindings for Python

Halide provides Python bindings for most of its public API. Only Python 3.x is
supported; at this time, Python 3.8 (or higher) is recommended. The Python
bindings are supported on 64-bit Linux, OSX, and Windows systems.

In addition to the ability to write just-in-time Halide code using Python, you
can also use existing Halide Generators (written in C++) to produce Python
extensions that can be used within Python code.

(Note that the ability to write Halide Generators in Python is *not* supported
at present, but is expected to be supported soon.)

## Python Requirements:

Before building, you should ensure you have prerequite packages installed in
your local Python environment. The best way to get set up is to use a virtual
environment:

```console
$ python3 -m venv venv
$ . venv/bin/activate
$ python3 -m pip install -U setuptools wheel
$ python3 -m pip install -r python_bindings/requirements.txt
```

## Compilation Instructions

Build as part of the CMake build with `-DWITH_PYTHON_BINDINGS=ON` (this is the
default). Note that this requires both Halide and LLVM to be built with RTTI and
exceptions **enabled**, which is not the default for LLVM.

## Documentation and Examples

As mentioned elsewhere, the Python API attempts to mimic the
[C++ Halide API](http://halide-lang.org/docs) as directly as possible; there
isn't separate Python-specific documentation for the API at this time.

For now, examine the code for the example applications in the `test/apps/` and
`tutorial/` subdirectories.

The tests run as part of the standard CTest infrastructure and are labeled with
the `python` label. You can run the Python tests specifically by running:

```
$ ctest -L python
```

From the Halide build directory.

## Differences from C++ API

The Python bindings attempt to mimic the Halide C++ API as closely as possible,
with some differences where the C++ idiom is either inappropriate or impossible:

-   Most APIs that take a variadic argumentlist of ints in C++ take an explicit
    list in Python. For instance, the usual version of the `Buffer` ctor in C++
    offers both variadic and list versions:

    ```
    Buffer<>(Type t, int extent_dim_0, int extent_dim_1, ...., extent_dim_N, string name = "");
    Buffer<>(Type t, vector<int> extents, string name = "");
    ```

    in Python, only the second variant is provided.

-   `Func` and `Buffer` access is done using `[]` rather than `()`

    -   For zero-dimensional `Func` and `Buffer`, you must explicitly specify
        `[()]` -- that is, use an empty tuple as the index -- because `[]` is
        not syntactically acceptable in Python.

-   Some classes in the Halide API aren't provided because standard Python
    idioms are a better fit:

    -   `Halide::Tuple` doesn't exist in the Python bindings; an ordinary Python
        tuple of `Halide::Expr` is used instead.
    -   `Halide::Realization` doesn't exist in the Python bindings; an ordinary
        Python tuple of `Halide::Buffer` is used instead.

-   static and instance method overloads with the same name in the same class
    aren't allowed, so some convenience methods are missing from `Halide::Var`

-   Templated types (notably `Halide::Buffer<>` and `Halide::Param<>`) aren't
    provided, for obvious reasons; only the equivalents of
    `Halide::Buffer<void>` and `Halide::Param<void>` are supported.

-   The functions in `Halide::ConciseCasts` are present in the toplevel Halide
    module in Python, rather than a submodule: e.g., use `halide.i8_sat()`, not
    `halide.ConciseCasts.i8_sat()`.

-   Only things in the `Halide` namespace are supported; classes and methods
    that involve using the `Halide::Internal` namespace are not provided.

-   No mechanism is provided for overriding any runtime functions from Python.

-   No mechanism is provided for supporting `Func::define_extern`.

-   `Buffer::for_each_value()` isn't supported yet.

-   `Func::in` becomes `Func.in_` because `in` is a Python keyword.

-   `Func::async` becomes `Func.async_` because `async` is a Python keyword.

-   `ParamMap` isn't supported as an argument to any `Func` or `Pipeline`
    method, and never will be: it exists as a way to support thread-safe
    arguments to JIT-compiled functions, which can now be supported more
    simply and elegantly via `compile_to_callable()`. (It is likely that
    `ParamMap` will be removed from the C++ bindings in a future version
    of Halide as well.)

-   The `not` keyword cannot be used to negate boolean Halide expressions.
    Instead, the `logical_not` function can be used and is equivalent to using
    `operator!` in C++.

-   There is no way to override the logical `and`/`or` operators in Python to
    work with `Expr`: you must use the bitwise `|` and `&` instead. (Note that
    incorrectly using using `and`/`or` just short-circuits weirdly, rather than
    failing with some helpful error; this is an issue that we have not yet found
    any way to improve, unfortunately.)

-   Some error messages need to be made more informative.

-   Some exceptions are the "incorrect" type (compared to C++ expectations).

-   Many hooks to override runtime functions (e.g. Func::set_error_handler)
    aren't yet implemented.

-   The following parts of the Halide public API are currently missing entirely
    from the Python bindings (but are all likely to be supported at some point
    in the future):

    -   `DeviceInterface`
    -   `evaluate()`

## Example of Simple Usage

The Python bindings for Halide are built as a standard part of the `install`
target, and are present in the Halide install location at
`$HALIDE_INSTALL/lib/python3/site-packages`; adding that to your `PYTHONPATH`
should allow you to simply `import halide`:

```python
# By convention, we import halide as 'hl' for terseness
import halide as hl

# Some constants
edge = 512
k = 20.0 / float(edge)

# Simple formula
x, y, c = hl.Var('x'), hl.Var('y'), hl.Var('c')
f = hl.Func('f')
e = hl.sin(x * ((c + 1) / 3.0) * k) * hl.cos(y * ((c + 1) / 3.0) * k)
f[x, y, c] = hl.cast(hl.UInt(8), e * 255.0)
f.vectorize(x, 8).parallel(y)

# Realize into a Buffer.
buf = f.realize([edge, edge, 3])

# Do something with the image. We'll just save it to a PNG.
import imageio
imageio.imsave("/tmp/example.png", buf)
```

It's worth noting in the example above that the Halide `Buffer` object supports
the Python Buffer Protocol (https://www.python.org/dev/peps/pep-3118) and thus
is converted to and from other compatible objects (e.g., NumPy's `ndarray`), at
essentially zero cost, with storage being shared. Thus, we can usually pass it
directly to existing Python APIs (like `imsave()`) that expect 'image-like'
objects without any explicit conversion necessary.

## Using Halide Generators from Python

### Compiling a C++ Generator for Python

Let's say we have a very simple Generator:

```
class MyFilter : public Halide::Generator<Simple> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Input<uint8_t> mask{"mask"};
    Output<Buffer<uint8_t, 2>> output{"output"};

    void generate() {
        output(x, y) = input(x, y) & mask;
        output.vectorize(x, 8).compute_root();
    }
};
HALIDE_REGISTER_GENERATOR(MyFilter, my_filter)
```

If you are using CMake, the simplest thing is to use
`add_python_aot_extension()`, defined in PythonExtensionHelpers.cmake:

```
add_python_aot_extension(my_filter
                         GENERATOR my_filter_generator
                         SOURCES my_filter_generator.cpp
                         [ FEATURES ... ]
                         [ PARAMS ... ])
```

This compiles the Generator code in `my_filter_generator.cpp` with the
registered name `my_filter` to produce the target `my_filter`, which is a Python
Extension in the form of a shared library (e.g.,
`foo.cpython-310-x86_64-linux-gnu.so`).

### Calling a C++ Generator from Python

As long as the shared library is in `PYTHONPATH`, it can be imported and used
directly. For the example above:

```
from my_filter import my_filter
import imageio
import numpy as np

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.dtype == np.uint8

# arbitrary
mask = 0x7f

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
my_filter(input_buf, mask, output_buf)

imageio.imsave("/tmp/masked_file.png", output_buf)
```

Above, we're using common Python utilities (`numpy`, `imageio`) to construct the
input/output buffers we want to pass to Halide.

**Note**: Getting the memory order correct can be a little confusing for numpy.
By default numpy uses "C-style"
[row-major](https://docs.scipy.org/doc/numpy-1.13.0/reference/internals.html)
order, which sounds like the right option for Halide; however, this nomenclature
assumes the matrix-math convention of ordering axes as `[rows, cols]`, whereas
Halide (and imaging code in general) generally assumes `[x, y]` (i.e., `[cols,
rows]`). Thus what you usually want in Halide is column-major ordering. This
means numpy arrays, by default, come with the wrong memory layout for Halide.
But if you construct the numpy arrays yourself (like above), you can pass
`order='F'` to make numpy use the Halide-compatible memory layout. If you're
passing in an array constructed somewhere else, the easiest thing to do is to
`.transpose()` it before passing it to your Halide code.)

## Keeping Up To Date

If you use the Halide Bindings for Python inside Google, you are *strongly*
encouraged to
[subscribe to announcements for new releases of Halide](https://github.blog/changelog/2018-11-27-watch-releases/),
as it is likely that enhancements and tweaks to our Python support will be made
in future releases.

## License

The Python bindings use the same
[MIT license](https://github.com/halide/Halide/blob/master/LICENSE.txt) as
Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014),
Rodrigo Benenson (2015) and the Halide open-source community.
