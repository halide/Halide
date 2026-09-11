# 17. Loop Types

Apart from the loop structure the other directives build, each loop carries a
**type**. The type decides how its iterations run, and it's the first token on
the loop line:

```
for <var>:             # serial, the default
parallel <var>:        # run iterations on the thread pool
vectorized <var>:      # SIMD
unrolled <var>:        # unrolled
gpu_block <var><API>:  # GPU, plus a <device_api> suffix
gpu_thread <var><API>:
gpu_lane <var><API>:
```

The type is a per-dimension thing. Serial is the default and prints as plain
`for`. `serial(v)` resets a dimension back to it. The type token (and the GPU
device suffix) is part of the structure this guide describes.

## Setting a dimension's type

`f.parallel(v)`, `f.vectorize(v)`, `f.unroll(v)`, `f.serial(v)` set the type of
an existing dimension in place. No new loop. They work per stage, like the
transforms in [Reshaping Loops](16-reshaping-loops.md), so
`f.update(i).parallel(v)` types update stage `s(i+1)` only.

```cpp
Var x("x"), y("y");
ImageParam in(type_of<int>(), 2, "in");

Func f("f");
f(x, y) = in(x, y);

f.parallel(y);

f.print_loop_nest();
```

which prints `parallel y: for x:`.

Here are the go-to defaults:

- **Vectorize** the stride-1 axis at the machine width. Use
  `natural_vector_size<T>()`. For `float`, that's 8 on AVX2, 16 on AVX-512.
- **Parallelize** an outer axis with enough iterations to feed all cores (see
  [Scheduling for CPUs](05-scheduling-for-cpus.md)).
- **Unroll** small fixed axes, like a 3-channel `c`, instead of vectorizing or
  parallelizing them.

## The factor form does a `split` first

`f.vectorize(v, n)`, `f.unroll(v, n)`, `f.parallel(v, n)` first do
`split(v, v, vi, n)`, then type one of the two halves. Which half depends on the
directive:

- `vectorize(v, n)` and `unroll(v, n)` type the **inner** (width-`n`) loop. The
  outer stays `for`.
- `parallel(v, n)` types the **outer** loop. The inner stays `for`. Use this to
  block many small iterations into fewer, bigger tasks.

This is where the split's inner/outer order stops being invisible. The two
halves now carry different tokens. `vectorize(x, 8)` prints
`for …: vectorized …:`, while `parallel(x, 8)` prints `parallel …: for …:`.

## The type rides the dimension through transforms

Since the type belongs to the dimension, `split`/`fuse`/`reorder` carry it
along:

- **`split` / `tile`**: both new loops inherit the source dimension's type and
  device.
- **`fuse`**: the fused loop takes the **inner** dimension's type. The outer's
  gets dropped.
- **`reorder`**: the type stays stuck to its dimension as it moves. That's the
  second way a `reorder` becomes visible.

## GPU: type plus a device

The GPU directives set both a type and a `DeviceAPI` (default `Default_GPU`,
printed as a `<Default_GPU>` suffix). `gpu_blocks`/`gpu_threads`/`gpu_lanes`
type existing dimensions. `gpu_tile` splits, then makes the block loop
`gpu_block` and the tile loop `gpu_thread`. `gpu_single_thread()` wraps the
stage in an extent-1 block+thread pair.

`print_loop_nest` shows GPU loops raw. It skips the GPU-specific lowering, so
what gets scheduled is what prints, and GPU legality (block-encloses-thread,
warp limits) isn't checked on this path.

The type and device are separate fields. A non-GPU directive changes only the
type and leaves any device in place. Nothing clears a device.

## Extent-1 loops collapse, unless they're GPU

The loop-elision rule (an extent-1 loop prints no `for` line, see
[Placement](14-placement-compute-root-and-compute-at.md)) is gated on
`device_api == None`. A 1-iteration serial/parallel/vectorized/unrolled loop
gets removed. A 1-iteration **GPU** loop survives. That's why
`gpu_single_thread()` shows its extent-1 block and thread loops.

## Fusing needs matching types

When two stages fuse with [`compute_with`](18-advanced-directives.md), each pair
of shared dimensions down to the fuse level has to have the same type and
device. Fusing a `parallel` dim with a `vectorized` one gets rejected. When they
match, the shared loop carries that one type.
