<!-- Describe your changes and the motivation behind them. -->

Fixes #<!-- issue number, if applicable -->

## Breaking changes

List any breaking changes here. If there are none, you can remove this section.
Common breaking changes include:

- Changes to any existing APIs in the `Halide::` namespace, but not the
  `Halide::Internal::` namespace.
- Anything else that may cause existing Halide code to fail to compile or change
  output.

These do not necessarily disqualify a PR from being merged, but they should at
least be tagged with the `release_notes` label.

## Checklist

- [ ] Tests added or updated (not required for docs, CI config, or typo fixes)
- [ ] Documentation updated (if public API changed)
- [ ] Python bindings updated (if public API changed)
- [ ] Benchmarks are included here if the change is intended to affect
  performance.
- [ ] Commits include AI attribution where applicable (see Code of Conduct)
