# Reuse distance heatmap

**Status:** [x] Done\
**Priority:** High\
**Needs new C++:** Yes\
**Keyboard shortcut:** Ctrl+U

## What it shows

Each pixel of each intermediate func is colored by how many packets elapsed
between the store that wrote it and the first subsequent load that consumed it.
Short reuse distance (blue) means the value was used immediately — good cache
behavior. Long reuse distance (red) means the value sat in memory for a long
time before being read, suggesting poor temporal locality or an unnecessarily
early `compute_root`. Pixels that were stored but never read (e.g., output
buffer pixels) are shown in gray. Pixels that were never stored (e.g., input
buffer, which only has loads) are black.

This view is complementary to the recompute heatmap: a pixel can be both
redundantly recomputed *and* have short reuse distance (written, immediately
consumed, written again — the `outGPyramid[3,4]` case), which confirms the
redundancy is cache-friendly even if it is wasted work.

## Design decisions

- **Which distance:** time from store to *first subsequent load* of the same
  (func, x, y). This is the most cache-relevant metric. "Last read" (lifetime)
  is a secondary stat that can be added later.
- **Multi-write pixels:** for pixels written multiple times (recomputes),
  compute a distance for each (store, next-load) pair and average them. A pixel
  recomputed 6× with consistently short distances is genuinely cache-friendly.
- **Never-read pixels:** store but no load → display as gray
  (`[128, 128, 128]`). Typical for output buffers.
- **Scrub gating:** reuse distance is a whole-trace quantity, but coloring is
  gated on `write_times` (the current scrub position) so the heatmap reveals
  each pixel's reuse color only once it has actually been computed. Without this
  gate the image would be fully colored at t=0 and scrubbing — forward *or*
  backward — would not change it.
- **Scale:** log-scale, normalized against a **global** (whole-pipeline) min→max
  range, *not* per-func. Reuse distances span many orders of magnitude —
  `compute_at` funcs may have distances of ~10 packets, `compute_root` funcs may
  have distances of 10⁶+. Use
  `(log1p(dist) - log1p(global_min)) / (log1p(global_max) - log1p(global_min))`
  mapped to blue→red. Per-func normalization was tried first and looked wrong:
  every func saturated near red because in log space each func's median sits
  just below its own max (e.g. local_laplacian per-func `t` was uniformly
  0.86–1.00). A shared global scale lets a well-scheduled func with short
  distances actually read blue, and makes magnitude comparable across funcs.
- **What counts as a "load":** a `halide_trace_load` event on func F at (x, y),
  as recorded by any consumer that calls `F(x, y)`. This is exactly what
  `collect_load_pixels` already scans.

## C++ method: `collect_reuse_distances(start, end)`

Single pass over packets in `[start, end)`:

```
last_store: map<(func_name, packed_xy), packet_idx>
per_pixel:  map<(func_name, packed_xy), (sum_dist, count, min_dist)>

for each packet at idx in [start, end):
    if STORE to func F at (x, y):
        last_store[(F, pack(x,y))] = idx
    if LOAD from func F at (x, y):
        if last_store has (F, pack(x,y)):
            dist = idx - last_store[(F, pack(x,y))]
            accumulate into per_pixel[(F, pack(x,y))]
            erase last_store entry  ← so we only count first load per write

return per func: (xs, ys, mean_distances)   [also expose min_distances?]
```

Erasing the `last_store` entry after the first load ensures we measure "store →
first read" distance, not "store → every read."

Return dict: `{func_name: (xs_int32, ys_int32, mean_dist_float64)}`. Never-read
pixels are absent from the arrays (caller paints them gray by checking
`np.isnan(item.reuse_dist)`).

## Implementation checklist

### C++ (`PyTrace.cpp`)

- [x] Add `collect_reuse_distances(size_t start, size_t end) const` method
- [x] Single-pass implementation with `std::unordered_map` keyed on `packed_xy`
  (use `(static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y)` — same as
  `collect_load_pixels`)
- [x] Separate `last_store` map (func → packed_xy → idx) and accumulation map
- [x] Erase from `last_store` on first load to capture store→first-read distance
- [x] Return `{func: (xs, ys, mean_distances)}` as `py::dict` with numpy arrays
- [x] Add pybind11 `.def("collect_reuse_distances", ...)` binding

### Python (`viewer.py`)

- [x] Add `DisplayMode.REUSE_DIST = 3`
- [x] Add `FuncItem.reuse_dist` — `np.full((height, width), np.nan, dtype=np.float32)`
  in `_init_data_array`
- [x] Call `trace.collect_reuse_distances(0, len(packets))` in `load_trace`,
  store result, then populate `item.reuse_dist` for each func (via
  `_apply_reuse_distances`, also re-applied after backward scrubs)
- [x] Add `FuncItem._build_reuse_rgba()` — log-scale normalize, blue→red colormap,
  gray for never-read (stored but `reuse_dist == np.nan` after the C++ pass
  because it was stored but never loaded)
- [x] Wire into `_update_pixmap` branch
- [x] Add "Reuse &Distance" `QAction`, shortcut Ctrl+U, checkable, resets on `load_trace`
- [x] Add `_toggle_reuse_mode(checked)` handler

## Open questions

- Should we expose `min_distance` alongside `mean_distance`? Min shows the
  best-case cache hit; mean shows the average. Could add a sub-mode toggle
  later.
- For funcs with `fold_storage`, the physical (x, y) may wrap — does the packed
  key need to use logical or physical coordinates? The trace records logical
  coordinates, so this should be fine.
- Should the C++ method take an optional `min_only` flag to skip accumulating
  sum/count when only min is needed?
