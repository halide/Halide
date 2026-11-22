# Debug symbols

The `debug_symbols` target feature makes Halide emit debug info for the code it
generates. The "source" that debug info refers to is the `.stmt` output: the
lowered IR exactly as it was handed to code generation. This lets you set
breakpoints on lines of the `.stmt`, step through it, inspect variables, and get
profilers to attribute time to its lines.

## Ahead-of-time compilation

Add the feature to the target, and also request the `stmt` output, so that the
debugger can find the file:

```shell
$ ./my_generator -g my_pipeline -o out -e static_library,h,stmt target=host-debug_symbols
```

or, with CMake:

```cmake
add_halide_library(my_pipeline FROM my_generators
                   FEATURES debug_symbols
                   STMT my_pipeline_stmt)
```

Without the `stmt` output, the debug info still refers to the file it would have
been written to, next to the compiled code.

## JIT compilation

With the feature in the JIT target (e.g. `HL_JIT_TARGET=host-debug_symbols`),
Halide writes the `.stmt` of each JIT module to the temp directory, as
`halide-jit-<name>-<hash>.stmt`, and registers the compiled code with the
debugger through the GDB JIT interface. Set `HL_DEBUG_CODEGEN=1` to see which
file a module refers to.

## What you get

- Every instruction is attributed to the line and column of the IR node it was
  generated for. Binary operators point at the operator itself, calls and loads
  at their name.
- Every function in the `.stmt`, including the closures of parallel loops, is a
  function to the debugger.
- `let` variables, loop variables and function arguments are visible as local
  variables. Loop variables are scoped to their loop. Buffer arguments
  (`*.buffer`) are typed as `halide_buffer_t *`, so their fields and dimensions
  can be printed.

Halide's names contain dots, so quote them in GDB:

```
(gdb) break my_pipeline.stmt:42
(gdb) info locals
(gdb) print 'f.s0.x'
(gdb) print *'input.buffer'
(gdb) print 'input.buffer'->dim[1]
```

## Pointing into the conceptual stmt

With the `debug_symbols_conceptual` feature instead, the debug info points into
the conceptual stmt (the `conceptual_stmt` output): the IR before it is lowered
to device code offloading, closures for parallel loops, and the like, which is
usually easier to read. Code made from IR that is not in the conceptual stmt,
e.g. the call that runs a parallel loop, is attributed to the nearest enclosing
IR that is, which can be the start of the function. A target can't have both
features.

## The html outputs

The `stmt_html` and `conceptual_stmt_html` outputs show the IR exactly as the
`stmt` and `conceptual_stmt` outputs print it: the line numbers and columns are
those of these files. The host assembly is compiled with debug info for them
(even without the `debug_symbols` feature, in which case the other outputs get
none), and that debug info links each line of IR to the assembly generated for
it. Hover IR to see its assembly, double-click it to keep that shown and go
there, and double-click it again (or press Escape) to let go of it; the same
goes for assembly and the IR it came from (the toolbar can make going there
scroll smoothly). A line of IR is marked faintly. Within the line, the debug
info says which IR node each instruction was generated for: on an operator, a
keyword or a name, the assembly of that expression and of all that is nested in
it is marked strongly. With rainbow parentheses on, the assembly that is
selected is colored by the node each instruction was generated for, in the
colors of the punctuation of those nodes. The assembly pane shows the line
numbers of the assembly file, without the lines that hold the debug info.

This needs every IR node to be printed in one place only, so lowering copies the
IR nodes that the stmt uses in more than one place.

## Caveats

- The code is still fully optimized, so many variables are `<optimized out>`,
  and stepping can jump back and forth between lines.
- DWARF version 4 is emitted, or CodeView on Windows.
