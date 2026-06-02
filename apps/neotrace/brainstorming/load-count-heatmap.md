# Load-count heatmap

**Status:** [ ] Not started\
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
  instead.
- Add a new C++ method `collect_load_counts(start, end)` that returns
  `{func: (xs, ys, counts_int32)}` where each entry is one unique (x,y) with its
  total load count. Alternative: add a `deduplicate=False` flag to
  `collect_load_pixels`.
- Log-scale: `log1p(count) / log1p(max_count)`.
- Colormap: black (0 loads) → white or yellow (many loads) — or viridis.

## Implementation checklist

- [ ] Add `collect_load_counts(size_t start, size_t end)` to `PyTrace.cpp`
  (like `collect_load_pixels` but accumulates count instead of deduplicating value)
- [ ] Add pybind11 binding
- [ ] Add `DisplayMode.LOAD_COUNT = 4`
- [ ] Add `FuncItem.load_counts` array (`np.zeros((height, width), dtype=np.int32)`)
- [ ] Populate in `load_trace` (call C++, apply offsets, write into `load_counts`)
- [ ] Add `FuncItem._build_load_count_rgba()`
- [ ] Wire into `_update_pixmap`, add menu action

## Open questions

- Is `collect_load_counts` better as a separate method or as a
  `deduplicate=False` mode on `collect_load_pixels`? Separate method is cleaner
  since the return type differs (counts vs. values).
