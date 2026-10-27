# 18. Advanced Directives

Four directives go past placing and reshaping a single Func's loops. `rfactor`
and `compute_with` restructure reductions and sibling Funcs for parallelism and
reuse. `in`/`clone_in` make per-consumer copies of a Func. `specialize` prints
conditional schedule variants. Reach for these only once the basic shape
([Scheduling for CPUs](05-scheduling-for-cpus.md)) is in place.

## `rfactor`: parallelize a reduction

A reduction loop carries a dependence across its iterations. Each step adds onto
the running result. So it can't be parallelized directly. `rfactor` splits the
work into independent **partial reductions** (which can be parallelized) plus a
final **merge**.

Call it on an update stage. It does two things. It makes a new intermediate
Func, and it rewrites the update stage into a merge that reads it.

```cpp
f(x)  = 0;
f(x) += in(r.x, r.y);                      // reduces over r.x and r.y
Func intm = f.update(0).rfactor(r.y, u);   // preserve r.y as a new pure Var u
```

becomes, in effect:

```cpp
intm(x, u)  = 0;              // the new intermediate: r.y is now a pure Var u
intm(x, u) += in(r.x, u);     // still reduces over r.x, parallelizable over u
f(x) += intm(x, r.y);         // f's update, rewritten to merge the partials
```

`rfactor` takes a list of `{RVar, Var}` pairs, the **preserved** vars. Each
named `RVar` becomes a fresh pure `Var` on the intermediate, which can then be
parallelized or vectorized. The non-preserved `RVar`s stay as the reduction the
intermediate does. The rewrite keeps functional equivalence. The reduction gets
re-associated, not changed.

**Schedule the intermediate.** It's an ordinary multi-stage Func, returned like
any other. By default it takes the non-pure inline default
([Defaults and Inlining](13-defaults-and-inlining.md)), which defeats the
purpose. So it normally gets `compute_root` (once, before `f`) or `compute_at` a
loop of `f` that encloses the merge's use. Its two stages schedule on their own,
so the partial reduction can be reordered, split, or parallelized by itself.

**Legality.** `rfactor` can only be called on an **update** stage, and the
reduction has to be **associative** (and commutative when an inner `RVar` is
factored out past a preserved outer one). Applied through a `specialize` branch
handle, it rewrites only that branch.

## `in` and `clone_in`: per-consumer copies

Both make a new, separate Func that a chosen set of consumers read instead of
the original. The difference is what that new Func computes:

- `f.in(g)` is an identity **wrapper**, `f_in_g(args) = f(args)`, that reads
  `f`'s stored result. It starts at the default inline schedule.
- `f.clone_in(g)` is an independent **clone** that recomputes `f`'s work. It's a
  copy of `f`'s whole definition and schedule as they stand at the call. `f` and
  the clone are independent, so they can be scheduled differently.

Both start not-realized (a pure wrapper or clone inlines straight back), and
show up only once given a compute level. Forms: `f.in(g)`, `f.in({g1, g2, …})`,
and a global `f.in()`.

The classic use is fixing the "two consumers force `f` to root" situation
([Placement](14-placement-compute-root-and-compute-at.md)). Give each consumer
its own wrapper, and each can build its copy inside itself:

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func f("f");
f(x, y) = in(x, y);

Func g("g");
g(x, y) = f(x, y);

Func output("output");
output(x, y) = f(x, y) + g(x, y);

Func fw = f.in(g);          // g now reads a wrapper of f, not f directly
f.compute_root();
fw.compute_at(g, x);
g.compute_root();

output.print_loop_nest();
```

`clone_in` is also the tool for a Func consumed by two stages with different
access patterns: clone it for one consumer, and schedule that copy on its own.

**Pass only Funcs that read `f` directly.** `f.in(g)` and `f.clone_in(g)` walk
the current call graph and redirect the first direct caller of `f` on each path.
When `g` reaches `f` only through intermediates, that first caller is a shared
Func that wasn't named. That leads to a whole family of surprises: redirecting
Funcs that were never named, order-dependent collisions between two wraps, pins
going stale after an `rfactor`. Passing direct consumers avoids all of it.

Two facts hold even when that advice is followed:

- **A clone shares `f`'s inputs.** The deep copy duplicates `f` but reads the
  same producers `f` reads. So a producer `p` that `f` reads now gets read in
  two places, and the only level around both is `root`. That makes
  `p.compute_at(f, x)` illegal. Clone the inputs too to give the clone private
  ones. A clone can also delete `f`: redirect every reader of `f` and `f`
  becomes unreachable and drops out. A wrapper, which reads `f`, never does.
- **A Func can be cloned only once.** A second, distinct clone or wrap on an
  already-wrapped Func aborts. Repeating the same `f.clone_in(a)` is fine, since
  it hands back the first clone. `f.in()` is exempt. This is a known upstream
  limitation.

## `compute_with`: fuse sibling Funcs

`b.compute_with(a, v)` interleaves two stages that would otherwise run in
separate nests into one shared loop nest, sharing their loops from the outermost
down to `v`. It makes no Func and changes no value. It only reshapes loops. The
Funcs tied together (directly or through a chain) by these per-stage **fuse
edges** form a **fused group**, placed once in realization order and built as a
unit.

The payoff is reusing loaded producer values between sibling Funcs. Classic
case: derivatives `Ix` and `Iy` that both read `gray`. `Ix.compute_with(Iy, x)`
loads `gray` once per `x` instead of once per Func.

```
produce f:                 # both f and g under one nest
  produce g:
    for y:                 # shared loops, outermost down to the fuse level
      for xi:
        f(...) = ...        # each member's body as siblings below v
      for xi:
        g(...) = ...
```

Only one member, the last in realization order, keeps the real shared loops.
Every other member's shared loops collapse to scheduling points at its splice
spot. All members have to share one compute level.

Legality:

- The two fused stages need **matching loop nests down to `v`**. `v` exists by
  name in both, the loop count from outermost down to `v` matches, and each
  shared pair agrees in kind (`Var` with `Var`, `RVar` with `RVar`) and in type
  and device ([Loop Types](17-loop-types.md)). Loops below `v` and all extents
  can differ.
- The fused Funcs have **no producer/consumer dependency** between them, and
  can't fuse into each other.
- All members share **one compute level**, the same `compute_root` or the same
  `compute_at` site. They can still have different store levels.
- The Func calling `compute_with` has **no specializations**.

A producer's `compute_at` legality inside a fused group needs no new rule. It's
the same "enclose every read" check, run against the post-fusion structure.

**Avoid inconsistent tiling across a fused group.** Fusing on a level `v` that's
a `tile`/`split` result in one member but reached differently in another is a
real Halide bug, not just a quirk. It can generate out-of-bounds accesses
([Halide #4751](https://github.com/halide/Halide/issues/4751)). Other shapes are
fine: multi-child groups, chains, differing extents, differing fuse levels.

## `specialize`: conditional schedule variants

`f.specialize(cond)` gives a definition a **conditional variant**. At run time,
if `cond` holds, a specialized schedule runs. Otherwise a fallback runs. It's
per definition (the pure stage, or a specific update stage via `f.update(n)`),
and each specialization forks its own copy of the whole definition, schedule and
LHS/RHS both.

```cpp
Func f = ...;
Stage fb = f.specialize(cond);   // returns a handle to the branch
fb.tile(...).vectorize(...);     // schedules only this branch
f.split(...);                    // directives after specialize() schedule the fallback
```

Directives on the returned handle hit only that branch. Directives on `f` after
the call hit the fallback. Repeated calls on the same handle add sibling
`if / else if` arms. Call `specialize` on a returned handle and it nests inside
that branch, so specializations form a tree. `f.specialize_fail(msg)` makes the
fallback a run-time error instead of a schedule.

`print_loop_nest` prints no conditions and no `if`/`else` markers. It walks
every branch, so the branch nests show up as back-to-back sibling subtrees under
one `produce`, in declaration order with the fallback last (`specialize_fail`
leaves no fallback nest). A producer computed at a loop of a specialized
consumer slots into each branch on its own, against that branch's own loop nest.
A producer computed at `root` prints once, not per branch.

**A producer can't be scheduled differently per branch.** There's no directive
for it. A producer is one Func with one schedule, shared by every branch, and
`in`/`clone_in` just redirect to a single wrapper read in all branches. The only
way is an algorithm change (two producers picked with `select`), which gives up
Halide's guarantee that scheduling can't change results. Treat it as a last
resort.
