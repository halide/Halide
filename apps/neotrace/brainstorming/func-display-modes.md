# Func display modes & properties panel

**Status:** [ ] Not started\
**Priority:** High\
**Needs new C++:** Yes\
**Keyboard shortcut:** Ctrl+P (toggle the panel)

## What it shows

Today every func renders as collapsed grayscale: `_init_data_array` only uses
dimension 0 (width) and dimension 1 (height), and both `collect_pixels` and
`collect_load_pixels` decode just `x = coords[lane]` and
`y = coords[n_lanes + lane]`. Any further coordinate — the color channel of an
RGB image, the `k` intensity level of `gPyramid[j](x, y, k)` — is dropped, so
the third dimension collapses onto the same `(x, y)` with last-write-wins.

That is wrong for the buffers that matter most visually. In `local_laplacian`:

| Func          | Shape (from `begin_realization`)        | Type / value range      | Should display as          |
| ------------- | --------------------------------------- | ----------------------- | -------------------------- |
| `input`       | `(x:[0,192), y:[0,320), c:[0,3))`       | `uint16`, `[257,65535]` | Planar RGB                 |
| `output`      | `(x:[0,192), y:[0,320), c:[0,3))`       | `uint16`, `[0,65535]`   | Planar RGB                 |
| `remap`       | `(x:[-768,769))`                        | `float`, `[-0.20,0.20]` | 1-D signed LUT (diverging) |
| `gPyramid[j]` | `(x, y, k)` with several intensity bins | `float`, ~`[0,1]`       | per-`k` tiles (see below)  |

So both the input and the output of the pipeline are color images being shown in
grayscale, and `remap` is a signed lookup table being squashed into the default
`[0,255]` grayscale ramp.

This spec proposes (1) a per-func, selection-driven **display-properties panel**
for editing how each func is rendered, and (2) the **channel/colormap display
modes** it drives — chiefly planar and interleaved RGB, plus controls for
adapting to other buffer kinds like the `remap` LUT.

## The properties panel

A dockable `QDockWidget` ("Display Properties") on the right, bound to the
current selection:

- The canvas already emits `TraceCanvas.func_selected(str)` on click but nothing
  listens to it yet — wire it to the panel. Support multi-select later (apply
  edits to all selected `FuncItem`s).
- The panel reads/writes the selected func's `FuncConfig` and triggers a
  re-render (`item._init_data_array()` if the array shape changes, otherwise
  just `item._update_pixmap()`); edits are live.
- All controls persist through the existing config export/import path
  (`_export_config`), which already round-trips `color_dim` — see below.

## Display properties

### Channel interpretation

The core new control. A small enum on `FuncConfig` plus the dimension index:

- **Grayscale** (default, current behavior): collapse to dims 0/1, normalize a
  scalar through a ramp.
- **Planar RGB(A):** the color dimension is a *Halide dimension* (a separate
  coordinate), conventionally the outermost — `f(x, y, c)`, here `c = dim 2`,
  extent 3. Channel index *c* routes the value into R/G/B(/A).
- **Interleaved RGB(A):** the color dimension is the *innermost* coordinate —
  `f(c, x, y)`, `c = dim 0` — so `(x, y)` come from dims 1/2 instead. Same
  routing, different axis.

Both reduce to "one dimension is the channel axis; the others are spatial,"
which is why `FuncConfig.color_dim` (already present, currently unused and only
serialized) is the right hook. Generalize it to:

- `color_dim: int` — which coordinate is the channel axis (`-1` = grayscale).
- `channel_order: str` — e.g. `"rgb"`, `"rgba"`, `"bgr"`, mapping channel index
  → display channel. Channels beyond the mapping length are ignored; a missing
  alpha defaults to opaque.

### Value range & normalization

- Per-func `min_value` / `max_value` already exist and `_auto_layout` seeds them
  from `stats.min_value`/`max_value`. Expose them as editable spin boxes with an
  "auto from stats" reset, and a per-channel override option (color images often
  want a shared range; scientific buffers may not).
- A normalization mode: linear (current) vs. log vs. signed/diverging.

### Scalar colormaps (for non-RGB buffers)

For a func like `remap` — 1-D, signed, narrow range — grayscale on `[0,255]` is
useless. Add:

- A **diverging** colormap (blue → white → red) centered at 0 for signed data,
  with the range auto-set to `[-max(|lo|,|hi|), +…]`.
- A **1-D strip vs. line-plot** toggle: a 1×N func can render as a tall strip
  (current) or as an actual value-vs-index plot, which is far more legible for a
  LUT.
- A few standard sequential maps (viridis/magma) for unsigned scalar funcs,
  reusing the colormap helpers the heatmap modes already imply.

### Relationship to existing modes

The four heatmap modes (`RECOMPUTE_HEAT`, `TEMPORAL`, `REUSE_DIST`,
`LOAD_COUNT`) are orthogonal, pipeline-wide toggles that override the per-pixel
color. These display properties only affect `DisplayMode.VALUE`. The per-`(x,y)`
heatmap arrays (`counts`, `write_times`, `reuse_dist`, `load_counts`) stay
channel-agnostic — they already collapse channels, and the recompute detector
already treats multi-channel writes to one `(x, y)` as distinct values rather
than recomputes (see `_build_heat_rgba`). No change needed there.

Channel tiling (rendering each `k`-slice as a side-by-side tile) is a *layout*
concern handled separately in
[channel-decomposition.md](channel-decomposition.md); this spec is about
*interpreting* a dimension as color/colormap. The panel should host the "expand
k-slices" toggle from that doc too, since both are per-func display edits.

## Data model & rendering changes

1. `FuncConfig`: add `color_dim` semantics (already a field), `channel_order`,
   `colormap`, `normalization`, and a `plot_1d` flag.
2. `_init_data_array`: when a color dim is active, compute the spatial extents
   from the *non-color* dimensions and keep the display target as `H×W×4` RGBA
   (the channel axis folds into RGBA, not into the array shape).
3. **Routing the channel coordinate is the new-C++ part.** `collect_pixels`
   currently returns only `(xs, ys, vals)`; to fill separate channels we need
   the value of the color dimension per store. Add a variant (or extra return
   array) that also yields the color-dim coordinate, then in `_render_range`
   write `item.data[ys, xs, channel_of[c]] = normalized` instead of broadcasting
   to all three. Same extension for `collect_load_pixels` so input images show
   in color.
4. Channel detection heuristic for sensible defaults: a `begin_realization`
   packet carries clean `[min, extent]` pairs per dimension (e.g. `output` →
   `[0,192, 0,320, 0,3]`), which is the authoritative source for "the last
   dimension has extent 3 → default to planar RGB." Note `FuncStats.min_coords`
   /`max_coords` are unreliable for the channel axis (they aggregate raw
   coordinates and can be polluted, e.g. `input` reports a dim-2 max of 192).

## C++ method

Extend the store/load collectors to surface the channel coordinate. Either:

- Add `collect_pixels_nd(start, end, color_dim)` returning
  `{func: (xs, ys, cs, vals)}` where `cs` is the value of dimension `color_dim`
  per element; or
- Add an optional `color_dim` arg to `collect_pixels`/`collect_load_pixels`
  that, when `>= 0`, appends a `cs` array to the tuple.

The decode loop already computes `dims` and indexes `coords[lane]` /
`coords[n_lanes + lane]`; reading `coords[color_dim * n_lanes + lane]` for the
channel is a one-line addition. Spatial axes are then "the two dims that aren't
`color_dim`."

## Implementation checklist

- [ ] Add a dockable "Display Properties" `QDockWidget`; toggle with Ctrl+P
- [ ] Connect `TraceCanvas.func_selected` → populate the panel from the selected
  `FuncItem`'s `FuncConfig`
- [ ] Extend `FuncConfig`: `channel_order`, `colormap`, `normalization`,
  `plot_1d` (build on the existing `color_dim`); thread through export/import
- [ ] C++: surface the color-dim coordinate from `collect_pixels` /
  `collect_load_pixels` (new variant or optional arg) + binding
- [ ] `_init_data_array`: derive spatial extents from non-color dims when a color
  dim is set
- [ ] `_render_range` / `_apply_load_pixels`: route value into
  `data[..., channel]` by channel index
- [ ] Planar (outermost dim) and interleaved (innermost dim) presets
- [ ] Diverging colormap + auto signed range; wire up `remap`
- [ ] 1-D strip vs. line-plot toggle
- [ ] Default channel interpretation from `begin_realization` extents
  (last/first dim extent ∈ {3,4} → RGB/RGBA)
- [ ] Sidebar/menu affordance to open the panel; "auto from stats" range reset

## Open questions

- Does Halide already emit a trace tag describing channel layout (HalideTraceViz
  consumes layout/labels tags)? If so, prefer it over the extent heuristic. The
  `func_type_and_dim` tag is already parsed; check for an rgb/labels tag.
- Channel-order detection: is there any signal in the trace for interleaved vs.
  planar, or must the user pick? Default by which axis (first/last) has extent
  3–4, and let the panel override.
- Per-channel vs. shared value range as the default for RGB — shared is simpler
  and usually correct for image data.
- Should the panel also expose alpha/transparency so overlapping funcs on the
  canvas can be composited, or keep funcs fully opaque as today?
- For `> 4` channels (true `k`-pyramids), is colormap-per-slice + tiling (defer
  to [channel-decomposition.md](channel-decomposition.md)) the right split, or
  should this panel own slice selection (show channel *k* as grayscale)?
