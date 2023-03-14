# Halide Bindings for Python

<!-- MarkdownTOC autolink="true" -->

- [Python Requirements](#python-requirements)
- [Compilation Instructions](#compilation-instructions)
- [Documentation and Examples](#documentation-and-examples)
- [Differences from C++ API](#differences-from-c-api)
- [Example of Simple Usage](#example-of-simple-usage)
- [Halide Generators In Python](#halide-generators-in-python)
    - [Writing a Generator in Python](#writing-a-generator-in-python)
        - [@hl.generator\("name"\)](#hlgeneratorname)
        - [hl.GeneratorParam](#hlgeneratorparam)
        - [hl.InputBuffer, hl.InputScalar](#hlinputbuffer-hlinputscalar)
        - [hl.OutputBuffer, hl.OutputScalar](#hloutputbuffer-hloutputscalar)
        - [Names](#names)
        - [generate\(\) method](#generate-method)
        - [Types for Inputs and Outputs](#types-for-inputs-and-outputs)
    - [Using a Generator for JIT compilation](#using-a-generator-for-jit-compilation)
    - [Using a Generator for AOT compilation](#using-a-generator-for-aot-compilation)
    - [Calling Generator-Produced code from Python](#calling-generator-produced-code-from-python)
    - [Advanced Generator-Related Topics](#advanced-generator-related-topics)
        - [Generator Aliases](#generator-aliases)
        - [Dynamic Inputs and Outputs](#dynamic-inputs-and-outputs)
        - [Calling a Generator Directly](#calling-a-generator-directly)
        - [The Lifecycle Of A Generator](#the-lifecycle-of-a-generator)
        - [Notable Differences Between C++ and Python Generators](#notable-differences-between-c-and-python-generators)
- [Keeping Up To Date](#keeping-up-to-date)
- [License](#license)

<!-- /MarkdownTOC -->

Halide provides Python bindings for most of its public API. Python 3.8 (or
higher) is required. The Python bindings are supported on 64-bit Linux, OSX,
and Windows systems.

In addition to the ability to write just-in-time Halide code using Python, you
can write [Generators](#halide-generators-in-python) using the Python bindings,
which can simplify build-system integration (since no C++ metacompilation step
is required).

You can also use existing Halide Generators (written in either C++ or Python)
to produce Python extensions that can be used within Python code.

## Python Requirements

Before building, you should ensure you have prerequite packages installed in
your local Python environment. The best way to get set up is to use a virtual
environment:

```console
$ python3 -m venv venv
$ . venv/bin/activate
$ python3 -m pip install -U setuptools wheel
$ python3 -m pip install -r requirements.txt
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

from the Halide build directory.

## Differences from C++ API

The Python bindings attempt to mimic the Halide C++ API as closely as possible,
with some differences where the C++ idiom is either inappropriate or impossible:

-   Most APIs that take a variadic argument list of ints in C++ take an explicit
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

-   No mechanism is provided for overriding any runtime functions from Python
    for JIT-compiled code. (Runtime functions for AOT-compiled code can be
    overridden by building and linking a custom runtime, but not currently
    via any runtime API, e.g. halide_set_custom_print() does not exist.)

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

```
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
from halide import imageio
imageio.imwrite("/tmp/example.png", buf)
```

It's worth noting in the example above that the Halide `Buffer` object supports
the Python Buffer Protocol (https://www.python.org/dev/peps/pep-3118) and thus
is converted to and from other compatible objects (e.g., NumPy's `ndarray`), at
essentially zero cost, with storage being shared. Thus, we can usually pass it
directly to existing Python APIs (like `imsave()`) that expect 'image-like'
objects without any explicit conversion necessary.

## Halide Generators In Python

In Halide, a "Generator" is a unit of encapsulation for Halide code. It is a
self-contained piece of code that can:

-   Produce a chunk of Halide IR (in the form of an `hl.Pipeline`) that is
    appropriate for compilation (via either JIT or AOT)
-   Expose itself to the build system in a discoverable way
-   Fully describe itself for the build system with metadata for (at least) the
    type and number of inputs and outputs expected
-   Allow for build-time customization of coder-specified parameters in a way
    that doesn't require editing of source code

Originally, Halide only supported writing Generators in C++. In this document,
we'll use the term "C++ Generator" to mean "Generator written in C++ using the
classic API", the term "Python Generator" to mean "Generator written in Halide's
Python bindings", and just plain "Generator" when the discussion is relatively
neutral with respect to the implementation language/API.

### Writing a Generator in Python

A Python Generator is a class that:

-   has the `@hl.generator` decorator applied to it
-   declares zero or more member fields that are initialized with values of
    `hl.InputBuffer` or `hl.InputScalar`, which specify the expected input(s) of
    the resulting `Pipeline`.
-   declares one or more member fields that are initialized with values of
    `hl.OutputBuffer` or `hl.OutputScalar`, which specify the expected output(s)
    of the resulting `Pipeline`.
-   declares zero or more member fields that are initialized with values of
    `hl.GeneratorParam`, which can be used to pass arbitrary information from
    the build system to the Generator. A GeneratorParam can carry a value of
    type `bool`, `int`, `float`, `str`, or `hl.Type`.
-   declares a `generate()` method that fill in the Halide IR needed to define
    all of the Outputs
-   optionally declares a `configure()` method to dynamically add Inputs or
    Outputs to the pipeline, based on (e.g.) the values of `GeneratorParam`
    values or other external inputs

Let's look at a fairly simple example:

> **TODO:** this example is pretty contrived; is there an equally simple
> Generator to use here that would demonstrate the basics?

```
import halide as hl

x = hl.Var('x')
y = hl.Var('y')

_operators = {
  'xor': lambda a, b: a ^ b,
  'and': lambda a, b: a & b,
  'or':  lambda a, b: a | b
}

# Apply a mask value to a 2D image using a logical operator that is selected at compile-time.
@hl.generator(name = "logical_op_generator")
class LogicalOpGenerator:
    op = hl.GeneratorParam("xor")

    input = hl.InputBuffer(hl.UInt(8), 2)
    mask = hl.InputScalar(hl.UInt(8))

    output = hl.OutputBuffer(hl.UInt(8), 2)

    def generate(g):
        # Algorithm
        operator = _operators[g.op]
        g.output[x, y] = operator(g.input[x, y], g.mask)

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
way required, but is recommended to improve readability.)

#### Types for Inputs and Outputs

For all of the Input and Output fields of Generators, you can specify native
Python types (instead of `hl.Type`) for certain cases that are unambiguous. At
present, we allow `bool` as an alias for `hl.Bool()`, `int` as an alias for
`hl.Int(32)`, and `float` as an alias for `hl.Float(32)`.

### Using a Generator for JIT compilation

You can use the `compile_to_callable()` method to JIT-compile a Generator into a
`hl.Callable`, which is (essentially) just a dynamically-created function.

```
import LogicalOpGenerator
from halide import imageio
import numpy as np

# Instantiate a Generator -- we can only set the GeneratorParams
# by passing in a dict to the Generator's constructor
or_op_generator = LogicalOpGenerator({"op": "or"})

# Now compile the Generator into a Callable
or_filter = or_op_generator.compile_to_callable()

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.ndim == 2
assert input_buf.dtype == np.uint8

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
or_filter(input_buf, 0x7f, output_buf)

# Note also that we can use named arguments for any/all, in the Python manner:
or_filter(mask=0x7f, input=input_buf, output=output_buf)

imageio.imwrite("/tmp/or.png", output_buf)
```

By default, a Generator will produce code targeted at `Target("host")` (or the
value of the `HL_JIT_TARGET` environment variable, if set); you can override
this behavior selectively by activating a `GeneratorContext` when the Generator
is *created*:

```
import LogicalOpGenerator

# Compile with debugging enabled
t = hl.Target("host-debug")
with hl.GeneratorContext(t):
    or_op_generator = LogicalOpGenerator({"op": "or"})
    or_filter = or_op_generator.compile_to_callable()
```

### Using a Generator for AOT compilation

If you are using CMake, the simplest thing is to use
`add_halide_library` and `add_halide_python_extension_library()`:

```
# Build a Halide library as you usually would, but be sure to include `PYTHON_EXTENSION`
add_halide_library(xor_filter
                   FROM logical_op_generator
                   PARAMS op=xor
                   PYTHON_EXTENSION output_path_var
                   [ FEATURES ... ]
                   [ PARAMS ... ])

# Now wrap the generated code with a Python extension.
# (Note that module name defaults to match the target name; we only
# need to specify MODULE_NAME if we need a name that may differ)
add_halide_python_extension_library(my_extension
                                    MODULE_NAME my_module
                                    HALIDE_LIBRARIES xor_filter)
```

(Note that this rule works for both C++ and Python Generators.)

This compiles the Generator code in `logical_op_generator.py` with the
registered name `logical_op_generator` to produce the target `xor_filter`, and then wraps
the compiled output with a Python extension. The result will be a shared library of the form
`<target>.<soabi>.so`, where <soabi> describes the specific Python version and
platform (e.g., `cpython-310-darwin` for Python 3.10 on OSX.)

Note that you can combine multiple Halide libraries into a single Python module;
this is convenient for packagaing, but also because all the libraries in a single
extension module share the same Halide runtime (and thus, the same caches, thread pools, etc.).

```
add_halide_library(xor_filter ...)
add_halide_library(and_filter ...)
add_halide_library(or_filter ...)

add_halide_python_extension_library(my_extension
                                    MODULE_NAME my_module
                                    HALIDE_LIBRARIES xor_filter and_filter or_filter)
```

Note that you must take care to ensure that all of the `add_halide_library` targets
specified use the same Halide runtime; it may be necessary to use `add_halide_runtime`
to define an explicit runtime that is shared by all of the targets:

```
add_halide_runtime(my_runtime)

add_halide_library(xor_filter USE_RUNTIME my_runtime ...)
add_halide_library(and_filter USE_RUNTIME my_runtime ...)
add_halide_library(or_filter USE_RUNTIME my_runtime ...)

add_halide_python_extension_library(my_extension
                                    MODULE_NAME my_module
                                    HALIDE_LIBRARIES xor_filter and_filter or_filter)
```

If you're not using CMake, you can "drive" a Generator directly from your build
system via command-line flags. The most common, minimal set looks something like
this:

```
python3 /path/to/my/generator.py -g <registered-name> \
                                 -o <output-dir> \
                                 target=<halide-target-string> \
                                 [generator-param=value ...]
```

The argument to `-g` is the name supplied to the `@hl.generator` decorator. The
argument to -o is a directory to use for the output files; by default, we'll
produce a static library containing the object code, and a C++ header file with
a forward declaration. `target` specifies a Halide `Target` string decribing the
OS, architecture, features, etc that should be used for compilation. Any other
arguments to the command line that don't begin with `-` are presumed to name
`GeneratorParam` values to set.

There are other flags and options too, of course; use `python3
/path/to/my/generator.py -help` to see a list with explanations.

(Unfortunately, there isn't (yet) a way to produce a Python Extension just by
running a Generator; the logic for `add_halide_python_extension_library` is currently all
in the CMake helper files.)

### Calling Generator-Produced code from Python

As long as the shared library is in `PYTHONPATH`, it can be imported and used
directly. For the example above:

```
from my_module import xor_filter
from halide import imageio
import numpy as np

# Read in some file for input
input_buf = imageio.imread("/path/to/some/file.png")
assert input_buf.ndim == 2
assert input_buf.dtype == np.uint8

# create a Buffer-compatible object for the output; we'll use np.array
output_buf = np.empty(input_buf.shape, dtype=input_buf.dtype)

# Note, Python code throws exception for error conditions rather than returning an int
xor_filter(input_buf, 0xff, output_buf)

# Note also that we can use named arguments for any/all, in the Python manner:
# xor_filter(input=input_buf, mask=0xff, output=output_buf)

imageio.imwrite("/tmp/xored.png", output_buf)
```

Above, we're using common Python utilities (`numpy`) to construct the
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

#### Generator Aliases

A Generator alias is a way to associate a Generator with one (or more) specific
sets of GeneratorParams; the 'alias' is just another registered name. This
offers a convenient alternative to specifying multiple sets of GeneratorParams
via the build system. To define alias(es) for a Generator, just add the
`@hl.alias` decorator before `@hl.generator` decorator:

```
@hl.alias(
    xor_generator={"op": "xor"},
    and_generator={"op": "and"},
    or_generator={"op": "or"}
)
@hl.generator("logical_op_generator")
class LogicalOpGenerator:
    ...
```

#### Dynamic Inputs and Outputs

If you need to build `Input` and/or `Output` dynamically, you can define a
`configure()` method. It will always be called after all `GeneratorParam` values
are valid, but before `generate()` is called. Let's take our example and add an
option to pass an offset to be added after the logical operator is done:

```
import halide as hl

x = hl.Var('x')
y = hl.Var('y')

_operators = {
  'xor': lambda a, b: a ^ b,
  'and': lambda a, b: a & b,
  'or':  lambda a, b: a | b
}

# Apply a mask value to a 2D image using a logical operator that is selected at compile-time.
@hl.generator(name = "logical_op_generator")
class LogicalOpGenerator:
    op = hl.GeneratorParam("xor")
    with_offset = hl.GeneratorParam(False)

    input = hl.InputBuffer(hl.UInt(8), 2)
    mask = hl.InputScalar(hl.UInt(8))

    output = hl.OutputBuffer(hl.UInt(8), 2)

    def configure(g):
        # If with_offset is specified, we
        if g.with_offset:
           g.add_input("offset", hl.InputScalar(hl.Int(32)))

    # See note the use of 'g' instead of 'self' here
    def generate(g):
        # Algorithm
        operator = _operators[g.op]
        if hasattr(g, "offset"):
            g.output[x, y] = operator(g.input[x, y], g.mask) + g.offset
        else:
            g.output[x, y] = operator(g.input[x, y], g.mask)

        # Schedule
        v = g.natural_vector_size(hl.UInt(8))
        g.output.vectorize(x, v)

if __name__ == "__main__":
    hl.main()
```

The only thing you can (usefully) do from `configure()` is to call `add_input()`
or `add_output()`, which accept only the appropriate `Input` or `Output`
classes. The resulting value is stored as a member variable with the name
specified (if there is already a member with the given name, an exception is
thrown).

#### Calling a Generator Directly

Each Generator has a class method (injected by `@hl.generator`) that allows you
to "call" the Generator like an ordinary function; this allows you to directly
take the Halide IR produced by the Generator and do anything you want to with
it. This can be especially useful when writing library code, as you can
'compose' more complex pipelines this way.

This method is named `call()` and looks like this:

```
@classmethod
def call(cls, *args, **kwargs):
    ...
```

It takes the inputs (specified either by-name or by-position in the usual Python
way). It also allows for an optional by-name-only argument, `generator_params`,
which is a simple Python dict that allows for overriding `GeneratorParam`s. It
returns a tuple of the Output values. For the earlier example, usage might be
something like:

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
the `@hl.generator` decorator – the Generator author doesn't need to know or
care about the specific details, only that they happen.

All Halide Generators are **single-use** instances – that is, any given
Generator instance should be used at most once. If a Generator is to be executed
multiple times (e.g. for different `GeneratorParam` values, or a different
`Target`), a new one must be constructed each time.

#### Notable Differences Between C++ and Python Generators

If you have written C++ Generators in Halide in the past, you might notice some
features are missing and/or different for Python Generators. Among the
differences are:

-   In C++, you can create a Generator, then call `set_generatorparam_value()`
    to alter the values of GeneratorParams. In Python, there is no public
    method to alter a GeneratorParam after the Generator is created; instead,
    you must pass a dict of GeneratorParam values to the constructor, after
    which the values are immutable for that Generator instance.
-   Array Inputs/Outputs: in our experience, they are pretty rarely used, it
    complicates the implementation in nontrivial ways, and the majority of use
    cases for them can all be reasonably supported by dynamically adding inputs
    or outputs (and saving the results in a local array).
-   `Input<Func>` and `Output<Func>`: these were deliberately left out in order
    to simplify Python Generators. It's possible that something similar might be
    added in the future.
-   GeneratorParams with LoopLevel types: these aren't useful without
    `Input<Func>`/`Output<Func>`.
-   GeneratorParams with Enum types: using a plain `str` type in Python is
    arguably just as easy, if not easier.
-   `get_externs_map()`: this allows registering ExternalCode objects to be
    appended to the Generator's code. In our experience, this feature is very
    rarely used. We will consider adding this in the future if necessary.
-   Lazy Binding of Unspecified Input/Output Types: for C++ Generators, if you
    left an Output's type (or dimensionality) unspecified, you didn't always
    have to specify a `GeneratorParam` to make it into a concrete type: if the
    type was always fully specified by the contents of the `generate()` method,
    that was good enough. In Python Generators, by contrast, **all** types and
    dimensions must be **explicitly** specified by either code declaration or by
    `GeneratorParam` setting. This simplifies the internal code in nontrivial
    ways, and also allows for (arguably) more readable code, since there are no
    longer cases that require the reader to execute the code in their head in
    order to deduce the output types.

## Keeping Up To Date

If you use the Halide Bindings for Python inside Google, you are *strongly*
encouraged to
[subscribe to announcements for new releases of Halide](https://github.blog/changelog/2018-11-27-watch-releases/),
as it is likely that enhancements and tweaks to our Python support will be made
in future releases.

## License

The Python bindings use the same
[MIT license](https://github.com/halide/Halide/blob/main/LICENSE.txt) as
Halide.

Python bindings provided by Connelly Barnes (2012-2013), Fred Rotbart (2014),
Rodrigo Benenson (2015) and the Halide open-source community.
