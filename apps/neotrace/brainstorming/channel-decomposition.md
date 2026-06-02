# Channel (k-slice) decomposition

**Status:** [ ] Not started\
**Priority:** Low\
**Needs new C++:** No

## What it shows

For funcs with a `k` dimension (e.g., `gPyramid[j](x, y, k)`,
`lPyramid[j](x, y, k)`), render each k-slice as a separate tile arranged
horizontally, rather than collapsing them. This lets you see how each processed
intensity level differs and which levels drive any recomputation or load
hotspots.

Currently, the `strides` config in `FuncConfig` (e.g., `{1,0}, {0,1}, {200,0}`)
already tiles k-slices on the canvas — this idea is about making that
decomposition explicit and optionally togglable.

## Design decisions

- The `strides` layout in `halide_trace_config.h` already handles this for
  local_laplacian via the trace tag. This feature would add a per-func toggle in
  the UI to expand/collapse k-slices without requiring trace tags.
- A "expand k" button in the func inspector panel (right-click context menu on a
  func) could recompute the `strides` and layout on the fly.
- No new array types needed — it's a canvas layout change, not a new data
  channel.

## Implementation checklist

- [ ] Add "Expand k-slices" option in func right-click context menu
- [ ] Compute new `strides` value from func dimensionality and current zoom
- [ ] Re-layout affected `FuncItem` on the canvas
- [ ] Add "Collapse" option to undo

## Open questions

- Is this redundant with the existing `strides`-based layout from trace tags? If
  local_laplacian already tiles gPyramid k-slices via its tag, this may only
  matter for pipelines that don't emit trace tags.
