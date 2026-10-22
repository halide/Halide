# simplifier_rule_verifier

Tools for checking and generating rewrite rules for Halide's simplifier
(`src/Simplify_*.cpp`). They were written for the paper "Verifying and Improving
Halide's Term Rewriting System with Program Synthesis" (Newcomb et al., OOPSLA
2021).

Both tools shell out to [z3](https://github.com/Z3Prover/z3), so it needs to be
on your `PATH`, or named by the `HL_Z3` environment variable. Set
`HL_DEBUG_RULE_VERIFIER` to 1 or 2 for progress and z3 queries on stderr, and
`HL_Z3_TIMEOUT` to raise the per-query limit in seconds from the default 60,
which some rules with several symbolic constants under a div or mod need.

## filter_rewrite_rules

```
filter_rewrite_rules rules.txt [output_dir]
```

Takes a file of proposed simplifier rules, one per line, in the same syntax used
in `src/Simplify_*.cpp`:

```
rewrite(min(x, y) + max(x, y), x + y)
rewrite((x + c0) + c1, x + fold(c0 + c1))
rewrite(x*c0 + y*c0, (x + y)*c0)
```

Variables named `c0`, `c1`, ... are constant wildcards, and anything else is a
general wildcard, as in the simplifier itself. A rule may carry a third argument
giving a predicate under which it applies.

For each rule it checks that:

- The rule is true, by asking z3 to find a counterexample.
- The rule obeys the reduction order in `reduction_order.cpp`, which is what
  stops the simplifier from rewriting in circles forever. Roughly, the right
  hand side must be strictly smaller than the left hand side under an ordering
  that accounts for both expression size and the specific operations used, so
  that repeated rewriting must terminate.
- No other rule in the file subsumes it.

Rules that fail are reported and dropped. The surviving rules are printed
grouped by the IR node type they apply to, ready to be pasted into the
corresponding `src/Simplify_*.cpp`. If an output directory is given, each group
is also written to `Simplify_<node type>.inc` in it.

The tool exits with a non-zero status if any rule was disproved or violated the
reduction order.

A rule may also be written with a predicate of `false`:

```
rewrite(min(x*c0, y*c0), min(x, y)*c0, false)
```

which asks the tool to synthesize the weakest predicate it can find under which
the rule holds. Above, it finds `0 <= c0`. If it can't prove the predicate it
synthesized is sufficient, it wraps it in `prove_me(...)` to flag that a human
needs to finish the job.

### What the checks assume

Signed integers of 32 bits and wider are modelled as unbounded SMT integers, so
overflow is assumed not to happen - the same assumption the simplifier itself
makes under `no_overflow_int`. Narrower types are modelled as bit-vectors, which
do wrap. Division and modulo follow Halide's Euclidean definition at every
width: `0 <= a%b < |b|`, and both return zero when `b` is zero.

Casts between widths aren't modelled, so a rule that mixes types is reported as
unverifiable rather than being checked. So is a rule using an intrinsic the SMT
conversion doesn't know; run with `HL_DEBUG_RULE_VERIFIER=1` to see which.

The reduction order is purely syntactic, so it rejects rules that terminate only
because a constant strictly decreases on each application, such as

```
rewrite((x + c0) % c1, (x + fold(c0 % c1)) % c1, c1 > 0 && (c0 >= c1 || c0 < 0))
```

Rules like that are in the simplifier and are fine; they just can't be justified
by this tool.

## super_simplify

```
super_simplify exprs.txt max_size
```

Takes a file of Halide `Expr`s, one per line, and uses counterexample-guided
inductive synthesis to search for the smallest equivalent expression of at most
`max_size` leaves. This is how candidate rules for `filter_rewrite_rules` were
found in the first place.

## Building

```
make
make test
```

or, from a CMake build of Halide:

```
cmake -G Ninja -S apps -B apps-build
cmake --build apps-build --target filter_rewrite_rules super_simplify
ctest --test-dir apps-build -L simplifier_rule_verifier
```
