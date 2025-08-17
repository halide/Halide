# Testing

Halide uses CTest as its primary test platform and runner.

## Organization

Halide's tests are organized beneath the top-level `test/` directory. These
folders are described below:

| Folder               | Description                                                                      |
|----------------------|----------------------------------------------------------------------------------|
| `autoschedulers/$AS` | Test for the `$AS` (e.g. `adams2019`) autoscheduler                              |
| `common`             | Code that may be shared across multiple tests                                    |
| `correctness`        | Tests that check correctness of various compiler properties                      |
| `error`              | Tests that expect an exception to be thrown (or `abort()` to be called)          |
| `failing_with_issue` | Correctness tests that are associated with a particular issue on GitHub          |
| `fuzz`               | Fuzz tests. Read more at [FuzzTesting.md](FuzzTesting.md)                        |
| `generator`          | Tests of Halide's AOT compilation infrastructure.                                |
| `integration`        | Tests of Halide's CMake package for downstream use, including cross compilation. |
| `performance`        | Tests that check that certain schedules indeed improve performance.              |
| `runtime`            | Unit tests for the Halide runtime library                                        |
| `warning`            | Tests that expected warnings are indeed issued.                                  |

The tests in each of these directories are given CTest labels corresponding to
the directory name. Thus, one can use `ctest -L generator` to run only the
`generator` tests. The `performance` tests configure CTest to not run them
concurrently with other tests (including each other).

_TODO: Update this section to reflect the transition to GoogleTest_

The vast majority of our tests are simple C++ executables that link to Halide,
perform some checks, and print the special line `Success!` upon successful
completion. There are three main exceptions to this:

First, the `warning` tests are expected to print a line that reads
`Warning:` and do not look for `Success!`.

Second, some tests cannot run in all scenarios; for example, a test that
measures CUDA performance requires a CUDA-capable GPU. In these cases, tests are
expected to print `[SKIP]` and exit and not print `Success!` or `Warning:`.

Finally, the `error` tests are expected to throw an (uncaught) exception that is
not a `Halide::InternalError` (i.e. from a failing `internal_assert`). The logic
for translating uncaught exceptions into successful tests is in
`test/common/expect_abort.cpp`.

## Debugging with LLDB

We provide helpers for pretty-printing Halide's IR types in LLDB. The
`.lldbinit` file at the repository root will load automatically if you launch
`lldb` from this directory and your `~/.lldbinit` file contains the line,

```
settings set target.load-cwd-lldbinit true
```

If you prefer to avoid such global configuration, you can directly load the
helpers with the LLDB command,

```
command script import ./tools/lldbhalide.py
```

again assuming that the repository root is your current working directory.

To see the benefit of using these helpers, let us debug `correctness_bounds`:

```
$ lldb ./build/test/correctness/correctness_bounds
(lldb) breakpoint set --file bounds.cpp --line 18
Breakpoint 1: where = correctness_bounds`main + 864 at bounds.cpp:18:12, address = 0x0000000100002054
(lldb) run
Process 29325 launched: '/Users/areinking/dev/Halide/build/test/correctness/correctness_bounds' (arm64)
Defining function...
Process 29325 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
    frame #0: 0x0000000100002054 correctness_bounds`main(argc=1, argv=0x000000016fdff160) at bounds.cpp:18:12
   15       g(x, y) = min(x, y);
   16       h(x, y) = clamp(x + y, 20, 100);
   17   
-> 18       Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
   19   
   20       Target target = get_jit_target_from_environment();
   21       if (target.has_gpu_feature()) {
Target 0: (correctness_bounds) stopped.
(lldb) 
```

Now we can try to inspect the Func `h`. Without the helpers, we see:

```
(lldb) v h
(Halide::Func) {
  func = {
    contents = {
      strong = (ptr = 0x0000600002486a20)
      weak = nullptr
      idx = 0
    }
  }
  pipeline_ = {
    contents = (ptr = 0x0000000000000000)
  }
}
```

But if we load the helpers and try again, we get a much more useful output:

```
(lldb) command script import ./tools/lldbhalide.py
(lldb) v h
... lots of output ...
```

The amount of output here is maybe a bit _too_ much, but we gain the ability to
more narrowly inspect data about the func:

```
(lldb) v h.func.init_def.values
...
(std::vector<Halide::Expr>) h.func.init_def.values = size=1 {
  [0] = max(min(x + y, 100), 20)
}
```

These helpers are particularly useful when using graphical debuggers, such as
the one found in CLion.
