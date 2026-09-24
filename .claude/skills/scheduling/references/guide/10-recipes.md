# 10. Recipes

A handful of patterns come up over and over. Each one notes when it fits, and
gives the schedule to match.

## Sliding window for stencils

**Use when** a producer gets read at several offsets along the consumer's strip
axis: a separable blur, a vertical-then-horizontal filter, or anything where row
K depends on rows K±1, K±2 of the producer.

The idea is to keep the producer's output in cache and amortize the redundant
work. Store it at the strip level and compute one scanline at a time:

```cpp
const int vec = natural_vector_size<float>();
Var yo, yi;
out.split(y, yo, yi, 32).parallel(yo).vectorize(x, vec);
producer.store_at(out, yo)       // allocate at strip level
        .compute_at(out, yi)     // compute one scanline at a time
        .vectorize(x, vec);
```

Halide keeps a small rolling buffer of scanlines per strip, and each new `yi`
computes only the newly needed rows. Add `.fold_storage(y, K)` for a fixed-size
circular buffer. One small thing worth remembering:
`store_at(f, v).compute_at(f, v)` at the same var does nothing, so the
`store_at` there can be dropped. It only earns its keep at a strictly outer
loop.

## Tiling and storage layout

**Use when** an intermediate is cheap but reused many times inside a small
region (transpose, small convolutions, 2D stencils), or a downstream stage reads
the producer with a data-dependent index.

```cpp
Var xo, yo, xi, yi;
out.tile(x, y, xo, yo, xi, yi, 64, 32)
   .parallel(yo)
   .vectorize(xi, vec);
producer.compute_at(out, xo)
        .vectorize(x, vec);
```

Pick tile sizes so each tile's intermediates fit in L1/L2.

**Storage layout for data-dependent gathers.** When a downstream stage reads a
producer with a data-dependent index on some axis (trilinear sampling,
bilateral-grid lookup, or any gather where the index depends on the pixel value
rather than the loop variable), that axis usually wants to be innermost in the
producer's storage, via `reorder_storage`. For `producer(x, y, z, c)` read as
`producer(xi, yi, zi, c)` with `zi = cast<int>(val*K)`, put `c` and `z`
innermost:

```cpp
producer.reorder_storage(c, z, x, y);
```

Now neighboring `(c, z)` values share cache lines. The rule of thumb: axes
indexed by *data* go innermost, and axes indexed by *loops* can go outer.

**Inlined boundary conditions inside gathers.** If a Func does a lot of vector
gathers through an inlined `BoundaryConditions::repeat_edge` (or `mirror_image`,
and so on), the gather ends up re-running the boundary clamp per lane. The
profiler flags this as "more vector gathers than dense vector loads." Scheduling
the boundary-condition Func explicitly (`compute_at` the consumer's tile or row,
or `compute_root` if it's shared) builds the clamped buffer once and turns those
gathers into dense loads.

## Pyramids

**Use when** the pipeline has a Gaussian/Laplacian pyramid, or any
downsample-then-upsample structure (local Laplacian, interpolate, multi-scale
anything). The two chains run in opposite directions, so they want different
handling.

**Downsample chain** (`base → down[1] → down[2] → …`): each level `l ≥ 1` gets
consumed twice, once by the next level down and once by the upsampling path
coming back up. That's the multi-consumer case, so it's usually best to
`compute_root` every level whose materialized size fits comfortably, the small
upper levels especially. The full-resolution base is too big to `compute_root`.
It can be inlined if its RHS is cheap (a premultiply, clamp, or cast), or
`clone_in` it for the first downsample consumer and schedule that clone with a
sliding window.

**Upsample chain** (`interp[L-1] → … → interp[0] → output`): this one's a
single-use chain, so fuse it into the output's parallel loop with
`compute_at(output, yo)`. It's best not to `compute_root` upsampling levels,
since that forces every level through DRAM and adds a barrier per level.

## Long stencil chains

**Use when** the pipeline has 8 or more single-use stencil stages (3x3 / 5x5
box, gaussian) in a linear chain. Full fusion works nicely for short chains (5
or fewer) or pointwise chains, but it tends to fall apart here. Every stencil
expands the needed footprint by its half-width, so per strip the work grows
quadratically with chain length, and boundary work starts to dominate.

The fix is to group the chain with `compute_root` checkpoints every K stages (K
= 8 to 12 for 3x3 / 5x5 stencils on a 2K×3K image):

```cpp
const int K = 11;
for (int j = last; j > 0; j -= K) {
    Func &out = (j == last) ? output : stages[j];
    out.compute_root()
       .tile(x, y, xo, yo, xi, yi, tile_w, tile_h)
       .fuse(xo, yo, t).parallel(t)
       .vectorize(xi, vec);
    // The K-1 stages before this checkpoint slide inside its tiles:
    for (int i = std::max(0, j-K+1); i < j; i++)
        stages[i].store_at(out, t).compute_at(out, yi).vectorize(x, vec);
}
```

Each K-stage group becomes its own parallel pass. The barrier cost between
passes is trivial next to the redundant-work savings, which makes this the one
case where multiple parallel regions clearly pay off.

## Histograms and scatters

**Use when** the pipeline has an update `f(x, y, cast<int>(expr), c) += value`,
so one of the write indices is data-dependent. A few things tend to matter here:

- It's best not to vectorize the spatial axes of the update. Different lanes may
  compute different bins, which makes the scatter non-vectorizable, and Halide
  will refuse, scalarize, or race. For small inner dims (like `c`), `unroll` is
  a better fit.
- Try not to schedule the pure init separately. Treat `hist` and `hist.update()`
  as one unit at the same `compute_at` site.
- The go-to pattern is to `compute_at` the histogram per tile of its downstream
  consumer, so each tile owns a small private histogram that fits in cache with
  no cross-tile races. A reasonable default is
  `hist.update().reorder(c, r.x, r.y, x, y).unroll(c)`.
- To parallelize across the reduction itself, reach for `rfactor`
  ([Advanced Directives](18-advanced-directives.md)).

One case to watch for is global aggregates. Histogram-equalization pipelines,
where the histogram sits upstream of the output (every output pixel reads a CDF
from the global histogram), are different: the histogram has to finish before
the output starts. That needs two parallel phases, histogram then output, rather
than the per-tile pattern.

## `compute_with` for sibling Funcs

**Use when** a complete standard-shape schedule is already profile-clean, and
the profile shows a producer's loads dominating because two sibling Funcs keep
re-loading it. It's not worth reaching for before the basic shape is exhausted.

When two Funcs share a common producer and an identical iteration space, and are
computed at the same level, `a.compute_with(b, v)` fuses them into one loop.
Each iteration produces one value of each, reusing the loaded producer values
between them. The classic case is derivatives `Ix` and `Iy` that both read
`gray`: `Ix.compute_with(Iy, x)` loads `gray` once per `x` instead of once per
Func. See [Advanced Directives](18-advanced-directives.md) for the full rules.
