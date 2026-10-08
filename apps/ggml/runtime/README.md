# The app's Halide thread pool

`thread_pool_common.h` is a copy of Halide's default thread pool,
`src/runtime/thread_pool_common.h` as of upstream commit ba4b018d0 (its last
change on main), so that the app can try changes to it without changing Halide's
runtime. To see every change made here, which is what an upstream proposal would
contain:

```sh
git diff ba4b018d0:src/runtime/thread_pool_common.h HEAD:apps/ggml/runtime/thread_pool_common.h
```

`thread_pool.cpp` is the glue that compiles it into the app as ordinary C++.
Halide's runtime defines every thread-pool entry point as a weak symbol, so the
copy defines them all strongly (`halide_do_par_for`, `halide_do_parallel_tasks`,
`halide_semaphore_*`, `halide_set_num_threads`, `halide_shutdown_thread_pool`,
the `halide_default_*` handlers, ...). The linker then binds the kernels' calls
and the runtime's own internal calls to the copy, with no registration at
startup. This also replaces the parts that `halide_set_custom_parallel_runtime`
can't: the thread count and shutdown. The glue provides what runtime code gets
from `runtime_internal.h` and `runtime_atomics.h`, and wraps the pool's C++
internals in a namespace of its own so they can't collide with the runtime's
weak copies. `ggml_thread_pool` is an OBJECT library: from an archive, the copy
would only be loaded for undefined symbols, and the runtime's weak definitions
would already satisfy them.

## Changes to the copy

- **Keep-awake** (as proposed upstream in halide/Halide#9526, whose diff to
  `thread_pool_common.h` it applies verbatim): a refcounted keep-awake count.
  While it is held, up to `halide_get_num_threads() - 1` idle workers, and
  threads waiting for their own parallel loops, poll for work instead of
  sleeping; idle workers aren't demoted to the B team; an idle thread sleeps
  anyway after 4096 polls without work; acquiring the first reference wakes both
  teams. The glue renames the upstream `halide_thread_pool_keep_awake` to
  `ggml_halide_thread_pool_keep_awake` (so it can't collide with the runtime's
  if #9526 lands; its error message still says the upstream name), declared with
  the RAII holder `ggml_halide::ThreadPoolKeepAwake` in `thread_pool.h`.

`thread_pool_test.cpp` (ctest `ggml_thread_pool`) stress-tests the copy through
the q4_0 x q8_0 mul_mat kernel and direct calls: nested loops,
`halide_do_parallel_tasks` with semaphores, error propagation, keep-awake held
and not with gaps between loops, thread-count changes, shutdown and restart, and
concurrent callers. It checks results only, not timings.
