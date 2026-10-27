# 7. What to Schedule

In a good schedule, every Func lands in one of three states:

1. **Inlined (no directive).** Cheap pointwise expressions with no internal
   reuse and few consumers. There's no memory cost, and they fold neatly into
   the consumer's vectorized loop.
2. **`compute_at(consumer, v)`.** Funcs whose output gets read more than once
   from a small window per consumer iteration, like stencils, blurs, and
   derivatives.
3. **`compute_root()`.** Multi-consumer at different footprints, or a per-strip
   recompute that's too expensive to fold in. The exceptions below cover these.

The default bias is to schedule as few Funcs as possible. Cache the one whose
output gets reused in a window, and let everything upstream inline into it.
Everything downstream then inlines into the next cached stage, or into the
output.

## Don't over-schedule

The most common over-scheduling mistake is an unnecessary `compute_at` on cheap,
single-use Funcs: elementwise products, luminance combines, boundary conditions,
normalizations, final combines. In the profile it shows up as a lot of tiny
Funcs, each under 5% of time, plus a pile of malloc/free overhead.

A handy rule of thumb: if a Func's RHS is one line and it has a single consumer,
it can usually be left alone. It's worth adding `compute_at` only once the Func
gets reused within a small window, or it's expensive enough to show up in the
profile before any directive is added at all.

## Don't double-cache

Say `A` is cached so its values stay hot across the 3x3 reuse in
`B(x, y) = sum of A(x±1, y±1)`. It's tempting to cache `B` too, but that usually
doesn't help. `B` reads `A` once per output pixel and writes once. The reuse
worth capturing was in `A`'s reads, not `B`'s writes, so caching `B` just adds a
store and a load for no extra reuse.

A quick check helps here. Whenever `compute_at` looks tempting, ask whether
anyone reads this Func's output at multiple places. If nobody does, inline it,
even when it looks like a stencil aggregation. A `det = A*B - C*C` combine
inlines. A final `out = a / b` normalization inlines. A Func read at several
spatial offsets, with nothing upstream cached at the same level, is the kind
that earns its `compute_at`.

## Exceptions: when to break the standard shape

A few patterns really do justify a separate `compute_root`, with its own
parallel region, for an intermediate.

**Multi-consumer with different footprints.** When a stage is read by multiple
downstream stages with different access patterns, `compute_root` is usually the
right call. Think of a pyramid level read by both the next downsampler and the
upsampling path, or a LUT read by two parts of the pipeline. Otherwise it gets
recomputed once per consumer. This isn't a license to `compute_root` every
pyramid level, though (see [Recipes](10-recipes.md)).

**Strip-axis overlap when the producer is small.** If a producer gets read with
an offset along the output's strip axis, computing it per strip needs an
expanded footprint each time, which means redundant work at the strip
boundaries. A `blury` reading `blurx(x, y±1)` while the output strips along `y`
is the usual example. Promoting the producer to `compute_root` makes sense when
all three of these line up: its materialized size is small (say under L3), the
access pattern is strip-axis offset, and it's cheap enough that one big parallel
pass beats the redundant boundary work times the strip count.

**Long stencil chains.** For chains of 8 or more single-use stencil stages,
fusing every stage starts to collapse under per-strip halo growth. The halo
grows with chain length, so per-strip work grows quadratically. The way out is
to break the chain with periodic `compute_root` checkpoints (see
[Recipes](10-recipes.md)).

## Don't `compute_root` something too big

It's worth estimating `extent_x × extent_y × extent_c × sizeof(T)` before
committing. If it's well over L3 (say over 16 MB), the buffer makes a full DRAM
round trip. The profile signature is a Func with `peak mem ≈ avg mem` and a lot
of time in `free`, which is the single-threaded teardown of a big buffer. For a
big intermediate, a few options tend to work better:

- **Inline it**, if the RHS is cheap. Duplicating per consumer beats writing
  gigabytes to DRAM.
- **`clone_in(consumer)`**, for one copy per consumer, each scheduled on its
  own.
- **Sliding window** inside the bigger consumer (see [Recipes](10-recipes.md)).

## The cost of each parallel region

Each `compute_root` plus `parallel` is its own parallel region, with a
thread-pool barrier that runs around 50 µs per region on 64 cores. So it's worth
watching the profile's `parallel loops` count, and not scattering `compute_root`
over cheap intermediates. Small single-use Funcs are generally happier
`compute_at` the output, or inlined.
