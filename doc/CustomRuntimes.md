# Custom Runtime Prefixes

Every Halide runtime exposes a set C ABI symbols consisting of common host
functions -- `halide_malloc`, `halide_free`, `halide_error`,
`halide_do_par_for`, and so on -- as well as target specific methods which
handle device functionality -- `halide_cuda_*`, `halide_direct3dcompute_*`,
`halide_metal_*`, `halide_opencl_*`, `halide_vulkan_*`, etc. This C ABI also
consists of mutable process-global state stored as global variables (e.g. the
installed custom allocator, the thread pool, the memoization cache, the
profiler, etc.). This works well when a program contains a single Halide
runtime, but it becomes a problem when a program must contain *more than one*.

Two independently produced components -- say, two libraries that each embed
their own AOT-compiled Halide pipelines and runtime -- both define
`halide_malloc` and both carry the same runtime state globals. When they are
linked into one process the linker collapses those duplicate (weak/`linkonce`)
symbols into a single copy, so the two components silently *share* one runtime.
Installing a custom allocator or error handler for one then affects the other,
and the two cannot be given different runtime configurations at all.

Runtime prefixes solve this by letting you rename the runtime's symbols with a
prefix of your choosing, so that each component carries its own, independent
runtime. This document describes the feature, the scopes it exposes, and how to
use it from C++, from the `GenGen` command line, and from CMake.

## Scopes

Rather than a single prefix, three independent prefixes are available, one per
"visibility" of a runtime symbol. They correspond to the enum
`Halide::RuntimeLinkage`:

- **Import** -- the names a *generated kernel* uses to call into the runtime.
  When a pipeline is compiled with `no_runtime`, its calls to `halide_malloc`
  and friends are left as external references; the import prefix renames those
  references so they resolve against a matching prefixed runtime at link time.

- **Export** -- the names a *runtime library* makes externally visible. When you
  compile a standalone runtime, the export prefix renames the public C ABI it
  defines (`halide_malloc` becomes, e.g., `my_prefix_malloc`).

- **Internal** -- the names used *within* the runtime library. This covers the
  runtime's own C++ symbols in the `Halide::Runtime::Internal` namespace,
  including the mutable state globals. Renaming these is what actually keeps two
  prefixed runtimes' state independent; without it the state globals would still
  collide even if the public ABI were renamed.

Each prefix is optional and they are set independently. A prefix replaces the
leading `halide_` of the C ABI names; because the internal C++ symbols contain
no `halide_` to replace, the internal prefix is prepended to them.

The pipeline's own entry points (the function you called `compile_to_*` on, its
`_argv` wrapper, and its metadata) are never renamed, and neither are C library
symbols.

## How the pieces fit together

For a component to link and run, the prefixes of its kernel and its runtime must
agree:

- The kernel's **import** prefix must equal the runtime's **export** prefix, so
  the kernel's calls resolve to the runtime's definitions.
- The kernel's **internal** prefix must equal the runtime's **internal** prefix,
  for the same reason applied to any internal symbols they share.

Different components use *different* prefixes from one another; that is what
keeps them isolated. A typical setup for two components `A` and `B` is:

| Component | Runtime (`export`, `internal`) | Kernel (`import`, `internal`) |
| --------- | ------------------------------ | ----------------------------- |
| A         | `A_`, `A_internal_`            | `A_`, `A_internal_`           |
| B         | `B_`, `B_internal_`            | `B_`, `B_internal_`           |

When A and B are linked into one process, `A_malloc` and `B_malloc` (and their
respective state globals) are distinct symbols, so each pipeline uses its own
runtime and their state stays independent.

## Backends

Both the LLVM and the C backend honor runtime prefixes.

- The **LLVM backend** renames the symbols directly on the generated module: a
  definition takes the export prefix, a kernel-called external declaration takes
  the import prefix, and the runtime's internal C++ symbols (including its state
  globals) take the internal prefix.
- The **C backend** emits a kernel that calls into an external runtime, so only
  the import prefix applies to it. It renames the runtime's C ABI functions with
  a block of `#define halide_x <prefix>x` at the top of the generated source;
  the preprocessor rewrites the runtime's function declarations and every call
  site consistently, while leaving types (`halide_buffer_t`), typedefs
  (`halide_malloc_t`), and enum values untouched. These `#define`s are emitted
  only into the generated C/C++ *source*, never the header, so several prefixed
  headers can still be included together.

## Limitations

- Runtime prefixes are not currently supported for JIT, but it's something we
  would like to support in the future. The difficulty is that the JIT resolves
  runtime calls against a single process-global shared runtime which already
  exists. So, for now, requesting runtime prefixes on a JIT target will emit an
  error.

## Usage from C++

The prefixes are described by a `Halide::RuntimePrefixParams`, which wraps a
`std::map<RuntimeLinkage, std::string>`.

To compile a **standalone runtime** with a set of prefixes, pass the map to
`compile_standalone_runtime`:

```c++
#include "Halide.h"
using namespace Halide;

Target target = get_host_target();

std::map<RuntimeLinkage, std::string> ns = {
    {RuntimeLinkage::Export, "my_prefix_"},
    {RuntimeLinkage::Internal, "my_prefix_internal_"},
};

compile_standalone_runtime("my_prefix_runtime.o", target, ns);
```

To compile a **pipeline** whose runtime calls match that runtime, apply the
matching prefixes and compile with `no_runtime`:

```c++
Func consumer = /* ... */;
Pipeline p(consumer);

Target target = get_host_target().with_feature(Target::NoRuntime);

p.apply_runtime_prefixes(target, RuntimePrefixParams({
    {RuntimeLinkage::Import, "my_prefix_"},
    {RuntimeLinkage::Internal, "my_prefix_internal_"},
}));

p.compile_to_module({}, "my_pipeline", target)
    .compile({{OutputFileType::object, "my_pipeline.o"},
              {OutputFileType::c_header, "my_pipeline.h"}});
```

`apply_runtime_prefixes` records the prefixes on the pipeline; any subsequent
`compile_to_*` for a non-JIT target then applies them. Calling it with a JIT
target raises a `Halide::CompileError`.

Inside a `Generator`, the prefixes travel on the `GeneratorContext` as
`RuntimePrefixParams` and are applied automatically when the generator's module
is built; in practice these are supplied through the command line or CMake,
described below.

## Usage from the GenGen command line

The prefixes are ordinary generator parameters named `runtime_prefixes.import`,
`runtime_prefixes.export`, and `runtime_prefixes.internal`. Any of them may be
omitted.

To emit a prefixed **standalone runtime** (the `-r` output):

```
./my_generator -r my_prefix_runtime -o . -e object \
    target=host \
    runtime_prefixes.export=my_prefix_ \
    runtime_prefixes.internal=my_prefix_internal_
```

To emit a matching **pipeline** with `no_runtime`:

```
./my_generator -g my_generator -f my_pipeline -o . -e object,c_header \
    target=host-no_runtime \
    runtime_prefixes.import=my_prefix_ \
    runtime_prefixes.internal=my_prefix_internal_
```

## Usage from CMake

`add_halide_runtime` accepts a `PARAMS` argument that is forwarded to the
runtime generator, and `add_halide_library` already forwards `PARAMS` to the
pipeline generator. Give a runtime its export/internal prefixes, and give each
library its matching import/internal prefixes together with `USE_RUNTIME`:

```cmake
add_halide_generator(my_pipeline.generator SOURCES my_pipeline_generator.cpp)

# A runtime with the "my_prefix_" prefix.
add_halide_runtime(
    my_prefix_runtime
    PARAMS runtime_prefixes.export=my_prefix_ runtime_prefixes.internal=my_prefix_internal_
)

# A pipeline that links against it. add_halide_library() compiles with
# no_runtime automatically when USE_RUNTIME is given.
add_halide_library(
    my_pipeline
    FROM my_pipeline.generator
    GENERATOR my_pipeline
    USE_RUNTIME my_prefix_runtime
    PARAMS runtime_prefixes.import=my_prefix_ runtime_prefixes.internal=my_prefix_internal_
)
```

Repeating this with a second, differently-prefixed runtime and library produces
two components that can be linked into the same program without their runtimes
colliding. For a complete, working example -- three variants of one pipeline,
each with its own runtime, linked into a single test that checks their state
stays independent -- see `test/generator/runtime_prefixes_iso_aottest.cpp` and
its CMake wiring in `test/generator/CMakeLists.txt`.

## Verifying the result

The renaming happens on the symbols of the emitted object, so you can confirm it
with `nm`. A stock runtime exports `halide_malloc`:

```
$ nm my_prefix_runtime.o | grep malloc
0000000000000000 T my_prefix_malloc
```

and its internal state globals are prefixed as well:

```
$ nm my_prefix_runtime.o | grep custom_malloc
0000000000000000 D my_prefix_internal__ZN6Halide7Runtime8Internal13custom_mallocE
```

A `no_runtime` pipeline object correspondingly imports the renamed symbols
rather than the stock `halide_` ones:

```
$ nm my_pipeline.o | grep malloc
                 U my_prefix_malloc
```
