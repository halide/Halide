# 19. How the Loop Nest Is Built

Every rule in this part comes together in one procedure. Given a scheduled
pipeline, this is how `print_loop_nest` builds its output, and how to work it
out by hand.

## The procedure

**1. Force the output to root.** The Func that `print_loop_nest()` is called on
always gets computed once, at the outermost level. It never gets inlined.

**2. Work out the realization order.** Order the pipeline so every producer
comes before its consumers. That's the post-order DFS with the tie-break from
[Placement](14-placement-compute-root-and-compute-at.md). Every Func stays in
this order so it can pass dependencies along. But a **pure inline** Func never
gets built and drops out of the later steps. A **non-pure inline** Func does get
built and keeps its slot. An `rfactor` intermediate, and any `in`/`clone_in`
wrapper or clone, are ordinary Funcs here. A **fused group** is a single node,
placed as a unit, with its members taking consecutive slots in the group's
internal order.

**3. Give each realized Func a site.** A realized Func is any Func that isn't
pure-inline. It gets its own `produce`. Each one goes to a site:

- `compute_root` Funcs and the output form the **top-level chain**, in
  realization order.
- a `compute_at(g, v)` Func gets **filed under** `g`'s loop over `v`. Several
  Funcs at the same `(g, v)` keep realization order.
- a **non-pure inline** Func gets filed at the innermost loop around each of its
  uses, like a `compute_at` resolved on its own per use site.
- a **fused group** gets filed once, as a unit, at the one compute level its
  members share.

Each realized Func also has a **store level**
([Storage Levels](15-storage-levels.md)), which defaults to its compute level.
The site var `v` is a dimension of the site's post-transform dimension list,
since `split`/`fuse`/`reorder`/`tile` already rewrote it before this step.

**4. Emit outside-in.** Start at the top-level chain (the `compute_root` items
and the output, in realization order) and emit each item. Emitting an item
recurses, so the nest grows inward.

## Emitting an item

An item is a single Func or a whole fused group. Emitting one writes its
`produce`/ `consume` wrapper around its loop nest(s):

- a **single Func** `f`: `produce f`, then `f`'s loop nest(s), then `consume f`
  wrapping everything after. A Func with update definitions emits one loop nest
  per stage, in stage order, all inside the single `produce f`. No `consume`
  between stages.
- a **fused group**: the members' stages interleave into one shared nest,
  wrapped by a `produce`/`consume` for every member (last-realized outermost).

**A stage's loop nest** gets built by walking that stage's post-transform
dimension list from the outermost dimension inward. If the stage carries
specializations, this makes one nest per branch, printed back to back inside the
single `produce`. At each dimension:

- if the dimension is **elided** (extent 1, and not a GPU loop), skip its `for`
  line but keep the level as a valid injection site.
- otherwise emit the loop line with the dimension's **type token**
  ([Loop Types](17-loop-types.md)): `for` by default, else
  `parallel`/`vectorized`/`unrolled`/`gpu_*`.
- **open a `store h:` node** for any item `h` whose store level is this level
  while its compute level is deeper. Several `store` nodes at one level nest,
  not siblings.
- **inject** the items filed at this level, each emitted by this same procedure,
  its `consume` wrapping the rest of the body.
- go inward, bottoming out at the leaf `f(...) = ...`.

"Inject an item" is the one recursive step, so an item filed inside an item
filed inside a third nests the same way. `store_root()` is the case where an
item's `store` node opens at the very outermost level, around the whole nest.

## A worked assembly

This pipeline puts the core pieces together. `f1`, `f2`, `f3` are `compute_root`
and form the top-level chain in that order. `f4` is `compute_at(output, y)` and
slots in under the output's `y` loop. And `clamped` is inlined, so it never
appears.

```cpp
Var x("x"), y("y");
ImageParam input(type_of<uint8_t>(), 2, "input");

Func clamped = BoundaryConditions::repeat_edge(input);

Func f1("f1");
f1(x, y) = clamped(x, y) + clamped(x + 1, y + 1);

Func f2("f2");
f2(x, y) = f1(x, y) + f1(x + 1, y + 1);

Func f3("f3");
f3(x, y) = x + y;

Func f4("f4");
f4(x, y) = x + 3 * y;

Func output("output");
output(x, y) = f2(x, y) + f2(x + 1, y + 1) + f3(x, y) + f4(x, y) + f4(x, y + 1);

f1.compute_root();
f2.compute_root();
f3.compute_root();
f4.compute_at(output, y);

output.print_loop_nest();
```

With this procedure in hand, any schedule's loop structure can be worked out.
The performance chapters in Part II lean on exactly this to reason about what a
schedule costs.
