# Contributing to Halide

Thank you for your interest in contributing to Halide! This document explains
how to participate in the project, whether you're reporting a bug, proposing a
feature, or submitting code.

## Code of Conduct

All participants in the Halide community are expected to follow our
[Code of Conduct](CODE_OF_CONDUCT.md). Please read it before contributing.

## Getting started

### Before opening a pull request

If you are new to the project, please open an Issue or start a Discussion before
submitting a pull request. This helps us align on the approach before you invest
significant effort, and avoids situations where a contribution gets rejected
after the work is already done.

Even for established contributors, opening an Issue or Discussion first is
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
- **Enhancement requests** — for cases where Halide produces a correct output,
  but something could be better: an error message could be made clearer,
  documentation is missing or misleading, an optimization was missed, or a
  pipeline took unexpectedly long to compile.

Both templates will ask you to provide a minimal reproducing case. The more
self-contained your example, the faster we can investigate.

## Proposing features

Feature proposals should be well-specified: describe the problem, the proposed
solution, and any alternatives you considered. If your idea is still in the
brainstorming stage, start a Discussion instead; it can be promoted to an issue
once the design solidifies.

## Pull requests

### Requirements

_Note: the following list of requirements is still in an aspirational state. You
may see contributors make ad-hoc, but justified, exceptions to these rules. When
this happens, the justification should be noted in writing in the PR comments._

- **Tests are expected** for all code changes. Bug fixes should include a
  regression test. New features should include tests that exercise the new
  functionality. We will always ask that new features come with a fuzz test,
  where applicable. Documentation-only changes, CI configuration updates, and
  trivial fixes (typos, formatting) are exempt.

- **CI must be green.** We merge on green. Our CI runs a number of automated
  checks, including (but not limited to) `clang-format` for C++ and `ruff` for
  Python. If CI fails, please update your PR to fix the issue. The set of checks
  may grow over time. We use `pre-commit` to enforce these checks in CI; it is a
  good idea to run it locally, too.

- **AI attribution.** If a significant part of your contribution was generated
  by an AI tool, you must note this in the commit message using a
  `Co-authored-by` trailer, as described in the
  [Code of Conduct](CODE_OF_CONDUCT.md). For example (this list is not
  exhaustive):

  ```
  Co-authored-by: Claude Opus 4.6 <noreply@anthropic.com>
  Co-authored-by: Cursor <cursoragent@cursor.com>
  Co-authored-by: Gemini 3 <gemini-code-assist@google.com>
  Co-authored-by: copilot-swe-agent[bot] <198982749+copilot@users.noreply.github.com>
  Co-authored-by: chatgpt-codex-connector[bot] <199175422+chatgpt-codex-connector[bot]@users.noreply.github.com>
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
a PR is approved and CI is green, any maintainer may merge it. While we don't
have a formal code ownership process, it is a good idea to use git-blame to
determine who the relevant maintainer is.

Normally, we are able to review PRs within a (business) week. If a PR has not
been reviewed in that time, please add a comment to the PR tagging the relevant
maintainer. We appreciate reminders, but no more than once per week, please. Do
not ping maintainers over email or other side channels.

If your PR has stalled in review, it may be helpful to break it up into smaller
PRs that are easier to review. A good rule of thumb is that PRs larger than 500
lines take significantly longer to review.

## Common PR anti-patterns

In the past, we have seen PRs fail to be merged for a number of (often
frustrating) reasons. We list a few here to help you avoid them.

1. **Working on something large before broader consensus is reached.** If you're
   working on something that you expect to take multiple days of work or
   hundreds of lines of code to complete, please start a Discussion to make sure
   the design is sound and that the maintainers understand your problem and
   agree with your proposed solution. It's frustrating to do a lot of work just
   to hear the maintainers think the problem doesn't need to be solved in Halide
   (e.g. should be done at the application level or in LLVM). These are
   sometimes known as "XY problems."
2. **Communication deadlock between author and reviewer.** We sometimes see PRs
   stall because the author and the reviewer believe they are waiting on the
   other person to do something. This is why we encourage you to ping PRs when
   you're waiting for a response.
3. **Bundling unrelated changes, especially bug-fixes, into a single PR.** When
   CI is red due to a pre-existing bug, it is tempting to roll the fix into your
   PR. However, this makes the PR larger and makes it take longer to review.
   This can snowball.

## Building Halide

See the [README](README.md#building-halide) for build instructions.
