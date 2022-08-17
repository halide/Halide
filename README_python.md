# Halide Bindings for Python

Halide provides Python bindings for most of its public API. Only Python 3.x is
supported; at this time, Python 3.8 (or higher) is recommended. The Python
bindings are supported on 64-bit Linux, OSX, and Windows systems.

In addition to the ability to write just-in-time Halide code using Python, you
can write Generators using the Python bindings, which can simplify build-system
integration (since no C++ metacompilation step is required).

You can also use existing Halide Generators (written in C++) to produce Python
extensions that can be used within Python code.

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

> **TODO:** do we want to document `pip install
> /path/to/Halide/python_bindings/`?

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

from the Halide build directory.

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

    In Python, only the second variant is provided.

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
    arguments to JIT-compiled functions, which can now be supported more simply
    and elegantly via `compile_to_callable()`. (It is likely that `ParamMap`
    will be removed from the C++ bindings in a future version of Halide as
    well.)

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

## Halide Generators In Python

In Halide, a "Generator" is a unit of encapsulation for Halide code. It is
self-contained piece of code that can: - Produce a chunk of Halide IR (in the
form of an `hl.Pipeline`) that is appropriate for compilation (via either JIT or
AOT) - Expose itself to the build system in a discoverable way - Fully describe
itself for the build system with metadata for (at least) the type and number of
inputs and outputs expected - Allow for build-time customization of
coder-specified parameters in a way that doesn't require editing of source code

Originally, Halide only supported writing Generators in C++. In this document,
we'll use the term "C++ Generator" to mean "Generator written in C++ using the
classic API", the term "Python Generator" to mean "Generator written in Halide's
Python bindings", and just plain "Generator" when the discussion is relatively
neutral with respect to the implementation language/API.

### Writing a Generator in Python

A Python Generator is a class that: - has the `@hl.generator` decorator applied
to it - declares zero or more member fields that are initialized with values of
`hl.InputBuffer` or `hl.InputScalar`, which specify the expected input(s) of the
resulting `Pipeline`. - declares one or more member fields that are initialized
with values of `hl.OutputBuffer` or `hl.OutputScalar`, which specify the
expected output(s) of the resulting `Pipeline`. - declares zero or more member
fields that are initialized with values of `hl.GeneratorParam`, which can be
used to pass arbitrary information from the build system to the Generator. A
GeneratorParam can carry a value of type `bool`, `int`, `float`, `str`, or
`hl.Type`. - declares a `generate()` method that fill in the Halide IR needed to
define all of the Outputs - optionally declares a `configure()` method to
dynamically add Inputs or Outputs to the pipeline, based on (e.g.) the values of
`GeneratorParam` values or other external inputs

Let's look at a fairly simple example:

> **TODO:** this example is pretty contrived; is there an equally simple
> Generator to use here that would demonstrate the basics?

```
import halide as hl

x = hl.Var('x')
y = hl.Var('y')

# Apply a mask value to a 2D image using a logical operator that is selected at compile-time.
@hl.generator(name = "logical_op_generator")
class LogicalOpGenerator:
    op = hl.GeneratorParam("xor")

    input = hl.InputBuffer(hl.UInt(8), 2)
    mask = hl.InputScalar(hl.UInt(8))

    output = hl.OutputBuffer(hl.UInt(8), 2)

    def generate(g):
        # Algorithm
        if g.op == "xor":
            g.output[x, y] = g.input[x, y] ^ g.mask
        elif g.op == "and":
            g.output[x, y] = g.input[x, y] & g.mask
        else:
            assert False, "'%s' is not an understood op" % g.op

        # Schedule
        v = g.natural_vector_size(hl.UInt(8))
        g.output.vectorize(x, v)

if __name__ == "__main__":
    hl.main()
```

If you've worked with Halide Generators written in C++, the "shape" of this will
likely look familiar. (If not, no worries; you shouldn't need any knowledge of
C++ Generators for the following to make sense.)

Let's take the details here one at a time.

#### @hl.generator("name")

This decorator adds appropriate "glue" machinery to the class to enforce various
invariants. It also serves as the declares a "registered name" for the
Generator, which is a unique name that the build system will use to identify the
Generator. If you omit the name, it defaults to defaults to `module.classname`;
if module is `__main__` then we omit it and just use the plain classname. Note
that the registered name need not match the classname. (Inside Halide, we use
the convention of `CamelCase` for class names and `snake_case` for registered
names, but you can use whatever convention you like.)

#### hl.GeneratorParam

Each `GeneratorParam` is an arbitrary key-value pair that can be used to provide
configurable options at compile time. You provide the name and a default value.
The default value can be overridden by the build machinery, which will replace
the value (based on user specified text).

Note that the type of the default value *is* used to define the expected type of
the `GeneratorParam`, and trying to set it to an incompatible value will throw
an exception. The types that are acceptable to use in a `GeneratorParam` are:

-   Python's `bool`, `int`, `float`, or `str`
-   Halide's `hl.Type`
-   ...that's all

Note that the value of a `GeneratorParam` is read-only from the point of view of
the Generator; they are set at Generator construction time and attempting to
change their value will throw an exception.

#### hl.InputBuffer, hl.InputScalar

These declare the inputs to the `hl.Pipeline` that the Generator will produce.
An `hl.InputScalar` is, essentially, a "factory" that produces an `hl.Param` in
the existing Python API, while an `hl.InputBuffer` is a factory for
`hl.ImageParam`.

From the Generator author's perspective, a field initialized with `InputScalar`
**is** a `Param` – not kinda-like-one, not a magic wrapper that forwards
everything; it is literally just `hl.Param`. Similarly, an `InputBuffer`
produces `ImageParam`, and an `InputFunc` is a wrapper around `Func`. You won't
be able to assign a new value to the member field for Inputs – as with
GeneratorParams, they are "read-only" to the Generator – but you will be able to
set constraints on them.

Note that in addition to specifying a concrete type and dimensionality for the
inputs, these factory classes support the ability to specify either (or both)
`None`, which means the type/dimensionality will be provided by GeneratorParams
in the build system.

#### hl.OutputBuffer, hl.OutputScalar

These declare the output(s) of the Pipeline that the Generator will produce. An
`hl.OutputBuffer` is, essentially, a "factory" that produces an `hl.Func` in the
existing Python API. (`hl.OutputScalar` is just an `hl.OutputBuffer` that always
has zero dimensions.)

From the Generator author's perspective, a field declared with `OutputBuffer`
**is** a `Func` – not kinda-like-one, not a magic wrapper that forwards
everything; it is literally just `hl.Func` (with type-and-dimensionality set to
match, see recent PR https://github.com/halide/Halide/pull/6734) . You won't be
able to assign a new value to the member field for Inputs – as with
GeneratorParams, they are "read-only" to the Generator – but you will be able to
set constraints on them.

Note that in addition to specifying a concrete type and dimensionality for the
inputs, these factory classes support the ability to specify either (or both) as
`None`, which means the type/dimensionality will be provided by GeneratorParams
in the build system.

#### Names

Note that all of the GeneratorParams, Inputs, and Outputs have names that are
implicitly filled in based on the fieldname of their initial assignment; unlike
in C++ Generators, there isn't a way to "override" this name (i.e., the name in
the IR will always exactly match the Python field name). Names have the same
constraints as for C++ Generators (essentially, a C identifier, but without an
initial underscore, and without any double underscore anywhere).

#### generate() method

This will be called by the Generator machinery to build the Pipeline. As with
C++ Generators, the only required task is to ensure that all Output fields are
fully defined, in a way that matches the type-and-dimension constraints
specified.

It is required that the `generate()` method be defined by the Generator.

(Note that, by convention, Halide Generators use `g` instead of `self` in their
`generate()` method to make the expression language terser; this is not in any
way required, but is recommended.)

### Using a Generator with JIT compilation

You can use the `compile_to_callable()` method to JIT-compile a Generator into
a `hl.Callable`, which is (essentially) just a dynamically-created function.

```
import LogicalOpGenerator
import imageio
import numpy as np

# Instantiate a Generator, set the GeneratorParams, then compile it
xor_op_generator = LogicalOpGenerator()
xor_op_generator.set_generator_params({"op": "xor"})
xor_filter = xor_op_generator.compile_to_callable()

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.dtype == np.uint8

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
xor_filter(input_buf, 0x7f, output_buf)

# Note also that we can use named arguments for any/all, in the Python manner:
xor_filter(mask=0x7f, input=input_buf, output=output_buf)

imageio.imsave("/tmp/xored.png", output_buf)
```

By default, a Generator will produce code targeted at `Target("host")` (or the value of the `HL_JIT_TARGET` environment variable, if set); you can override this behavior selectively by activating a `GeneratorContext` when the Generator is *created*:

```
import LogicalOpGenerator

# Maybe we always want to compile for plain x86-64, with no advanced SIMD enabled?
t = hl.Target("x86-64-linux")
with hl.GeneratorContext(t):
    xor_op_generator = LogicalOpGenerator()
    xor_op_generator.set_generator_params({"op": "xor"})
    xor_filter = xor_op_generator.compile_to_callable()
```

### Using a Generator with AOT compilation



```
import LogicalOpGenerator
import imageio
import numpy as np

# Instantiate a Generator, set the GeneratorParams, then compile it
xor_op_generator = LogicalOpGenerator()
xor_op_generator.set_generator_params({"op": "xor"})
xor_filter = xor_op_generator.compile_to_callable()

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.dtype == np.uint8

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
xor_filter(input_buf, 0x7f, output_buf)

# Note also that we can use named arguments for any/all, in the Python manner:
xor_filter(mask=0x7f, input=input_buf, output=output_buf)

imageio.imsave("/tmp/xored.png", output_buf)
```


### Compiling a C++ Generator for use with Python

Let's look at the C++ equivalent of the `XorFilter` Generator we saw earlier:

```
class LogicalOpFilter : public Halide::Generator<LogicalOpFilter> {
public:
    GeneratorParam<std::string> op{"op", "xor"};

    Input<Buffer<uint8_t, 2>>   input{"input"};
    Input<uint8_t>              mask{"mask"};

    Output<Buffer<uint8_t, 2>>  output{"output"};

    void generate() {
        # Algorithm
        if (op == "xor") {
            output(x, y) = input(x, y) ^ mask;
        } else if (op == "and") {
            output(x, y) = input(x, y) & mask;
        } else {
            std::cerr << op << " is not a supported op\n";
            abort();
        }

        # Schedule
        int v = natural_vector_size<uint8_t>();
        output.vectorize(x, v);
    }
};
HALIDE_REGISTER_GENERATOR(LogicalOpFilter, logical_op_generator)
```

If you are using CMake, the simplest thing is to use
`add_python_aot_extension()`, defined in PythonExtensionHelpers.cmake:

```
add_python_aot_extension(xor_filter
                         GENERATOR logical_op_generator
                         SOURCES logical_op_generator.cpp
                         PARAMS op=xor
                         [ FEATURES ... ])
```

This compiles the Generator code in `logical_op_generator.cpp` with the
registered name `logical_op_generator` to produce the target `xor_filter`, which is
a Python Extension in the form of a shared library (e.g.,
`xor_filter.cpython-310-x86_64-linux-gnu.so`). Note that we explicitly specify
the value of the `op` GeneratorParam, even though we don't need to, since `xor`
is the default value. (We could of course produce an `and_filter` by just adding
another build rule like the one above, but with `op=and`.)

> **TODO:** add skeletal example of how to drive the Generator directly

### Calling Generator-Produced code from Python

As long as the shared library is in `PYTHONPATH`, it can be imported and used
directly. For the example above:

```
from xor_filter import xor_filter
import imageio
import numpy as np

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.dtype == np.uint8

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
xor_filter(input_buf, 0xff, output_buf)

# Note also that we can use named arguments for any/all, in the Python manner:
# xor_filter(input=input_buf, mask=0xff, output=output_buf)

imageio.imsave("/tmp/xored.png", output_buf)
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

### Advanced Generator-Related Topics

#### The Lifecycle Of A Generator

Whether being driven by a build system (for AOT use) or by another piece of
Python code (typically for JIT use), the lifecycle of a Generator looks
something like this:

-   An instance of the Generator in question is created. It uses the
    currently-active `GeneratorContext` (which contains the `Target` to be used
    for code generation), which is stored in a thread-local stack.
-   Some (or all) of the default values of the `GeneratorParam` members may be
    replaced based on (e.g.) command-line arguments in the build system
-   All `GeneratorParam` members are made immutable.
-   The `configure()` method is called, allowing the Generator to use
    `add_input()` or `add_output()` to dynamically add inputs and/or outputs.
-   If any `Input` or `Output` members were defined with unspecified type or
    dimensions (e.g. `some_input = hl.InputBuffer(None, 3)`), those types and
    dimensions are filled in from `GeneratorParam` values (e.g.
    `some_input.type` in this case). If any types or dimensions are left
    unspecified after this step, an exception will be thrown.
-   If the Generator is being invoked via its `call()` method (see below), the
    default values for `Inputs` will be replaced by the values from the argument
    list.
-   The Generator instance has its `generate()` method called.
-   The calling code will extract the values of all `Output` values and validate
    that they match the type, dimensions, etc of the declarations.
-   The calling code will then either call `compile_to_file()` and friends (for
    AOT use), or return the output values to the caller (for JIT use).
-   Finally, the Generator instance will be discarded, never to be used again.

Note that almost all of the code doing the hand-wavy bits above is injected by
the `@hl.generator` decorator – the Generator author
doesn't need to know or care about the specific details, only that they happen.

All Halide Generators are **single-use** instances – that is, any given Generator instance should be used at most once. If
a Generator is to be executed multiple times (e.g. for different
`GeneratorParam` values, or a different `Target`), a new one must be constructed
each time.

#### Calling a Generator Directly

Each Generator has a class method (injected by `@hl.generator`) that allows you to "call" the Generator like an ordinary function; this allows you to directly take the Halide IR produced by the Generator and do anything you want to with it. This can be especially useful when writing library code, as you can 'compose' more complex pipelines this way.

This method is named `call()` and looks like this:


```
@classmethod
def call(cls, *args, **kwargs):
    ...
```


It takes the inputs (specified either by-name or by-position in the usual Python way). It also allows for an optional by-name-only argument, `generator_params`, which is a simple Python dict that allows for overriding `GeneratorParam`s. It returns a tuple of the Output values. For the earlier example, usage might be something like:


```
import LogicalOpFilter

x, y = hl.Var(), hl.Var()

input_buf = hl.Buffer(hl.UInt(8), [2, 2])
mask_value = 0x7f

# Inputs by-position
func_out = LogicalOpFilter.call(input_buf, mask_value)

# Inputs by-name
func_out = LogicalOpFilter.call(mask=mask_value, input=input_buf)

# Above again, but with generator_params
func_out = LogicalOpFilter.call(input_buf, mask_value,
                                generator_params = {"op": "and"})
func_out = LogicalOpFilter.call(generator_params = {"op": and},
                                input=input_buf, mask=mask_value)
```

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
