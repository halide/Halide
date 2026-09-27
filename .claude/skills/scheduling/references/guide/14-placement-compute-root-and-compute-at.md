# 14. Placement: `compute_root` and `compute_at`

Two directives pull a Func off the inline default and give it real loops.
`compute_root` builds it once at the top, and `compute_at` builds it inside a
consumer's loop. This chapter walks through both, plus the order realized Funcs
show up in and when a placement turns out to be illegal.

## `compute_root`: build it once at the top

`f.compute_root()` computes `f` in full, once, at the outermost level, before
anything uses it. It gets its own loop nest (all its stages) wrapped in
`produce f`. The rest of the program nests under `consume f`.

`consume f` is not selective. It just wraps everything after `produce f`,
whether or not those parts read `f`. The name means "`f`'s values are ready to
use here," not "this is exactly `f`'s readers."

Put several Funcs at `compute_root` (plus the output, always at root) and they
come out in **realization order**. That's a topological order where every
producer comes before its consumers. Each non-final Func wraps the rest of the
program in its `consume`, so the blocks nest:

```
produce F1:
  <loops of F1>
consume F1:
  produce F2:
    <loops of F2>
  consume F2:
    ...
        produce Fn:
          <loops of Fn>
```

The last Func, always the output, gets no `consume`, since nothing reads it.

As a concrete example, a shared `base` feeds two intermediates that both feed
the output:

```cpp
Var x("x"), y("y");

Func base("base");
base(x, y) = x + y;

Func left("left");
left(x, y) = base(x, y) + 1;

Func right("right");
right(x, y) = base(x, y) * 2;

Func output("output");
output(x, y) = left(x, y) + right(x, y);

base.compute_root();
left.compute_root();
right.compute_root();

output.print_loop_nest();
```

`base` is built once, even though both `left` and `right` read it, and the
`consume` blocks nest:

```
produce base:
  for y:
    for x:
      base(...) = ...
consume base:
  produce left:
    for y:
      for x:
        left(...) = ...
  consume left:
    produce right:
      for y:
        for x:
          right(...) = ...
    consume right:
      produce output:
        for y:
          for x:
            output(...) = ...
```

## Realization order

The topological order isn't unique. Where a consumer's producers don't depend on
each other, Halide picks a deterministic order. It's the post-order of a
depth-first walk from the output: go down a node's producer edges, then add the
node after its whole subtree, skipping anything already visited. Two things fall
out. Every producer comes before all its consumers. A shared producer gets added
once, on first reach.

The walk's only choice is the order it descends a node's producer edges. That
order breaks ties on three keys, in order: **prefix, then first-visitation
index, then full name.** The prefix is the Func's name with any `$n` suffix and
trailing digits stripped. The first-visitation index is the order Funcs first
get seen in a separate pre-order walk from the output.

For most schedules this boils down to "alphabetical by name." One thing to keep
in mind, though: it ranks a consumer's own producers, and it's not the
left-to-right order of the defining expression. `consumer(x) = b(x) + a(x, y)`
still builds `a` before `b`. Every Func is a node in this graph, including ones
that end up inlined, since an inline Func still passes its producers'
dependencies along. A [fused group](18-advanced-directives.md) counts as a
single node.

## `compute_at`: build it inside a consumer's loop

`f.compute_at(g, var)` builds `f` inside `g`'s loop over `var`, fresh on each
iteration of that loop, right before the part of `g`'s body that uses it. Its
`produce`/`consume` slots in as a prefix to that loop's body. The rest of `g`'s
body becomes the content of `f`'s `consume`.

Here's `producer.compute_at(consumer, y)` where
`consumer(x, y) = producer(x, y) + producer(x, y + 1)`:

```cpp
Var x("x"), y("y");

Func producer("producer");
producer(x, y) = x + y;

Func consumer("consumer");
consumer(x, y) = producer(x, y) + producer(x, y + 1);

producer.compute_at(consumer, y);

consumer.print_loop_nest();
```

which prints:

```
produce consumer:
  for y:
    produce producer:
      for y:
        for x:
          producer(...) = ...
    consume producer:
      for x:
        consumer(...) = ...
```

The injection recurses. A Func computed inside a Func that's itself computed
inside another nests the same way. The named `var` has to be a dimension of `g`:
one of its argument `Var`s, a dimension a transform made on `g`, or one of its
`RVar` loops.

### Several producers of one consumer

Each producer lands at its own compute level. They don't share one flat
`consume`.

- **Both at the same level** (say both `compute_at(output, y)`): they form a
  nested produce/consume chain at that level, ordered by the tie-break above.
- **At different levels**: the outer one shows up first, and its `consume` holds
  both the consumer's inner loops and the inner producer's block. Compute level
  decides the nesting, not the producer/consumer graph. An inner producer nests
  inside an outer one even when they're unrelated.
- **One at root, one `compute_at`**: the root producer joins the top-level chain
  and never nests inside the consumer.

### Loop elision: fewer loops than dimensions

A root Func prints one loop per dimension. A `compute_at` Func usually doesn't.
Halide computes only the region of the producer needed per iteration of the
site. A dimension whose needed extent is a single point becomes an extent-1
loop, and Halide drops it. No `for` line.

Here's the rule. A dimension `d` of `f.compute_at(g, L)` survives as a loop when
the `d`-coordinates of `f` read by `g` span more than one point, as `g`'s loops
inside `L` run. A multi-tap stencil in `d` keeps the loop. A single-point read
collapses it.

For `f.compute_at(output, x)` with `output` reading `f` at offsets `(0,0)`,
`(dx,0)`, `(0,dy)`, `(dx,dy)`:

- `dx = dy = 1`: both loops survive.
- `dx = 0`: `f`'s `x` loop drops.
- `dx = dy = 0`: both drop, and `f` prints no loops at all
  (`produce f: f(...) = ...`). This is why `compute_at` at the innermost loop of
  a pointwise consumer acts a lot like inlining.

Which loops collapse depends on bounds inference, so this guide doesn't work it
out. The loop structure, meaning placement, order, and which loops exist before
elision, comes straight from the schedule. And an elided loop still marks a
valid injection site. A Func computed at an elided loop lands there, outside any
surviving inner loops.

### When the site has several stages

`compute_at` names a site Func and a `Var`, never a stage. But a non-pure site
has several stages, and each one might have a loop named `var`. So here's the
rule.

`(g, var)` means the `var` loop in **every** stage of `g`. When the nest gets
built, `f` slots in just inside that loop, in each stage of `g` whose body uses
`f`. That's "uses" directly, or through another producer already built in that
body. A stage that never uses `f` gets nothing.

So a producer read only inside a reduction can be computed at that `RVar` loop.
It lands in that one stage. When several stages read `f` and share the named
loop, `f` slots into each, and its needed region (so which loops survive
elision) gets worked out per stage. Meanwhile `f`'s own compute level applies to
`f` as a whole. All of its stages build together at the chosen site.

`f.compute_at(h, v)` doesn't even need `h` to read `f` directly. It's enough
that `f`'s use lands inside `h`'s `v` loop. That happens when an intermediate
`g` reads `f` and is itself computed at `h`'s `v` loop.

## When a placement is illegal

All of `compute_at` legality is one idea. **The chosen level must enclose every
read of `f`.**

`f` built inside `g`'s `v` loop can only feed reads that sit inside some `g.*.v`
loop. So the schedule is legal exactly when every read of `f`, in any stage of
`g`, and including reads reached indirectly through Funcs `g` calls, sits inside
that loop family. Halide checks this by intersecting, over every read of `f`,
the loop levels around it. The survivors (plus `root`) are the legal sites.

The ways to be illegal are the ways that fails:

- **`var` is missing from a reading stage.** The named loop isn't present in
  some stage that reads `f`, so there's nowhere to put `f` there. Naming a
  reduction `RVar` is legal only when that's the one stage reading `f`, and
  illegal when another stage reads `f` too.
- **`g` isn't a consumer.** No stage of `g` reads `f`.
- **`f` is read outside `g`.** A different consumer, or `g`'s own outer scope,
  never gets reached by an `f` built inside `g`. When `f` gets read at two
  unrelated places, the only level around both is `root`.

That last one is the big case. Fixing it, so two consumers can each build their
own copy of `f` inside themselves, is what the wrapper Funcs `in()` and
`clone_in()` are for (see [Advanced Directives](18-advanced-directives.md)).
This "enclose every read" check never grows new cases. Later directives just
reshape the loop nest it runs against.
