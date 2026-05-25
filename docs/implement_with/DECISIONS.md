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
