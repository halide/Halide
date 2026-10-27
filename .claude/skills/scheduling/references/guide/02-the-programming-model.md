# 2. The Programming Model

Scheduling works on a handful of objects. This chapter names them and shows the
graph they form. The rest of the manual assumes this vocabulary.

## The objects

**`Var`.** A name for a dimension, which is a loop variable. A `Var` holds no
state beyond its own identity. Vars are the arguments of a pure definition, and
the handles named later in scheduling calls.

**`Func`.** A *handle* to a function definition. The `Func` is the thing that
gets scheduled. Copying a `Func` produces another handle to the same underlying
function, so scheduling through either one affects that single function. A
Func's state includes:

- its **name**, used for printing and for breaking ties in computation order,
- an ordered list of **stages**: an initial (pure) definition plus zero or more
  update definitions (below),
- the set of **other Funcs it reads from**, its *producers*, which come from its
  definitions,
- its **compute level** and **store level**: where it's computed and where its
  storage lives. The default is *inline*.

**`Expr`.** A value expression on the right-hand side of a definition. For the
loop nest, the only thing that matters about an `Expr` is which Funcs it reads.
That's what wires up the producer/consumer graph. Plain arithmetic, constants,
and `cast<T>(...)` never show up in the loop nest. They live inside the leaf
`f(...) = ...` line.

**`ImageParam`.** An input buffer. It's a *leaf*. It never gets computed and
never shows up in the loop nest. A Func that reads an `ImageParam` just has one
fewer producer to build. The buffer is already there.

**`RDom` / `RVar`.** A *reduction domain* and its *reduction variables*. An
`RDom r(min, extent, …)` declares one or more `RVar`s (`r.x`, `r.y`, and so on)
for use in update definitions. An `RVar` names a loop like a `Var` does. The
difference: its iterations are *ordered*, and can carry a dependency from one to
the next, like a running sum. RVars only appear in update definitions.

## Stages and update definitions

A Func can have **update definitions**. These are assignments, written after the
first one, that change the Func's values in place.

```cpp
Func hist("hist");
hist(x) = 0;              // stage 0: the pure (initial) definition
RDom r(0, N, "r");
hist(in(r)) += 1;         // stage 1: an update definition
```

The initial definition plus the updates form an ordered list of **stages**: `s0`
(pure), `s1`, and so on. Each stage finishes before the next one starts. They
all write to the same storage. Stages are part of the algorithm. They say what
the Func is, so no scheduling is needed to understand them. Each one is still
scheduled on its own, with `f.update(n)` (see
[Reshaping Loops](16-reshaping-loops.md)).

An update definition can reference **at most one** `RDom`. Several reduction
axes come from a single multi-dimensional `RDom`, like `RDom r(0, 4, 0, 5)`.

## Pure vs non-pure

A Func is **pure** if it has only its initial definition, with no updates. Add
one or more update definitions and it's **non-pure**. This split matters all
through Part III.

A pure Func can be substituted into its callers as an expression. A non-pure
Func can't, because a reduction isn't an expression, so it has to be built into
storage. That one fact drives several rules ahead.

## The pipeline graph

The Funcs, connected by producer edges, form a directed acyclic graph. That's
the *pipeline*. One Func is the **output**: the one that `print_loop_nest()` (or
`realize`) is called on. Everything reachable from the output by following
producer edges is part of the pipeline. Nothing else is.

A couple of things are worth locking in early:

- The output is special. It's always computed once, at the top, and it never
  gets inlined away.
- Forcing a Func to be non-inline makes it **opaque to bounds analysis**. Take
  an affine helper like `helper(x) = m*x + b`, used as an index in
  `f(helper(x))`. Inlined, it compiles fine. Force it non-inline, though, and
  bounds inference can fail with
  `calls ... in an unbounded way in dimension ...`. So it's usually best to keep
  index helpers inlined.
