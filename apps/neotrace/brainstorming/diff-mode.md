# Diff mode

**Status:** [ ] Not started\
**Priority:** Low\
**Needs new C++:** No\
**Keyboard shortcut:** Ctrl+D (pin), toggle diff overlay

## What it shows

A reference scrub position is "pinned." The viewer then highlights which pixels
changed between the pin and the current position. Unchanged pixels fade to gray;
changed pixels show their new value in full color with a colored border or tint.
Useful for step-debugging: "what did this one parallel tile compute?" or "what
changed between two scheduling decisions?"

## Design decisions

- **Pin mechanism:** Ctrl+D at current scrub position snapshots all
  `FuncItem.last_value` arrays into `FuncItem.pinned_value` (deep copy).
- **Diff rendering:** in a new `DisplayMode.DIFF = 5`, each pixel compares
  `last_value` vs. `pinned_value`. Pixels where `last_value == pinned_value` (or
  both NaN) are shown at 30% brightness; pixels that differ are shown at full
  brightness with a green tint.
- Unpinned pixels (NaN in `pinned_value` but written in `last_value`) are shown
  as bright blue — they were written after the pin.
- **Memory cost:** one extra `float32` array per FuncItem (same size as
  `last_value`). For a typical trace with ~30 funcs and 256×256 tiles, that's \<
  10 MB total.

## Implementation checklist

- [ ] Add `FuncItem.pinned_value` — `np.full(...)` initialized to NaN in `_init_data_array`
- [ ] Add `DisplayMode.DIFF = 5`
- [ ] Add `FuncItem._build_diff_rgba()` — three-way comparison: same, changed, new
- [ ] Add "Pin position" action (Ctrl+D) that deep-copies all `last_value` arrays into
  `pinned_value` and enables the diff toggle
- [ ] Add "Show &Diff" `QAction` in View menu, only enabled after a pin
- [ ] Reset `pinned_value` and disable diff action on `load_trace`

## Open questions

- Should pinning also record the packet index so the UI can display "pinned at
  packet N"?
- Diff mode interacts with RECOMPUTE_HEAT and TEMPORAL — should all modes be
  mutually exclusive (radio group) once there are 4+ of them?
