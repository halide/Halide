# 15. Storage Levels

So far a Func's compute level has fixed two things at once: where it's computed,
and where its storage gets allocated. These can be split apart. Alongside its
compute level, a Func has a **store level**: the loop where its buffer gets
allocated. By default the store level equals the compute level.

## `store_at` and `store_root`

Two directives change the store level, and only that. They don't move the
computation.

- `f.store_at(g, v)` allocates `f`'s storage in `g`'s loop over `v`.
- `f.store_root()` allocates `f`'s storage at the outermost level.

Why split them? To allocate storage at an outer loop while computing at an inner
one. Values computed on one iteration of the inner loop can then get reused on
later iterations. That's Halide's **sliding-window** optimization, and it lets
the buffer fold down to a small size. Those are changes to which values get
recomputed, and to buffer sizes. They don't change the produce/consume/`for`
structure. The one visible effect is an added `store` node.

## The `store` node

`print_loop_nest` prints `store f:` only when `f`'s store level differs from its
compute level. When they match, which is the default (and also
`store_root().compute_root()`), there's no `store` line.

When it shows, `store f:` sits at the store level and holds everything down to
`f`'s `produce`/`consume` at the compute level. The `produce`/`consume` and
every `for` stay right where `compute_at` alone would put them. `store_at` just
adds the enclosing `store g:` line. Here `g` is computed at `output`'s inner
loop `x` but stored at its outer loop `y`:

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func g("g");
g(x, y) = in(x, y);

Func output("output");
output(x, y) = g(x, y) + g(x + 1, y) + g(x, y + 1) + g(x + 1, y + 1);

g.compute_at(output, x).store_at(output, y);

output.print_loop_nest();
```

which prints:

```
produce output:
  for y:
    store g:          # at the store level (output's y)
      for x:          # loop between store and compute level
        produce g:    # at the compute level (output's x)
          for y:
            for x:
              g(...) = ...
        consume g:
          output(...) = ...
```

`store_root()` puts the node at the outermost level, wrapping the whole pipeline
body. Like `produce`/`consume`, the `store` node follows `f` per site-func
stage. It only shows up in the stages of the site that actually compute `f`.

## Sliding windows in practice

The store/compute split is the engine behind stencil sliding windows. For a
separable blur, store the producer at the strip level and compute it one
scanline at a time:

```cpp
producer.store_at(out, yo)     // allocate once per strip
        .compute_at(out, yi);  // compute one scanline per iteration
```

Halide keeps a small rolling buffer of scanlines per strip. Each new `yi`
computes only the newly needed rows. Add `.fold_storage(y, K)` to force a
fixed-size circular buffer of `K` slots. See [Recipes](10-recipes.md) for the
full pattern.

One thing to know: `store_at(f, v).compute_at(f, v)` at the same var does
nothing. It's the same as `compute_at(f, v)` alone. `store_at` only earns its
keep at a strictly outer loop than `compute_at`.

## `hoist_storage`: allocation placement only

There's a third, even more physical level: the **hoist-storage level**, set by
`f.hoist_storage(g, v)` or `f.hoist_storage_root()`. It moves the actual memory
allocation further out, to skip re-allocating inside a loop. It does not turn on
the sliding-window reuse that `store_at` enables. By default it sits with the
store level.

For the printed loop nest, this one is invisible. A schedule with
`hoist_storage` prints the same nest as without it. It only affects allocation
placement and buffer sizing. Use it to get the freshness of a fine-grained
`compute_at` without paying an alloc and free every iteration:
`f.compute_at(g, inner).hoist_storage(g, parallel_var)`.

One thing to be careful about here: **hoist only up to the loop that holds the
parallel loop, never above it.** If `g` is `parallel(yo)`, the allocation has to
stay inside `yo` so each thread gets its own buffer. Lift it past the parallel
loop and the buffer becomes shared state, which is a race.
`hoist_storage_root()` is only safe when no enclosing loop is parallel.

## Legality

- The store level has to **enclose** the compute level. Same loop, or an outer
  one. Storing inside the compute loop is illegal.
- A Func with a store level needs a non-inline compute level. `store_at`,
  `store_root`, or `hoist_storage` on an inlined Func is illegal. Give it an
  explicit `compute_at` or `compute_root` first.
- The hoist-storage level has to enclose the store level, which encloses the
  compute level. Hoisting to a loop inside the compute level is illegal.
- The store level, like the compute level, has to enclose every use of `f`.

## Where storage lives: `store_in`

`f.store_in(MemoryType::Stack)` allocates on the stack. Fast, but only for small
fixed-size allocations. This is a memory-type choice, separate from the store
level, and it doesn't change the printed loop nest.
