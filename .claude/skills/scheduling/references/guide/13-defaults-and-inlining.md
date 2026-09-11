# 13. Defaults and Inlining

Every Func has a **compute level** that says where it gets computed. There are
three:

- **inline**, the default,
- **root**, computed once at the top (see
  [Placement](14-placement-compute-root-and-compute-at.md)),
- **at** a consumer's loop (also
  [Placement](14-placement-compute-root-and-compute-at.md)).

This chapter is about the default. The other two get the next chapter.

## The default is inline, not root

**By default, every Func except the output gets inlined into its consumer.**
This is probably the most important thing to know about Halide's defaults, and
it catches a lot of people out, so it's worth saying plainly: the default is not
`compute_root`.

For a **pure** Func, inline means it isn't built at all. It gets no
`produce`/`consume` and no loops of its own. Wherever a consumer reads it, its
definition gets pasted in, like inline text, so it just vanishes from the loop
nest. A pipeline of pure Funcs with no scheduling collapses into one loop nest
over the output's dimensions, with every producer folded into the output's leaf:

```cpp
Var x("x"), y("y");

Func producer("producer");
producer(x, y) = x + y;

Func consumer("consumer");
consumer(x, y) = producer(x, y) + producer(x + 1, y + 1);

consumer.print_loop_nest();
```

Here `producer` is read twice by `consumer` but never appears.

Inlining trades memory for **redundant computation**. Each use re-evaluates the
producer, so reading a producer twice means computing it twice per output pixel.
Chain a few of those together and it multiplies out quickly.

### The inline-everything trap

Since the default is inline, making no scheduling calls on a real pipeline
doesn't leave a safe, slow baseline. The usual outcome is one of these:

1. a compiler that takes forever or runs out of memory (exponential inlining
   blowup),
2. huge, slow code, or
3. correct code that recomputes a staggering amount.

A decent first move toward a working baseline is the opposite of the default.
**Add `.compute_root()` to every Func that isn't a trivial, cheap, single-use
expression.** That's slow, but it works, and it's a fine base to optimize from.

Think of it as a starting point rather than a good schedule (see
[Scheduling for CPUs](05-scheduling-for-cpus.md)). The opposite mistake is just
as common, by the way: sprinkling `compute_at` on everything. There's more on
that in [What to Schedule](07-what-to-schedule.md).

## Realized vs not-realized

Two terms show up all over this part:

- A Func is **realized** when it's computed into storage somewhere in the nest.
  It gets its own `produce` block, and a `consume` wherever it's read.
- A **not-realized** Func has no block at all. Its definition gets pasted into
  every call site, so it never appears in the loop nest.

It's tempting to read "inline" as a synonym for "not realized." For a **pure**
Func that's exactly right. But inline is a level, not a promise, because of the
non-pure case below. Halide uses "inline" for the default level of every Func,
so inline and realized aren't opposites. The real opposite of realized is
"pasted in."

## The non-pure inline default

A **non-pure** Func left at the default inline level can't be pasted in. A
reduction isn't an expression. So Halide builds it instead, at the innermost
loop around **each** use, recomputing it from scratch every iteration. For
`g(x, y) = f(x)` with `f` an unscheduled reduction:

```cpp
Var x("x"), y("y");
ImageParam in(type_of<uint8_t>(), 2, "in");

Func f("f");
f(x) = 0;
RDom r(0, 10, "r");
f(x) += in(x, r);

Func g("g");
g(x, y) = f(x);   // f unscheduled, so it's realized at this innermost use

g.print_loop_nest();
```

which prints:

```
produce g:
  for y:
    for x:
      produce f:          # all of f's stages, recomputed every (x, y)
        f(...) = ...
        for r:
          f(...) = ...
      consume f:
        g(...) = ...
```

When `f` gets read at a single loop depth, this is exactly
`f.compute_at(consumer, v)` at the innermost enclosing loop. That's why the rest
of the manual can treat the non-pure inline default as "a default `compute_at`
at the innermost use." It only goes past any single `compute_at` when `f` gets
read at different depths in different stages of a consumer. Then the inline
level places `f` on its own, at each use's own enclosing loop.

This case is rare, and rarely the fast choice. In practice a non-pure
intermediate gets an explicit `compute_root` or `compute_at`.

## Undoing a schedule: `compute_inline`

`f.compute_inline()` resets `f`'s compute level back to inline. It's literally
`compute_at(inlined())`, and its job is to **undo** a previous
`compute_root`/`compute_at`. It resets only the compute level. Any recorded
`split`/`fuse`/`reorder` stays put. For a pure Func that's moot, since it
vanishes anyway. For a non-pure Func, which still gets built, a leftover
transform really does take effect on the built loops. One catch: a store or
hoist level can't be left set on an inlined Func (see
[Storage Levels](15-storage-levels.md)).
