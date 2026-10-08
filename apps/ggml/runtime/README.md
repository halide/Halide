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
- **Lock-free `do_par_for` fast path** (`fast_par_for_t`): with keep-awake, the
  remaining fork/join cost is `work_queue.mutex`, taken to queue a loop and
  again to claim and finish each iteration and to wait. A top-level parallel
  loop started while the pool is idle (nothing queued, and at least
  `min(size, threads) - 1` idle A-team workers polling in
  `halide_cond_with_spinning::wait`) instead publishes itself in a single global
  slot. The polling workers join it and claim iterations without locks, and so
  does the owner, which returns once every iteration has finished; the first
  error is returned. The loop is split into `min(size, threads)` contiguous
  chunks, one per participant: the owner's, then one per worker, numbered as it
  starts. Each chunk has its own cache line, with the worker's presence flag and
  the chunk's iteration counter, which keeps contention low, and gives a worker
  the same iterations (and data) in successive loops of the same size, like
  GGML's static row split. A participant claims iterations from its own chunk,
  then from the others' in turn, so a late or absent helper's iterations are run
  by whoever is free and never hold up the loop. It adds the number it claimed
  to a shared total once, as it leaves, and the owner waits for the total to
  reach the loop's size. The next fast-path loop waits for helpers still in the
  previous one to leave before reusing the slot. Loops of one iteration, or with
  one thread, run inline on the caller. Everything else takes the usual path:
  loops started while the slot is in use (nested in a fast-path iteration, or on
  another thread), loops on a busy pool, and all of `halide_do_parallel_tasks`
  (semaphores, async). Busy-waiting uses the CPU's pause instruction
  (`yield`/`pause`), selected with `#if`. Upstream, the runtime is compiled
  arch-independently, so this would need a per-arch runtime helper. The glue
  adds `ggml_halide_thread_pool_fast_loops()`, which counts fast-path loops, for
  tests and diagnostics.
- **Lost owner wakeup** (an upstream bug, not specific to this copy): a thread
  that owns a job can run a serial task of another parallel region. If the
  task's semaphore runs dry, the thread puts it back and, once its own job is
  done, returns without looking at it again. The task's owner may have gone to
  sleep meanwhile, having seen the task in use, so nobody ran the rest of it.
  Putting a serial task back now wakes its sleeping owner. The fast path made
  this likely (about one run in 30 of `ggml_thread_pool` with
  `HL_NUM_THREADS=5`), because its owner and helpers each run
  `halide_do_parallel_tasks` at the same time in that test.

`thread_pool_test.cpp` (ctest `ggml_thread_pool`) stress-tests the copy through
the q4_0 x q8_0 mul_mat kernel and direct calls: nested loops,
`halide_do_parallel_tasks` with semaphores, error propagation, keep-awake held
and not with gaps between loops, thread-count changes, shutdown and restart,
concurrent callers, and the fast path (uneven iterations, failing iterations,
nested loops and semaphores inside its iterations, shutdown right after it, slow
and late helpers, oversubscription by spinning threads, and `HL_NUM_THREADS`
from 1 to 64). It checks that the fast path was used, and results, but not
timings.
