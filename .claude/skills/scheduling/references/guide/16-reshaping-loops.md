# 16. Reshaping Loops

Four directives rewrite a stage's dimension list. They add, remove, rename, and
reorder its loops. They change only that stage's own loops (and the dimension
names usable as `compute_at`/`store_at` sites). They never move the Func
relative to other Funcs, and never change which values get computed.

They work per stage. `f.<directive>(…)` schedules the pure stage.
`f.update(i).<directive>(…)` schedules update stage `s(i+1)`, and leaves the
others alone. Inside a stage, they treat `RVar`s exactly like `Var`s.

## `split`

`f.split(old, outer, inner, factor)` replaces dimension `old` with two: `inner`
(innermost, running `0 .. factor-1`) and `outer` just outside it. List `[x, y]`
under `split(x, xo, xi, 8)` becomes `[xi, xo, y]`, printing
`for y: for xo: for xi:`.

Net effect: one extra `for` loop. It's fine to reuse `old`'s name as `inner` or
`outer`.

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func f("f");
f(x, y) = in(x, y);

Var xo("xo"), xi("xi");
f.split(x, xo, xi, 8);

f.print_loop_nest();
```

## `fuse`

`f.fuse(inner, outer, fused)` is the reverse. It removes `inner` and `outer` and
puts a single `fused` dimension at `inner`'s old spot, running over the product
of their extents. `[x, y]` under `fuse(x, y, xy)` becomes `[xy]`, one loop
`for xy:`.

Net effect: one fewer `for` loop. The two vars have to be **adjacent** in the
loop nest. If they aren't, `reorder` them adjacent first.

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func f("f");
f(x, y) = in(x, y);

Var xy("xy");
f.fuse(x, y, xy);

f.print_loop_nest();
```

## `reorder`

`f.reorder(v_inner, …, v_outer)` lists dimensions **innermost first**, and
permutes only the listed dimensions among the slots they hold now. Any dimension
left unnamed keeps its spot. So `f(x, y)` (loops `for y: for x:`) under
`reorder(y, x)` becomes loops `for x: for y:`.

The last argument becomes the **outermost** loop. This one's easy to get wrong,
so it's worth repeating: the list is innermost-first, not outermost-first.

A `reorder` of plain serial loops is invisible in the printed nest. Since this
guide ignores loop-variable names and constant bounds
([Reading a Loop Nest](12-reading-a-loop-nest.md)), swapping two ordinary `for`
loops gives structurally identical output. `reorder` becomes visible two ways:

- **Topologically.** It changes which loop a `compute_at` producer sits under,
  and so how many site-func loops fall inside that producer's block.
- **By type.** Once loops carry distinct types
  (`vectorize`/`parallel`/`unroll`), the type token rides with the dimension and
  moves in plain sight (see [Loop Types](17-loop-types.md)).

The inner/outer order a `split` or `tile` picks is invisible for the same
reason, until one half gets typed.

## `tile`

`f.tile(x, y, xo, yo, xi, yi, xf, yf)` splits both `x` and `y`, then reorders
the four results into a tiled walk. It's exactly:

```cpp
f.split(x, xo, xi, xf);
f.split(y, yo, yi, yf);
f.reorder(xi, yi, xo, yo);   // innermost first
```

giving loops `for yo: for xo: for yi: for xi:`. Net effect: two extra `for`
loops.

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func f("f");
f(x, y) = in(x, y);

Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
f.tile(x, y, xo, yo, xi, yi, 8, 8);

f.print_loop_nest();
```

## Transformed dimensions are placement sites

The dimensions these transforms make are first-class loop levels. A producer
filed at a transformed dimension `d` slots in just inside `d`'s loop, with the
site's loops inside `d` falling inside the producer's `consume`. Same rule as
[compute_at](14-placement-compute-root-and-compute-at.md), now on the
post-transform list. A producer computed at a split outer loop, for instance,
lands between the outer and inner loops.

## Legality

- `split`, `fuse`, and `reorder` have to name dimensions that exist right now. A
  var that was never a dimension, or one a previous `fuse` already ate, gets
  rejected. `compute_at` at a dimension a `fuse` removed is the same "site has
  to be a current loop" rule after a transform.
- `reorder` has to name each dimension at most once.

## Related shape directives

Two more directives shape a stage without adding or removing loops:

- `f.bound(x, min, extent)` promises at compile time that `x` only runs over
  `[min, min+extent)`. Often **required** to get fixed-size vectorize/unroll on
  the output, and to unlock a lot of optimizations. It doesn't change loop
  structure.
- `f.reorder_storage(a, b, c, …)` changes the memory layout, innermost-dimension
  first. So `reorder_storage(c, x, y)` makes `c` the stride-1 axis. It changes
  storage, not the loop nest. See [Recipes](10-recipes.md) for when it matters.
