# Timeline / Gantt strip

**Status:** [ ] Not started\
**Priority:** Medium\
**Needs new C++:** No

## What it shows

A horizontal panel below the main canvas. The X-axis is packet index (0 → total
packets). Each func gets one row, colored blocks showing when
`begin_realization` and `end_realization` bracket its active intervals. Parallel
tiles produce multiple overlapping (or adjacent) blocks per func. The current
scrub position is shown as a vertical line.

Reveals: which funcs run concurrently, how wide parallel tiles are, the overall
pipeline execution shape, and why `compute_root` funcs have long idle gaps
between their realization and when consumers read them.

## Design decisions

- Data source: `trace.funcs` (`FuncStats`) already stores realization-bracket
  packet ranges. No new C++ needed for a basic version.
- Panel height: fixed ~120 px, one row per func (font-sized), scrollable
  vertically if there are many funcs.
- X-axis: linear in packet index. Time-axis zoom could be added later.
- Interaction: clicking a block selects that func in the sidebar.
- Scrub line: reuse the existing timeline slider position; draw a vertical
  overlay.

## Implementation checklist

- [ ] Add `GanttWidget(QWidget)` class in `viewer.py` (or a new `gantt.py` module)
- [ ] Populate from `trace.funcs` realization ranges after `load_trace`
- [ ] Add the widget to the main layout below `TraceCanvas`
- [ ] Connect scrub position signal so the vertical marker updates on seek
- [ ] Add "Show &Timeline" toggle in View menu

## Open questions

- Should the Gantt strip be in the same file as `viewer.py` or extracted to
  `gantt.py`? `viewer.py` is already large; a separate module may be cleaner.
- Do `FuncStats` realization ranges already cover parallel tiles as separate
  intervals, or just min/max over all tiles? If the latter, the Gantt will show
  a single merged block per func even for parallel execution.
