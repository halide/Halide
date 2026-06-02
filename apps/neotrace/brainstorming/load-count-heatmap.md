# Load-count heatmap

**Status:** [x] Done\
**Priority:** Medium\
**Needs new C++:** Yes\
**Keyboard shortcut:** Ctrl+L

## What it shows

Each pixel is colored by how many times it was loaded, log-scaled. Black = never
loaded (output-only pixels). Bright = hot spot loaded many times. Reveals
stencil footprints (e.g., the 1-3-3-1 downsample kernel creates a cross-shaped
hot region at each pixel), cache pressure, and whether `compute_root` funcs are
being read redundantly.

## Design decisions

- `collect_load_pixels` already deduplicates — we need a *counting* variant
  instead. Added a separate `collect_load_counts(start, end)` (clearer than a
  `deduplicate=False` flag, since the return type differs: counts vs. values).
- **Colormap:** the classic "hot" ramp black → red → yellow → white (R over
  `[0, ⅓]`, G over `[⅓, ⅔]`, B over `[⅔, 1]`). Intuitive for "hot spots" and
  visually distinct from the other heatmaps (recompute green→red, temporal/reuse
  blue→…). count == 0 maps to black with no special-casing.
- **Scale:** log-scale, `log1p(count) / log1p(max_count)`, normalized **per
  func** against that func's whole-trace max — *not* global. This is the
  opposite choice from the reuse-distance heatmap, and for a concrete reason:
  load counts are dominated by outliers (a single boundary-clamped `input` pixel
  was loaded ~3×10⁵ times; the `remap` LUT ~10⁵). On a shared global scale those
  crush every interior pipeline func into near-black. Load-count distributions
  are also bottom-heavy (median far below max), so per-func normalization does
  *not* saturate the way reuse distance did — each func uses the full ramp and
  its stencil footprint reads clearly.
- **Scrub gating:** load count is scrub-aware, mirroring reuse distance. Rather
  than gating a whole-trace quantity, the per-pixel `load_counts` array is
  *accumulated incrementally* as the user scrubs forward (loads that have
  happened so far), and rebuilt from zero on backward scrubs — exactly like the
  store-driven `data` array. This also makes input-only buffers (no stores) work
  correctly: they accumulate their loads over time, which is precisely where the
  interesting stencil footprints live (gating on `write_times`, as reuse
  distance does, would have left them permanently black). The color *scale*
  (per-func max) is fixed at the whole-trace value so brightness fills in toward
  a stable final color rather than rescaling each tick.
- **Performance:** the per-range `collect_load_counts` scan only runs while the
  mode is active (loads typically outnumber stores, so an unconditional scan
  would roughly double scrub cost). Entering the mode rebuilds `load_counts` for
  `[0, current_time]` once to establish the baseline.

## C++ method: `collect_load_counts(start, end)`

Single pass over loads in `[start, end)`; per func, accumulate a count per
unique packed `(x, y)`. Returns `{func: (xs_int32, ys_int32, counts_int32)}`,
one entry per unique loaded pixel. Each `(x, y)` is unique within a func's
arrays, so the Python side can accumulate successive ranges with a plain
in-place `+=` (no duplicate fancy-index targets).

## Implementation checklist

- [x] Add `collect_load_counts(size_t start, size_t end)` to `PyTrace.cpp`
  (like `collect_load_pixels` but accumulates a count instead of deduplicating)
- [x] Add pybind11 binding
- [x] Add `DisplayMode.LOAD_COUNT = 4`
- [x] Add `FuncItem.load_counts` array (`np.zeros((height, width), dtype=np.int32)`)
  and `FuncItem.load_count_norm_max` (per-func fixed scale)
- [x] Accumulate incrementally in `_render_to_time` (forward extends, backward
  /init rebuilds) — only while the mode is active
- [x] Compute per-func whole-trace max in `load_trace`
  (`_load_count_max_by_func`); apply via `_apply_load_count_scale`, re-applied
  after backward scrubs
- [x] Add `FuncItem._build_load_count_rgba()` ("hot" ramp, log-scaled)
- [x] Wire into `_update_pixmap`, add "Load Count Heatmap" menu action (Ctrl+L),
  add it to the mutual-exclusion group, reset on `load_trace`
- [x] Add `_toggle_load_count_mode(checked)` — rebuilds counts for the current
  time on enter, since accumulation only runs while active

## Open questions

- Could expose a *unique-loaders* count (how many distinct consumers read a
  pixel) as a sub-mode, separate from raw load count.
- A per-func vs. global scale toggle could be useful for the rare case where
  cross-func magnitude comparison matters more than per-func structure.
