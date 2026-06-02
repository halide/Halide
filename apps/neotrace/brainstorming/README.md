# Neotrace visualization ideas

| File                                                 | Idea                                                  | Priority | Status      | New C++? |
| ---------------------------------------------------- | ----------------------------------------------------- | -------- | ----------- | -------- |
| [temporal-order.md](temporal-order.md)               | Color pixels by when they were first written          | High     | Done        | No       |
| [reuse-distance.md](reuse-distance.md)               | Color pixels by write→first-read gap                  | High     | Done        | Yes      |
| [load-count-heatmap.md](load-count-heatmap.md)       | Color pixels by how often they are loaded             | Medium   | Done        | Yes      |
| [func-display-modes.md](func-display-modes.md)       | Per-func display panel: planar/interleaved RGB, LUTs  | High     | Done        | Yes      |
| [timeline-gantt.md](timeline-gantt.md)               | Gantt strip showing per-func active intervals         | Medium   | Not started | No       |
| [channel-decomposition.md](channel-decomposition.md) | Expand k-slices into side-by-side tiles               | Low      | Not started | No       |
| [diff-mode.md](diff-mode.md)                         | Highlight pixels that changed since a pinned position | Low      | Not started | No       |

## Useful commands

```bash
# After editing PyTrace.cpp — rebuild C++ bindings
uv sync --reinstall-package halide

# Run the viewer on the local_laplacian trace
uv run neotrace view local_laplacian.hltrace

# Run performance tests
uv run pytest tests/test_perf.py -v -s

# Run all tests
uv run pytest

# Run the recompute diagnostic script
uv run python diagnose_recomputes.py local_laplacian.hltrace [func_pattern ...]

# Regenerate the local_laplacian trace (from apps/neotrace)
# HL_TRACE_FILE opens in append mode — delete it first!
rm -f local_laplacian.hltrace
uv run cmake -G Ninja -S ../local_laplacian -B build -DCMAKE_BUILD_TYPE=Release
uv run cmake --build build
HL_TRACE_FILE=local_laplacian.hltrace \
	./build/process_viz ../images/rgb_small.png 4 1 1 0 output.png
```
