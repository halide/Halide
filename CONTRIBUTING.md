# Contributing to Halide

Thank you for your interest in contributing to Halide! This document explains
how to participate in the project, whether you're reporting a bug, proposing a
feature, or submitting code.

## Code of Conduct

All participants in the Halide community are expected to follow our
[Code of Conduct](CODE_OF_CONDUCT.md). Please read it before contributing.

## Getting started

### Before opening a pull request

If you are new to the project, please open an issue or start a Discussion before
submitting a pull request. This helps us align on the approach before you invest
significant effort, and avoids situations where a contribution gets rejected
after the work is already done.

Even for established contributors, opening an issue or Discussion first is
encouraged for any non-trivial change.

### Questions and brainstorming

For questions about using Halide, brainstorming ideas, or early-stage feature
discussions, please use
[GitHub Discussions](https://github.com/halide/Halide/discussions). Issues are
reserved for actionable items: confirmed bugs, missed optimizations with
reproducing cases, and polished feature specifications.

## Reporting bugs

We have two issue templates for different kinds of problems:

- **Bug report** — for functional correctness issues (wrong output, crashes,
  compilation failures, etc.)
- **Missed optimization** — for cases where Halide produces correct but
  unexpectedly slow code.

Both templates will ask you to provide a minimal reproducing case. The more
self-contained your example, the faster we can investigate.

## Proposing features

Feature proposals should be well-specified: describe the problem, the proposed
solution, and any alternatives you considered. If your idea is still in the
brainstorming stage, start a Discussion instead; it can be promoted to an issue
once the design solidifies.

## Pull requests

### Requirements

- **Tests are expected** for all code changes. Bug fixes should include a
  regression test. New features should include tests that exercise the new
  functionality. Documentation-only changes, CI configuration updates, and
  trivial fixes (typos, formatting) are exempt.

- **CI must be green.** We merge on green. Our CI runs a number of automated
  checks, including (but not limited to) `clang-format` for C++ and `ruff` for
  Python. If CI fails, please update your PR to fix the issue. The set of checks
  may grow over time.

- **AI attribution.** If any part of your contribution was generated or
  substantially assisted by an AI tool, you must note this in the commit message
  using a `Co-authored-by` trailer, as described in the
  [Code of Conduct](CODE_OF_CONDUCT.md). For example:

    ```
    Co-authored-by: Claude <noreply@anthropic.com>
    ```

- **Documentation.** New or changed public APIs (those in the `Halide::`
  namespace) should be accompanied by updated documentation.

- **Python bindings.** Halide provides a Python binding for its public C++ API.
  If your change adds or modifies an API in the `Halide::` namespace, please
  update the corresponding Python bindings as well.

- **Performance impact.** If your change could affect code generation quality or
  runtime performance, please describe the expected impact in the PR description
  and include benchmark results where practical.

### Review and merging

Every pull request requires approval from at least one project maintainer. Once
a PR is approved and CI is green, any maintainer may merge it.

## Building Halide

See the [README](README.md#building-halide) for build instructions.
