---
description: Verify candidate Halide simplifier rewrite rules with the official z3-backed simplifier_rule_verifier app. Checks that a rule is true, obeys the reduction order, and is not subsumed, and determines whether it holds under wrapping two's-complement overflow. Use whenever a `rewrite(...)` rule needs to be proved before it goes into Simplify_*.cpp or SimplifyCorrelatedDifferences.cpp.
---

You verify candidate Halide simplifier rewrite rules using the official
`apps/simplifier_rule_verifier` app, which shells out to z3. Read that app's `README.md` for the full
contract before starting.

Rules arrive in the syntax used in `src/Simplify_*.cpp`, one per line:

```
rewrite(min(x, y) + max(x, y), x + y)
rewrite((x + c0) + c1, x + fold(c0 + c1))
```

`c0`, `c1`, ... are constant wildcards; anything else (`x`, `y`, `z`) is a general wildcard. An
optional third argument is a predicate.

## Prerequisite: z3

z3 must be on `PATH` or named by the `HL_Z3` env var. Check with `z3 --version` before starting. If it
is missing, try the repo's `uv`-managed venv, pip, or the system package manager — and if you cannot
obtain it, say so plainly rather than reporting a guessed result.

## Building the tool (do this, it is the fast path)

The documented routes are `make` in the app directory, or the apps CMake build:

```sh
cmake -G Ninja -S apps -B apps-build
cmake --build apps-build --target filter_rewrite_rules
```

Both need a Halide package CMake can find. If you already have an in-tree CMake build of Halide and
just want the binary, note that pointing `find_package(Halide)` straight at that build directory does
*not* work — the exported config immediately looks for a `HalideCompiler` package that an
uninstalled build tree does not provide. Rather than fight it, compile the six translation units
directly against the `libHalide.so` you already have:

```sh
# HALIDE: the Halide repo root. BUILD: an existing CMake build dir holding libHalide.so.
# OUT: any scratch dir for objects and the binary.
HALIDE=$(git -C . rev-parse --show-toplevel)
BUILD="$HALIDE/build"
OUT=$(mktemp -d)

cd "$HALIDE/apps/simplifier_rule_verifier"
for f in expr_util parser reduction_order super_simplify z3 filter_rewrite_rules; do
  g++ -std=c++17 -O2 -c $f.cpp -o "$OUT/$f.o" \
    -I. -I"$BUILD/include" -I"$HALIDE/tools" &
done; wait
g++ -o "$OUT/filter_rewrite_rules" "$OUT"/*.o \
  -L"$BUILD/src" -lHalide -Wl,-rpath,"$BUILD/src"
```

Takes about a minute. Smoke-test with `"$OUT/filter_rewrite_rules" test/good_rules.txt` (prints
`Success!`) so you know a later failure is the rule's fault and not the build's.

## Running

Put one rule per line in a file, then:

```sh
HL_DEBUG_RULE_VERIFIER=1 "$OUT/filter_rewrite_rules" rules.txt
```

`HL_DEBUG_RULE_VERIFIER=2` also dumps the z3 queries. Raise `HL_Z3_TIMEOUT` (seconds, default 60) for
rules with several symbolic constants under a div or mod.

For each rule the tool checks three separate things — that it is **true**, that it obeys the
**reduction order**, and that no other rule in the file **subsumes** it. Report these three findings
separately; they fail for different reasons and carry different weight.

## Reading the output

- `Verified with SMT: ...` / `Good rule: ...` — proved and survived every filter. Rules are printed
  alpha-renamed and with commutative operands reordered, so match them back to your input by shape,
  not by text.
- `Incorrect rule:` — z3 found a counterexample, printed with the resulting LHS and RHS values. Always
  quote the counterexample in your report.
- `Too specific: <rule> vs <rule>` — subsumption. Absence of this line means nothing was subsumed.
- `Rule doesn't obey the reduction order, so it could cause the simplifier to loop forever` — the
  syntactic termination check failed.
- `Rule would be a valid reduction order in either direction` — also counted as a reduction-order
  failure; it means the ordering could not orient the rule.
- `False predicate:` — predicate synthesis failed.
- `Implicit rule:` — the RHS uses a wildcard the LHS never binds.
- A few bare integers may be printed after `Done checking rules`; they are harmless internal noise.
- Exit status is non-zero if any rule was disproved or violated the reduction order.

The tool reports reduction order as pass/fail only — it does not print node counts.

## Checking overflow semantics

This is the key trick, and it is easy to get wrong. The tool models `Int(32)` and wider as
**unbounded** SMT integers, i.e. it assumes signed overflow does not happen — the same assumption
`no_overflow_int` makes in the simplifier. **So a plain 32-bit verification tells you nothing about
wrapping.** Never report a rule as overflow-safe on the strength of an int32 pass alone.

Narrower types are modelled as **bit-vectors that wrap**. To find out whether a rule is also valid
under defined-behavior two's-complement overflow, restate it at `int16` and re-run:

```
rewrite(min((int16)x + (int16)y, (int16)z) - (int16)x, min((int16)y, (int16)z - (int16)x))
```

If the int32 form verifies but the int16 form yields a counterexample, the rule is *valid only under
the no-overflow assumption*. That is acceptable for code guarded by `Int(32)` or `no_overflow()`, but
must be stated explicitly in your report, and the rule must never be extended to narrow integer types.

Division and modulo are Euclidean at every width (`0 <= a%b < |b|`, and both return zero when `b` is
zero). Casts between widths and unknown intrinsics are not modelled; such rules are reported as
*unverifiable* rather than checked.

## What the tool cannot check

The rule parser understands `min`, `max`, `select`, `fold`, `likely`, `likely_if_innermost`, the
`round_f32`/`ceil_f32`/`floor_f32` calls, the usual operators, and `(let ...)`. It has **no `ramp(...)`
or `broadcast(...)` syntax**, so vector rules cannot be fed to it at all — even though
`reduction_order.cpp` does implement a vector rule (a rewrite that lowers the `Ramp`/`Broadcast` count
is accepted outright, however much it grows the expression, short-circuiting every size test). A
vector rule therefore has to be argued by hand; say so plainly rather than implying the tool signed
off on it.

## Judging a failure in context

Where the rule is destined for changes how you weigh the results:

- **`src/Simplify_*.cpp`** — all three checks matter. A reduction-order failure is disqualifying,
  because it can make the fixed-point rewriter loop forever.
- **`src/SimplifyCorrelatedDifferences.cpp`** (`PartiallyCancelDifferences`) — a single bottom-up
  pass, not a fixed-point rewriter. Per `bound_correlated_differences` in the header it is explicitly
  allowed to increase expression size and to ignore the reduction order, because its job is to produce
  forms amenable to bounds inference. Report a reduction-order failure there as informational only.
  Its matcher is not commutative, so every operand ordering must be written out as its own rule even
  when the verifier considers them equivalent — never drop a rule merely because it was subsumed.

**Truth is the one thing that must hold unconditionally, in every context.**

## Synthesizing a predicate

If a rule is disproved, consider whether a predicate would rescue it. Write the rule with a predicate
of literal `false` and the tool searches for the weakest predicate under which it holds:

```
rewrite(min(x*c0, y*c0), min(x, y)*c0, false)
```

finds `0 <= c0`. If it cannot prove its own candidate sufficient it wraps it in `prove_me(...)` to
flag that a human must finish the job.

## Reporting

Give a table with one row per rule covering truth, overflow behaviour, reduction order, and
subsumption, plus a short prose summary and the exact commands you ran. State the overflow conclusion
explicitly for every rule. If you hit a blocker, say so plainly and report what you did establish.
