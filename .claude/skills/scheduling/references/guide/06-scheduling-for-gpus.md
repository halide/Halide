# 6. Scheduling for GPUs

Halide can target GPUs (CUDA, OpenCL, Metal), but setting a GPU target isn't
enough on its own. Nothing runs on the GPU until loops are mapped explicitly
onto the hardware's grid of blocks and threads. Where CPU scheduling reaches for
`parallel` and `vectorize`, GPU scheduling maps dimensions onto that grid.
[Loop Types](17-loop-types.md) covers the raw `gpu_*` primitives; this chapter
is the strategy for using them.

## The core primitives

The shift from CPU to GPU is mostly a one-to-one swap:

| CPU                         | GPU                             | What it does                                                                                                    |
| :-------------------------- | :------------------------------ | :-------------------------------------------------------------------------------------------------------------- |
| `.parallel(y)`              | `.gpu_blocks(y)`                | Maps a dimension onto a grid of thread blocks. Blocks don't share fast memory.                                  |
| `.vectorize(x)`             | `.gpu_threads(x)`               | Maps a dimension onto the threads within one block. Threads in a block can share memory.                        |
| `.tile(x, y, xi, yi, w, h)` | `.gpu_tile(x, y, xi, yi, w, h)` | Splits two dimensions into outer and inner, then maps the outer loops to blocks and the inner loops to threads. |

## The basic recipe

Scheduling a Func onto the GPU comes down to four steps:

- **Set a GPU target.** The target string needs a GPU backend, such as
  `host-cuda`, `host-opencl`, or `host-metal`.
- **`compute_root()` the kernel.** Applying `.compute_root()` to a Func marks a
  distinct kernel boundary, so the whole function is computed before the next
  stage begins.
- **Map the spatial dimensions** with `.gpu_tile()`, which spreads them across
  blocks and threads.
- **Handle intermediates** by computing them inside a block with
  `.compute_at()`, then mapping their inner loops with `.gpu_threads()`.

A pointwise example, mapping a 2D function onto 16x16 blocks of threads:

```cpp
Func f("f");
Var x("x"), y("y");
f(x, y) = input(x, y) * 2.0f;

Var xo, yo, xi, yi;
f.compute_root()
 .gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
```

## Picking block sizes

A few habits keep the hardware busy:

- **Keep blocks a multiple of the warp size.** That's 32 on NVIDIA, 64 on AMD.
  Sizes like 16x16 (256 threads) or 32x8 (256 threads) tend to work well.
- **Don't over-`compute_root`.** Halide handles host-to-device transfers
  automatically, so the thing to avoid is bouncing data through global memory
  needlessly. Reserve `.compute_root()` for stages that genuinely need global
  synchronization.
- **Stage reused data in shared memory.** Map the consumer with `.gpu_blocks()`,
  compute the producer at the block level with `.compute_at()`, then map the
  producer's inner loops with `.gpu_threads()`.

## Stencils and shared memory

When adjacent output pixels read overlapping input (a Gaussian blur, say),
pulling those inputs from global memory over and over is a major bottleneck. The
fix is to load the overlapping region into fast shared memory once per block.

Map the final consumer onto blocks and threads with `.gpu_tile()`, compute the
producer at the consumer's block level, and map the producer's inner loops with
`.gpu_threads()` so the whole block fills shared memory in parallel:

```cpp
Var xo, yo, xi, yi;
// Define the block grid on the final output
blur_y.compute_root()
      .gpu_tile(x, y, xo, yo, xi, yi, 16, 16);

// Stage blur_x in shared memory for each block of blur_y
blur_x.compute_at(blur_y, xo)
      .gpu_threads(x, y);
```

## Reductions

A reduction domain on a GPU needs care: if many threads write the same location
at once, they either race or serialize on atomic locks.

For a small reduction over a full-image output (a 5x5 local filter, for
instance), parallelize the spatial dimensions across threads and leave the
`RDom` serial. Both the initialization and the update stage need scheduling:

```cpp
RDom r(0, 5, 0, 5);
output(x, y) = 0.0f;
output(x, y) += input(x + r.x, y + r.y);

Var xo, yo, xi, yi;
// Parallelize the initialization
output.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
// Parallelize the update across spatial dimensions; r stays serial
output.update(0).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
```

For a massive global reduction, factor it with `rfactor`
([Advanced Directives](18-advanced-directives.md)) to compute partial sums per
block before aggregating them.

## Multi-stage pointwise pipelines

A deep chain of simple math (chained color, brightness, and contrast
adjustments, say) should not get `.compute_root()` at every stage. Doing so
writes each intermediate out to global VRAM and reads it straight back, which
saturates memory bandwidth for no reason.

Instead, let the pointwise stages inline, which is the default when no directive
is applied to an intermediate. Apply `.compute_root()` and `.gpu_tile()` only to
the final output. The compiler then fuses all the intermediate math into a
single kernel, keeping intermediate values in fast registers.
