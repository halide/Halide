# Temporal order heatmap

**Status:** [x] Done\
**Priority:** High\
**Needs new C++:** No\
**Keyboard shortcut:** Ctrl+T

## What it shows

Each pixel is colored by when it was first written, normalized per-func to \[0,
1\] and mapped to a blue→yellow colormap. Unwritten pixels are black. The result
reveals tile order, wavefront patterns, and the structure of `parallel()` splits
— for example, `split(y, yo, y, 64).parallel(yo)` in `output` will appear as
interleaved horizontal stripes, each a different color depending on which
parallel tile claimed that row first.

## Design decisions

- **Normalization scope:** Per-func (packet index of first store for that func,
  scaled to its own min/max). Global normalization would make most funcs a
  single flat hue since their realization windows don't overlap much.
- **Colormap:** Blue (early) → yellow (late). Perceptually uniform and works for
  colorblind viewers. Implemented as a simple linear interpolation: R=t, G=t,
  B=1-t is a reasonable approximation; or compute viridis-style lookup table in
  NumPy.
- **Multi-write pixels:** Use the time of the *first* store. Subsequent rewrites
  (recomputes) do not change the color. This keeps it orthogonal to the
  recompute heatmap.
- **Data source:** `collect_pixels(start, end)` already returns all stores in
  packet order; take the minimum packet index seen for each (x, y) per func. No
  new C++ needed.

## Implementation checklist

- [x] Add `DisplayMode.TEMPORAL = 2` to the `DisplayMode` enum (`viewer.py:51`)
- [x] Add `FuncItem.write_times` — `np.full((height, width), np.nan, dtype=np.float32)`
  initialized alongside `last_value` in `_init_data_array`
- [x] In `_render_range`, after writing to `item.last_value`, record the (relative)
  packet index in `item.write_times` only where `np.isnan(item.write_times[ys, xs])`
  (i.e., only on first write per pixel)
- [x] Add `FuncItem._build_temporal_rgba()` — normalize `write_times` to [0,1] per-func,
  apply blue→yellow colormap, return RGBA uint8 array
- [x] Wire `_build_temporal_rgba` into `_update_pixmap` (add branch alongside `RECOMPUTE_HEAT`)
- [x] Add "Temporal &Order" `QAction` to View menu, shortcut Ctrl+T, checkable
- [x] Add `_toggle_temporal_mode(checked)` — sets `DisplayMode.TEMPORAL` or `VALUE`
  on all FuncItems and calls `_update_pixmap`
- [x] Reset `write_times` to NaN in `_init_data_array` (same place as `last_value` reset)
- [x] Uncheck the temporal action in `load_trace` (alongside the recompute action reset)

## Open questions

- Should TEMPORAL and RECOMPUTE_HEAT be mutually exclusive (as separate menu
  checkboxes that uncheck each other), or combined into a mode selector (radio
  buttons)? Right now they're independent toggles, but a radio-group might be
  cleaner once there are 3+ modes.
- For funcs computed in parallel, packet order reflects the trace serialization
  order which is non-deterministic across runs. This is fine — it shows *one*
  execution's order — but worth noting in a tooltip or status bar.
