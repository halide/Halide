# Generator compile cache

## Overview

Building a large app suite runs one generator process per (generator, target)
pair, and most of that time is spent recompiling pipelines that haven't actually
changed since the last build. The generator compile cache is an opt-in,
content-addressed cache that lets `execute_generator` skip that work: when the
inputs to a generator invocation are unchanged, the previously emitted artifacts
(object files, headers, static libraries, etc.) are copied back into place
instead of being recompiled.

The cache is inert unless you opt in by setting the `HL_CACHE_DIR` environment
variable (or the corresponding `Halide_CACHE_DIR` CMake variable, see below) to
a directory. When it is unset, none of this machinery runs and build output is
identical to a Halide with no cache support at all.

The feature also requires that Halide was built with `WITH_SERIALIZATION=ON`
(the default), since the cache key is only sound if it includes a serialized
snapshot of the pipeline being compiled. If `HL_CACHE_DIR` is set but
serialization support isn't compiled in, Halide prints a one-time warning and
disables caching for that build.

## What gets cached

Both compilation paths that `execute_generator` can take are cached
independently:

- Compiling a generator (the `compile_multitarget` path), once per
  `-g`/`-o`/`target=...` invocation.
- Compiling the standalone runtime (the `-r`/GenRT path).

## Cache key

A cache entry is addressed by a SHA-256 digest that mixes in everything that can
affect the emitted files, so that two invocations agreeing on all of the
following are guaranteed to produce identical outputs:

- **Compiler identity**: a fingerprint of the running libHalide (or, if
  statically linked, the generator executable itself). This is normally the
  linker-assigned build ID, read directly from the already-loaded image (Mach-O
  `LC_UUID` on macOS, ELF `.note.gnu.build-id` on Linux) so that rebuilding
  Halide invalidates the cache without hashing the ~32 MB binary on every run.
  If no build ID is available (e.g. `--build-id=none`, or Windows, which isn't
  yet implemented), it falls back to hashing the whole binary.
- **The generator name, output types, build mode** (normal vs. gradient), and
  the fully-resolved **generator-param settings** (`target=` is handled
  separately, below).
- **The target(s) and per-target suffixes** being compiled.
- **The serialized pipeline** for each target: the generator is instantiated and
  its (pre-autoschedule) algorithm and schedule are serialized via
  `Serialization.h` and folded into the key. This is what makes the key sound
  for source edits — the actual algorithm and schedule are captured, so editing
  a generator's `generate()`/`schedule()` invalidates the cache even when none
  of its command-line params changed. Autoscheduling itself is deliberately
  excluded so that computing a cache key never requires running a (potentially
  expensive) autoscheduler.
- **The contents of any `-p` plugins** (e.g. autoschedulers), since they affect
  codegen but live outside libHalide.

If the compiler identity can't be determined, or the pipeline can't be
serialized (for example, a generator that throws while building), the invocation
falls back to compiling normally without touching the cache.

## Using it from the command line

Set `HL_CACHE_DIR` before invoking a generator executable directly:

```shell
$ export HL_CACHE_DIR=$HOME/.cache/halide-generators
$ ./my_generator -g my_pipeline -o . target=host
```

The first invocation compiles and populates the cache; subsequent invocations
with the same generator, target, params, plugins, and (unchanged) pipeline
source restore the outputs instead of recompiling.

## Using it from CMake

The shipped CMake helpers (`add_halide_library`, `add_halide_runtime`, etc.)
read the `Halide_CACHE_DIR` cache variable — which defaults to
`$ENV{HL_CACHE_DIR}` — and, when it's set, wrap every generator/GenRT invocation
so the value reaches the generator process:

```shell
$ cmake -B build -DHalide_CACHE_DIR=$HOME/.cache/halide-generators
$ cmake --build build
```

When `Halide_CACHE_DIR` is empty, the emitted build commands are byte-for-byte
identical to a build with no cache configured. See
[`Halide_CACHE_DIR`](HalideCMakePackage.md#variables) in the CMake package docs.

## Cache maintenance

Entries are installed atomically (staged in a temp directory under the cache
root, then renamed into place), so concurrent builds never observe a partial
entry.

The cache is pruned opportunistically after each store to honor two optional
size/age limits, read from the environment:

- `HL_CACHE_MAX_SIZE` — maximum total size of the cache, evicting the
  least-recently-used entries first once exceeded. Accepts a plain byte count or
  a `K`/`M`/`G` suffix (powers of 1024). Defaults to `1G`.
- `HL_CACHE_MAX_AGE` — if set, entries not used within this many seconds are
  evicted regardless of total size.

Because a large parallel build runs many generator processes that each store an
entry, pruning is debounced to at most once every 60 seconds (via a
`.last_prune` stamp in the cache directory), so the size limit is a soft cap
that a burst of concurrent stores can briefly exceed. Pruning across processes
is additionally serialized by a non-blocking advisory lock; if another process
already holds it, a store simply skips its own prune pass rather than waiting.

To clear the cache entirely, just delete the directory named by `HL_CACHE_DIR`.
