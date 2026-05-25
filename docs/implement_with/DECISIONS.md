# `implement_with` — Decisions Log

The authoritative record of *why* the implementation looks the way it
does. Each entry captures: the decision, the alternatives considered, and
the reasoning. Future sessions should consult this before re-litigating.

Distinguish two categories:

- **Design-doc-consistent decisions** — fill in detail that
  [`DESIGN.md`](DESIGN.md) leaves underspecified. Cheap. Just record here.
- **Design-doc-revising decisions** — *also* require an update to
  `DESIGN.md` (text edit + changelog entry). Marked **[REVISES DESIGN]**.

---

## Index

- [OQ#2 — spec-Func naming](#oq2-spec-func-naming) (Phase 1, session 1)
- [`require` vs `requires`](#require-vs-requires) (Phase 1, session 1) **[REVISES DESIGN]**
- [Storage location for the directive](#storage-location-for-the-directive) (Phase 1, session 1)
- [Serialization deferral](#serialization-deferral) (Phase 1, session 1)
- [Spec input Func type declarations](#spec-input-func-type-declarations) (Phase 2, session 2)
- [Auto-stub for undefined spec input Funcs](#auto-stub-for-undefined-spec-input-funcs) (Phase 2, session 2)
- [Materialization guard location](#materialization-guard-location) (Phase 2, session 2)

---

## OQ#2 — spec-Func naming

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** Spec authors must explicitly name spec-pattern Funcs via the
existing `Func(name)` constructor. The Builder's `spec()` docstring spells
this out. `MatchContext::input("a")` (Phase 3+) will key off these names.

**Alternatives considered.**

1. Macro that captures the local variable name, e.g.
   `HALIDE_SPEC_FUNC(a)` expanding to `Func a("a")`. **Rejected:** adds
   macro surface area Halide otherwise avoids, and provides no real
   ergonomic benefit over `Func a("a")`.
2. Positional naming via `MatchContext::input(0)`. **Rejected:** loses
   readability; the BLAS-style examples in §3.7 of DESIGN.md become
   essentially unreadable; cross-references like "the `K` dimension
   matches between `A` and `B`" lose their handle.

**Reasoning.** Matches existing Halide convention for naming Funcs.
Authors who care about names already use them; authors who don't can
accept auto-generated names from the matcher's diagnostic output. No new
mechanism required.

**Implication.** The Phase 1 test enforces this by reading
`p.outputs()[0].name() == "out"`. Future phases that consume spec
pipelines (matcher, schedule transfer) can assume the names are
authoritative.

---

## `require` vs `requires`

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved
**[REVISES DESIGN]**

**Decision.** The Builder method is named `require` (singular), not
`requires` as written throughout DESIGN.md §3.3–§3.7 and §4.1.

**Alternatives considered.**

1. Keep `requires`. **Rejected:** `requires` is a keyword in C++20 and
   later. Halide itself builds at C++17, so the *definition* would
   compile, but any **user** code at C++20+ calling
   `Instruction::Builder::requires(...)` would be a syntax error. The
   feature's whole point is to be a user-extensible library; making it
   only usable at C++17 would be a serious wart.
2. `with_required_features`, `target_requires`, `needs_target`, etc.
   **Rejected:** verbose and farther from the doc; `require` reads
   naturally and is closest to the original.

**Reasoning.** The cost of the rename is small (one method, one
docstring line). The cost of "this API is only usable in C++17" is
ongoing and ugly.

**Doc update.** DESIGN.md's prose examples (§3.3 onward) still spell it
`requires` for now — the rename is noted in the changelog
([§11](DESIGN.md#11-changelog)) but the examples are not yet rewritten,
to avoid mid-Phase-1 churn. **Phase 2 or whichever session next touches
the examples should propagate the rename throughout.**

---

## Storage location for the directive

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is stored as a
`std::vector` on `StageScheduleContents`, with accessors exposed on
`StageSchedule`.

**Alternatives considered.**

1. **Side-table in `Instruction.cpp` keyed by `Internal::Function`.**
   Considered for minimum schema churn. **Rejected:** static state that
   survives across pipelines, awkward lifetime, complicates deep_copy.
2. **`FuncScheduleContents` (per-Func, not per-Stage).** **Rejected:**
   §3.3 of DESIGN.md shows `f.update(0).implement_with(...)` — the
   directive is per-stage, so storage should be per-stage too. The Func
   overload of `implement_with` is just sugar for the init stage.

**Reasoning.** Per-stage matches the design's semantics and matches
existing per-stage directives like `prefetches`. The added field is
small (one vector + accessors); deep-copy was a one-line addition.

**Implication.** Future phases (matcher, schedule transfer) read the
directive from `definition.schedule().implement_with_directives()`. This
is the canonical location.

---

## Serialization deferral

**Phase:** 1 · **Session:** 1 (2026-05-25) · **Status:** Resolved

**Decision.** `Internal::ImplementWithDirective` is **not** serialized.
Phase 1 builds with `WITH_SERIALIZATION=OFF`. Builds with serialization
on will silently drop directives across serialize/deserialize.

**Alternatives considered.**

1. Add a flatbuffer schema entry and full serialize/deserialize support
   in Phase 1. **Rejected:** scope creep — flatbuffer schema changes are
   non-trivial and the directive is inert in Phase 1 anyway. Better
   landed when the directive carries semantic weight (Phase 3+).
2. Hard-error on serialization of a non-empty directive vector.
   **Tentatively accepted as a follow-up** (see below); not landed in
   session 1.

**Reasoning.** A new field that flatbuffers doesn't know about
round-trips to empty — bad if the user thinks it persists, fine if they
don't try to serialize. Phase 1 is inert anyway. Document the gap; don't
let it pretend to work.

**Follow-up actions.**

- Before Phase 3 (where directives start influencing bounds inference),
  either:
  - add full serialization, or
  - file an `internal_assert` in the serializer that fires when
    `implement_with_directives` is non-empty, so it can't slip silently.

---

## Spec input Func type declarations

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** Undefined spec input Funcs must be declared with explicit
element types via the `Func(Type, dims, name)` constructor — e.g.
`Func a(Float(32), 1, "a")`. The bare `Func a("a")` form (no type) auto-stubs
with `Int(32)`, which may cause type mismatches in Phase 4's structural
matcher if the use-site type differs.

**Alternatives considered.**

1. Allow bare `Func a("a")` and infer type from context (e.g., from the
   output Func's type). **Rejected for Phase 2:** type-polymorphic specs are
   a v2 feature; Phase 2 does not need to solve type inference. The
   design doc §3.3 example using bare `Func a` is aspirational shorthand.
2. Require explicit stub definitions (`a(i) = 0.0f;`). **Rejected:** Phase 2's
   purpose is to eliminate this boilerplate.
3. Use `ImageParam` for spec inputs. **Rejected:** `ImageParam` does not support
   Halide scheduling directives (`bound`, `vectorize`, etc.) that form the
   instruction contract (see design doc §4.2).

**Reasoning.** The matcher (Phase 4) will need to compare spec Func types
against use-site types. Getting the type right now (via `required_types`)
produces correct spec IR; getting it wrong defers a type error until Phase 4.
The cost to spec authors is minimal: one constructor change per input Func.

**Implication.** Phase 2 and Phase 4 tests use `Func(Float(32), 1, "name")`.
The design doc's bare-`Func` examples remain aspirational and should be
revisited when v2 type-polymorphic specs are designed.

---

## Auto-stub for undefined spec input Funcs

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** When `FuncRef::operator Expr()` is called for an undefined Func
inside a spec thunk, it auto-defines the Func with a zero stub body (typed
from `required_types` if set, `Int(32)` otherwise). This allows downstream
scheduling directives (`bound`, `vectorize`, etc.) to be applied — they
require the Func to have dimension args set, which only happens via `define`.

**Alternatives considered.**

1. Register args without creating a definition (new `Function::set_args()` API).
   **Rejected:** more invasive API change; semantically no different from a
   stub body that is never executed.
2. Modify `Func::bound()` to accept undefined Funcs in spec context.
   **Rejected:** `bound` and `vectorize` both ultimately need the args list;
   bypassing those checks individually would require many touch points.
3. Thread-local flag + special Call::make path (no auto-stub). **Rejected:**
   this approach produces undefined-Func Call nodes in the IR, which breaks
   `func.outputs() == 0` invariants in several downstream consumers.

**Reasoning.** The auto-stub is the minimal change that makes the full
design-doc spec syntax work end-to-end. The stub body is irrelevant because
spec pipelines are never lowered. The `const_cast` in `FuncRef::operator Expr()`
is safe because `FuncRef` is already a handle type — const-ness on a `FuncRef`
does not imply immutability of the backing Function.

**Implication.** After auto-stubbing, spec input Funcs are defined with a stub
body. The matcher (Phase 4) identifies them as inputs by checking whether they
appear in `Pipeline::outputs()` — inputs are those referenced but not in the
outputs list, regardless of whether they have stub definitions or not.

---

## Materialization guard location

**Phase:** 2 · **Session:** 2 (2026-05-25) · **Status:** Resolved

**Decision.** The spec-pattern guard is placed at the top of
`Pipeline::compile_to_module()`, which is the single choke point through
which all compile/realize paths pass (`compile_jit`, `compile_to_object`,
`compile_to_file`, `realize`, etc. all call it).

**Alternatives considered.**

1. Also guard `Pipeline::realize(vector<int32_t>)` directly.
   **Deferred:** the `realize` path's existing `has_pure_definition` checks
   do not fire for a defined output Func, so `compile_to_module` is always
   reached. No need to duplicate.
2. Guard at `Func::realize()` level. **Rejected:** this would miss other
   `Pipeline` entry points; guarding at `Pipeline::compile_to_module` is
   more complete.

**Reasoning.** Single guard, no duplication, catches all code paths.

**Implication.** The error message mentions `compile_to_module` in its
context. Future diagnostics improvements (Phase 6) may want to give more
specific pipeline-entry-point names in the error.
