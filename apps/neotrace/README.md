# neotrace

Interactive trace visualization for Halide.

## Installation

```bash
cd apps/neotrace
pip install -e .
```

For video export support:

```bash
pip install -e ".[video]"
```

## Usage

### Interactive Viewer

```bash
# Open the viewer
neotrace view

# Open with a trace file
neotrace view path/to/trace.bin
```

### Trace Info

```bash
# Print summary of a trace file
neotrace info path/to/trace.bin

# Verbose output
neotrace info -v path/to/trace.bin
```

### Generating Traces

To generate a trace file from a Halide pipeline:

```bash
HL_TRACE_FILE=trace.bin HL_TARGET=host-trace_stores ./your_pipeline
```

Or trace all operations:

```bash
HL_TRACE_FILE=trace.bin HL_TARGET=host-trace_all ./your_pipeline
```

## Controls

- **Pan**: Click and drag, or use scroll bars
- **Zoom**: Mouse wheel
- **Select Func**: Click on a Func visualization
- **Move Func**: Drag a selected Func to reposition
- **Timeline**: Use the slider to scrub through the trace
- **Export Config**: File → Export Config to save your layout

## Development

```bash
pip install -e ".[dev]"
ruff check .
pytest
```

---

## Visualization Specification

### Data Dimensionality & Rendering Modes

Halide Funcs can have varying dimensionality. Neotrace supports multiple rendering modes:

| Dimensions    | Examples                  | Default Rendering         |
|---------------|---------------------------|---------------------------|
| 0D            | Scalar reduction          | Single value display      |
| 1D            | Histogram, LUT            | Line or wrapped rectangle |
| 2D            | Grayscale image           | Heatmap / grayscale       |
| 2D + channels | RGB/RGBA image            | Color image               |
| 3D            | Volume, video frame stack | Tiled 2D slices           |
| 4D+           | Batch of volumes          | Nested tiling             |

#### Rendering Modes

1. **Grayscale / Heatmap Mode** (1D, 2D)
    - Maps scalar values to color via configurable colormap
    - Colormaps: `grayscale`, `viridis`, `plasma`, `hot`, `cool`
    - Value range: configurable `[min_value, max_value]`

2. **RGB Mode** (2D + channel dimension)
    - Interprets one dimension as color channels (R, G, B, optionally A)
    - Channel dimension detected heuristically (dimension with extent 3 or 4)
    - Value ranges: `uint8` 0-255, `float32` 0.0-1.0 (configurable)

3. **Line Mode** (1D)
    - Renders 1D data as a horizontal or vertical line
    - Height/width configurable (default: 16px)

4. **Wrapped Mode** (1D)
    - Wraps 1D data into a 2D rectangle
    - Wrap width auto-computed to approximate square

5. **Projected Mode** (3D+)
    - Reduces higher dimensions by fixing indices
    - User specifies which indices to hold constant
    - Example: 4D `[batch, y, x, c]` with `batch=0` → RGB image

6. **Tiled Mode** (3D+)
    - Arranges slices in a grid
    - User specifies base visualization dims and tiling dims
    - Example: `[z, y, x]` → z slices arranged in grid

### Load & Store Visualization

- **Stores**: Solid color based on value (current behavior)
- **Loads**: Configurable visual treatment:
    - `outline`: Border around accessed pixels
    - `heatmap`: Overlay showing access frequency
    - `flash`: Brief highlight animation during playback

### Liveness Visualization

Funcs are visualized based on their liveness state:

| State      | Condition                         | Visual Treatment         |
|------------|-----------------------------------|--------------------------|
| **Unborn** | Current time < first store        | Grayed out (20% opacity) |
| **Alive**  | Between first store and last load | Full opacity             |
| **Dead**   | Current time > last load          | Faded (40% opacity)      |

### Axis Indicators

Each Func displays coordinate range labels:

- **X-axis**: `[min_x, max_x)` below the image
- **Y-axis**: `[min_y, max_y)` to the left (rotated)

### Memory Locality View (Future)

Specialized view for cache/memory access pattern analysis:

- **Address Heatmap**: Linearized memory with access frequency coloring
- **Access Timeline**: Time vs memory offset showing stride patterns
- **Liveness Bands**: Per-region liveness ranges

### Configuration Schema

```yaml
funcs:
    "pipeline:func_name":
        position: [ x, y ]
        zoom: 4.0
        visible: true

        # Value mapping
        min_value: 0.0
        max_value: 255.0
        colormap: "grayscale"

        # Dimensionality
        render_mode: "auto"    # auto, grayscale, rgb, line, wrapped, projected, tiled
        channel_dim: -1        # -1 = auto-detect, or explicit dimension index

        # For projected mode
        fixed_indices: { }      # e.g., {0: 5} to fix dim 0 at index 5

        # For tiled mode
        tile_dims: [ ]          # which dims to tile
        tile_layout: "auto"    # auto, or [rows, cols]

        # For 1D wrapped mode
        wrap_width: "auto"

        # Load visualization
        show_loads: true
        load_style: "outline"  # outline, heatmap, flash

        # Liveness
        liveness_mode: "fade"  # fade, hide, none
```
