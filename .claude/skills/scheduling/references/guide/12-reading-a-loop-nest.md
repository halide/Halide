# 12. Reading a Loop Nest

`Func::print_loop_nest()` prints the loop structure of a scheduled pipeline as
pseudocode. It's the way to confirm a schedule did what was intended, and it's
the output the rest of this part works through in detail.

## The notation

The output uses five line shapes, nested by two-space indentation:

```
produce <f>:        # a region that computes (stores into) Func f
consume <f>:        # a region that reads f's stored values
for <var>:          # a loop over one dimension
<f>(...) = ...      # the leaf: compute one point of f
store <f>:          # f's storage scope, shown only when it differs from compute
```

**Indentation means containment.** `produce f` holds the loops that compute `f`,
and ends at the matching `consume` or dedent. Everything that reads `f` sits
under `consume f`. A `for` holds its body. The leaf `f(...) = ...` runs at the
innermost point.

`consume` and `store` only turn up once a schedule places several Funcs, or
splits storage apart from computation. So they arrive later, with the directives
that create them in the chapters that follow. A single unscheduled Func needs
just `produce`, `for`, and the leaf.

## Loop structure, and what this guide sets aside

This guide is about the loop **structure** a schedule produces. That's the
produce/consume nesting, the count and nesting of `for` loops, their order, and
their **type** (`parallel`, `vectorized`, and so on, covered in
[Loop Types](17-loop-types.md)). That structure follows from the schedule, so it
can be reasoned about directly.

`print_loop_nest` also prints two things that don't affect the structure. The
examples here leave them out:

- **Exact loop-variable names.** Halide's names carry internal `split`/`rfactor`
  lineage plus a global counter, and can't be reproduced by hand.
- **Constant loop bounds** like `for x in [0, 7]`. These come from
  [bounds inference](04-bounds-inference.md).

Neither one changes the structure. A practical upshot: reordering two plain
serial loops gives identical output, so a bare `reorder` of serial loops leaves
no trace. Reordering *typed* loops does show (see
[Reshaping Loops](16-reshaping-loops.md) and [Loop Types](17-loop-types.md)).

The structure isn't *always* fixed by the schedule alone. The main exception is
**loop elision**. A loop whose extent works out to a single point gets dropped
from the output. Whether that happens depends on bounds inference over the real
index math, not on the schedule (see
[Placement](14-placement-compute-root-and-compute-at.md)). So the loops a
schedule implies are the ones shown here. Which of them collapse to a point is a
bounds question, flagged where it matters.

## One Func's loops

With no scheduling, a Func's loops come straight from its definitions. This is
exactly what shows up inside the output's `produce`, or any Func built on its
own.

Each stage carries an ordered **dimension list**, innermost first. For the pure
definition it starts as the argument `Var`s in order, so the first argument is
the innermost dimension. The stage prints **one `for` loop per dimension**,
outermost first (the reverse of the list), with the leaf in the middle.

So `f(x, y, c) = ...` has dimension list `[x, y, c]` and prints:

```
produce f:
  for c:
    for y:
      for x:
        f(...) = ...
```

That's row-major order: the first dimension varies fastest. `reorder` changes
the order. `split`/`fuse`/`tile` change the count (see
[Reshaping Loops](16-reshaping-loops.md)).

## Update stages print as sibling nests

All of a Func's stages print inside one `produce f` block, as back-to-back
sibling loop nests. Not as separate produce/consume blocks. There's no `consume`
between stages. A reader sees the Func's final, post-update values, so its
`consume` (if any) wraps the whole thing. Here's the histogram from
[Chapter 2](02-the-programming-model.md):

```cpp
Var x("x");
ImageParam in(type_of<int>(), 1, "in");

Func hist("hist");
hist(x) = 0;

RDom r(0, 256, "r");
hist(clamp(in(r), 0, 255)) += 1;

hist.print_loop_nest();
```

which prints:

```
produce hist:
  for x:              # stage 0: initialize hist(x) = 0
    hist(...) = ...
  for r:              # stage 1: the scatter update
    hist(...) = ...
```

Each stage has its own dimension list, built from its own left-hand side: the
free `Var`s on the LHS, plus the `RVar`s of the one `RDom` it uses. The `RVar`s
go innermost, in `RDom` declaration order (`r.x` innermost), with the free pure
`Var`s outside them. A pure dimension whose LHS slot holds an `RVar` or a
general expression prints no loop in that stage.

So `f(x, y) += in(x + r.x, y + r.y)` with a 2-D `RDom` gives the update stage
`for y: for x: for r.y: for r.x:`. The pure stage `f(x, y) = 0` gives just
`for y: for x:`. A scatter with no free LHS variable, like the histogram, gives
a stage with only the reduction loop.
