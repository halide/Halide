"""
Interactive trace viewer using Qt.
"""

from __future__ import annotations

import time as _time
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path

import graphviz
import numpy as np
from halide import FuncStats, Trace, TracePacket
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import (
    QAction,
    QBrush,
    QColor,
    QFont,
    QImage,
    QKeySequence,
    QPainter,
    QPixmap,
    QShortcut,
    QWheelEvent,
)
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDockWidget,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGraphicsItem,
    QGraphicsPixmapItem,
    QGraphicsScene,
    QGraphicsSimpleTextItem,
    QGraphicsView,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QProgressDialog,
    QPushButton,
    QSlider,
    QSplitter,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)


class DisplayMode(IntEnum):
    VALUE = 0
    RECOMPUTE_HEAT = 1
    TEMPORAL = 2
    REUSE_DIST = 3
    LOAD_COUNT = 4


# Display channel index for each character of a FuncConfig.channel_order string.
_CHANNEL_SLOT = {"r": 0, "g": 1, "b": 2, "a": 3}

# Compact viridis control points (matplotlib viridis sampled at 0, 0.1, …, 1.0),
# linearly interpolated in _viridis. A debug colormap doesn't need full fidelity.
_VIRIDIS = np.array(
    [
        [68, 1, 84],
        [72, 33, 115],
        [64, 67, 135],
        [52, 94, 141],
        [41, 120, 142],
        [32, 144, 140],
        [34, 167, 132],
        [68, 190, 112],
        [121, 209, 81],
        [189, 222, 38],
        [253, 231, 37],
    ],
    dtype=np.float32,
)


def _normalize_values(
    vals: np.ndarray, min_v: float, max_v: float, mode: str
) -> np.ndarray:
    """Map raw values to t in [0, 1] under the given normalization mode.

    linear  — (v - min) / (max - min)
    log     — log1p over [min, max], for quantities spanning many magnitudes
    signed  — center 0 at t = 0.5 over [-A, A], A = max(|min|, |max|); for
              diverging data like the signed `remap` LUT
    """
    vals = vals.astype(np.float32)
    if mode == "log":
        lo = float(np.log1p(max(0.0, min_v)))
        hi = float(np.log1p(max(0.0, max_v)))
        if hi > lo:
            t = (np.log1p(np.clip(vals, 0.0, None)) - lo) / (hi - lo)
        else:
            t = np.zeros_like(vals)
    elif mode == "signed":
        a = max(abs(min_v), abs(max_v), 1e-12)
        t = 0.5 + 0.5 * (vals / a)
    else:  # linear
        if max_v > min_v:
            t = (vals - min_v) / (max_v - min_v)
        else:
            t = np.full_like(vals, 0.5)
    return np.clip(t, 0.0, 1.0)


def _viridis(t: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Interpolate the viridis control points for t in [0, 1]."""
    x = np.clip(t, 0.0, 1.0) * (len(_VIRIDIS) - 1)
    i0 = np.floor(x).astype(np.int32)
    i1 = np.minimum(i0 + 1, len(_VIRIDIS) - 1)
    f = (x - i0)[..., None]
    c = _VIRIDIS[i0] * (1.0 - f) + _VIRIDIS[i1] * f
    return (
        c[..., 0].astype(np.uint8),
        c[..., 1].astype(np.uint8),
        c[..., 2].astype(np.uint8),
    )


def _apply_colormap(
    t: np.ndarray, name: str
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Map normalized t in [0, 1] to (r, g, b) uint8 arrays via a scalar colormap.

    grayscale — r = g = b = t
    hot       — black → red → yellow → white
    diverging — blue → white → red (pair with the "signed" normalization)
    viridis   — perceptually uniform sequential
    """
    if name == "hot":
        r = (np.clip(3.0 * t, 0.0, 1.0) * 255.0).astype(np.uint8)
        g = (np.clip(3.0 * t - 1.0, 0.0, 1.0) * 255.0).astype(np.uint8)
        b = (np.clip(3.0 * t - 2.0, 0.0, 1.0) * 255.0).astype(np.uint8)
    elif name == "diverging":
        lower = t < 0.5
        r = np.where(lower, t * 2.0, 1.0)
        g = np.where(lower, t * 2.0, (1.0 - t) * 2.0)
        b = np.where(lower, 1.0, (1.0 - t) * 2.0)
        r = (np.clip(r, 0.0, 1.0) * 255.0).astype(np.uint8)
        g = (np.clip(g, 0.0, 1.0) * 255.0).astype(np.uint8)
        b = (np.clip(b, 0.0, 1.0) * 255.0).astype(np.uint8)
    elif name == "viridis":
        r, g, b = _viridis(t)
    else:  # grayscale
        v = (t * 255.0).astype(np.uint8)
        r = g = b = v
    return r, g, b


@dataclass
class FuncConfig:
    """Configuration for how a Func is displayed."""

    position: tuple[float, float] = (0.0, 0.0)
    zoom: float = 4.0
    min_value: float = 0.0
    max_value: float = 255.0
    # Channel interpretation. color_dim is the index of the Halide dimension
    # that acts as the color/channel axis (-1 = grayscale scalar). The two
    # remaining dims are spatial. channel_order maps channel index → display
    # channel ("rgb", "rgba", "bgr", …); channels beyond its length are ignored.
    color_dim: int = -1
    channel_order: str = "rgb"
    # Scalar (grayscale-interpretation) display controls; ignored when a color
    # dim is active. colormap ∈ {grayscale, hot, diverging, viridis};
    # normalization ∈ {linear, log, signed}.
    colormap: str = "grayscale"
    normalization: str = "linear"
    # Render a 1-D func as a value-vs-index line plot instead of a strip.
    plot_1d: bool = False
    visible: bool = True


@dataclass
class ViewerState:
    """State of the trace viewer."""

    trace: Trace | None = None
    func_configs: dict[str, FuncConfig] = field(default_factory=dict)
    current_time: int = -1  # Index into packets, -1 means nothing rendered yet
    playing: bool = False


class FuncItem(QGraphicsPixmapItem):
    """A draggable graphics item representing a Func's data."""

    def __init__(
        self,
        func_name: str,
        stats: FuncStats,
        config: FuncConfig,
        parent: QGraphicsItem | None = None,
    ):
        super().__init__(parent)
        self.func_name = func_name
        self.stats = stats
        self.config = config
        self.label = None  # Will be set by TraceCanvas.add_func
        self._dirty = False  # Track whether pixmap needs regeneration
        self.display_mode = DisplayMode.VALUE

        # Make it draggable
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsMovable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIsSelectable, True)
        self.setFlag(QGraphicsItem.GraphicsItemFlag.ItemSendsGeometryChanges, True)

        # Initialize the data array
        self._init_data_array()
        self._update_pixmap()

        # Create axis labels as child items (move with the func)
        self._create_axis_labels()

    def _spatial_dim_indices(self) -> tuple[int | None, int | None]:
        """Return the (x_dim, y_dim) coordinate indices for this func.

        The spatial axes are the first two dimensions that are not the color
        dimension. With color_dim == -1 this is (0, 1) — the default layout.
        For planar RGB f(x, y, c) (color_dim == 2) it stays (0, 1); for
        interleaved RGB f(c, x, y) (color_dim == 0) it becomes (1, 2).
        """
        ndims = len(self.stats.min_coords)
        cd = self.config.color_dim
        dims = [d for d in range(ndims) if d != cd]
        xd = dims[0] if len(dims) >= 1 else None
        yd = dims[1] if len(dims) >= 2 else None
        return xd, yd

    def _init_data_array(self):
        """Initialize the numpy array for storing Func values."""
        mn = self.stats.min_coords
        mx = self.stats.max_coords
        xd, yd = self._spatial_dim_indices()

        if xd is not None and xd < len(mn):
            width = mx[xd] - mn[xd]
            min_x = mn[xd]
        else:
            width = 1
            min_x = 0
        if yd is not None and yd < len(mn):
            height = mx[yd] - mn[yd]
            min_y = mn[yd]
        else:
            height = 1
            min_y = 0

        # Minimum coordinate of the color dimension, used to convert a raw
        # channel coordinate into a 0-based channel index. Prefer the clean
        # declared shape (realize_min); fall back to the aggregated min.
        cd = self.config.color_dim
        if cd >= 0:
            rm = self.stats.realize_min
            if cd < len(rm):
                self.color_dim_min = rm[cd]
            elif cd < len(mn):
                self.color_dim_min = mn[cd]
            else:
                self.color_dim_min = 0
        else:
            self.color_dim_min = 0

        # Clamp to reasonable sizes
        width = max(1, min(width, 4096))
        height = max(1, min(height, 4096))

        # RGBA array for displayed values
        self.data = np.zeros((height, width, 4), dtype=np.uint8)
        self.data[:, :, 3] = 255  # Fully opaque
        # Last raw value written to each pixel; NaN = never written
        self.last_value = np.full((height, width), np.nan, dtype=np.float32)
        # Redundant write count: incremented when the same value is written again
        self.counts = np.zeros((height, width), dtype=np.int32)
        # Relative store ordinal of the *first* write to each pixel; NaN = never
        # written. Drives the temporal-order heatmap. Subsequent rewrites
        # (recomputes) do not change this, keeping it orthogonal to `counts`.
        self.write_times = np.full((height, width), np.nan, dtype=np.float32)
        # Monotonic store counter for this func, used to stamp write_times.
        # Reset here so backward scrubs (which re-init) restart the ordering.
        self._write_time_base = 0
        # Mean reuse distance (store → first subsequent load, in packets) per
        # pixel; NaN = stored-but-never-read or never-stored. This is a static
        # whole-trace quantity populated from collect_reuse_distances, so it is
        # re-applied (not recomputed) after backward scrubs reset this array.
        self.reuse_dist = np.full((height, width), np.nan, dtype=np.float32)
        # Global (whole-pipeline) min/max reuse distance, used as a shared log
        # color scale so magnitude is comparable across funcs. Set by
        # _apply_reuse_distances; harmless defaults until then.
        self.reuse_norm_min = 0.0
        self.reuse_norm_max = 1.0
        # Per-pixel load count accumulated up to the current scrub time. Like
        # `data` it grows as the user scrubs forward (loads that have happened
        # so far) and is rebuilt from zero on backward scrubs, so the heatmap
        # tracks the scrub position. Input-only buffers (no stores) accumulate
        # here too, which is exactly where the interesting stencil footprints
        # live. Only populated while LOAD_COUNT mode is active (see
        # _accumulate_load_counts) to keep scrubbing fast otherwise.
        self.load_counts = np.zeros((height, width), dtype=np.int32)
        # This func's whole-trace max load count, used as a fixed log color
        # scale so brightness stays stable while scrubbing. Per-func (not
        # global) because load counts are dominated by boundary-clamp and
        # lookup-table outliers; a per-func scale lets each func use the full
        # ramp and makes its stencil footprint legible. Set by
        # _apply_load_count_scale; harmless default until then.
        self.load_count_norm_max = 1.0
        self.width = width
        self.height = height
        self.min_x = min_x
        self.min_y = min_y
        self._dirty = True  # Mark for pixmap regeneration

    def _build_heat_rgba(self) -> np.ndarray:
        """Build an RGBA heatmap from per-pixel redundant write counts.

        Never written         → black
        Written, no recompute → green  (counts == 0)
        Written, recomputed N times → red, log-scaled  (counts > 0)

        counts[y, x] is incremented only when the same raw value is written
        to (x, y) again — so fold_storage reuse and multi-channel writes
        (which write different values) are correctly ignored.
        """
        h, w = self.counts.shape
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255

        written = ~np.isnan(self.last_value)
        if not written.any():
            return rgba

        max_recomp = int(self.counts.max()) if self.counts.any() else 0
        if max_recomp > 0:
            denom = max(1.0, float(np.log1p(max_recomp)))
            t = (np.log1p(self.counts.astype(np.float32)) / denom).clip(0.0, 1.0)
        else:
            t = np.zeros((h, w), dtype=np.float32)

        # Interpolate green (0,200,0) → red (255,0,0) by t
        rgba[:, :, 0] = np.where(written, (t * 255.0).clip(0, 255), 0).astype(np.uint8)
        rgba[:, :, 1] = np.where(written, ((1.0 - t) * 200.0).clip(0, 200), 0).astype(
            np.uint8
        )
        rgba[:, :, 2] = 0
        return rgba

    def _build_temporal_rgba(self) -> np.ndarray:
        """Build an RGBA heatmap colored by when each pixel was first written.

        Never written → black. Otherwise the first-write ordinal is normalized
        per-func to [0, 1] and mapped blue (early) → yellow (late) via the
        linear ramp R=t, G=t, B=1-t. Per-func normalization keeps each func's
        own realization window legible rather than collapsing to a flat hue.
        """
        h, w = self.write_times.shape
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255

        written = ~np.isnan(self.write_times)
        if not written.any():
            return rgba

        wt = self.write_times[written]
        lo = float(wt.min())
        hi = float(wt.max())
        t = np.zeros((h, w), dtype=np.float32)
        if hi > lo:
            t[written] = (self.write_times[written] - lo) / (hi - lo)
        # If hi == lo (single distinct time) t stays 0 → uniform blue.

        ramp = (t * 255.0).clip(0, 255).astype(np.uint8)
        rgba[:, :, 0] = np.where(written, ramp, 0)
        rgba[:, :, 1] = np.where(written, ramp, 0)
        rgba[:, :, 2] = np.where(written, (255 - ramp), 0)
        return rgba

    def _build_reuse_rgba(self) -> np.ndarray:
        """Build an RGBA heatmap from per-pixel mean reuse distance.

        Never stored             → black
        Stored but never read    → gray (128, 128, 128)
        Stored and later read    → blue (short distance) → red (long distance)

        Distances span many orders of magnitude (compute_at ~10 packets vs.
        compute_root 10^6+), so they are log-scaled against a *global*
        (whole-pipeline) min→max range:
        (log1p(dist) - log1p(global_min)) / (log1p(global_max) - log1p(global_min)).

        A global scale (rather than per-func) is essential: per-func
        normalization makes every func saturate near red because the log-space
        median sits just below each func's own max. Sharing one scale lets a
        well-scheduled func with short distances actually read blue.
        """
        h, w = self.reuse_dist.shape
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255

        # write_times is set only on real stores (not by _apply_load_pixels), so
        # it — unlike last_value — distinguishes a stored pixel from an
        # input-buffer pixel that only has loads. Input buffers stay black.
        #
        # Gate everything on `stored` (current scrub position): reuse_dist is a
        # whole-trace quantity, so without this gate every read pixel would be
        # colored from t=0 and scrubbing would not change the image. Gating by
        # write_times reveals each pixel's reuse color only once it has actually
        # been computed at the current time.
        stored = ~np.isnan(self.write_times)
        has_reuse = stored & ~np.isnan(self.reuse_dist)
        # Stored but never read → gray. (Disjoint from has_reuse.)
        rgba[stored & ~has_reuse] = (128, 128, 128, 255)

        if has_reuse.any():
            lo = float(np.log1p(self.reuse_norm_min))
            hi = float(np.log1p(self.reuse_norm_max))
            t = np.zeros((h, w), dtype=np.float32)
            if hi > lo:
                t[has_reuse] = (np.log1p(self.reuse_dist[has_reuse]) - lo) / (hi - lo)
            # If hi == lo (all distances equal) t stays 0 → uniform blue.
            t = t.clip(0.0, 1.0)
            r = (t * 255.0).clip(0, 255).astype(np.uint8)
            b = ((1.0 - t) * 255.0).clip(0, 255).astype(np.uint8)
            rgba[:, :, 0] = np.where(has_reuse, r, rgba[:, :, 0])
            rgba[:, :, 1] = np.where(has_reuse, 0, rgba[:, :, 1])
            rgba[:, :, 2] = np.where(has_reuse, b, rgba[:, :, 2])
        return rgba

    def _build_load_count_rgba(self) -> np.ndarray:
        """Build an RGBA heatmap from per-pixel load counts.

        Never loaded (count == 0) → black. Otherwise the count is log-scaled
        against this func's whole-trace max and mapped through the classic
        "hot" ramp black → red → yellow → white, so hot spots (heavily re-read
        pixels — stencil centers, redundantly read compute_root buffers) glow
        bright. The footprint of a stencil kernel shows up as a structured
        bright region around each consumed pixel.

        The scale is each func's fixed whole-trace max (not the current
        accumulated max) so brightness does not jump around as the user
        scrubs; pixels simply fill in toward their final color. Per-func
        rather than global because load counts are dominated by outliers —
        a single boundary-clamped input pixel may be read 10^5+ times while a
        lookup table is read 10^5 times — which on a shared scale would crush
        every interior pipeline func into near-black. A per-func scale lets
        each func's stencil structure read clearly.
        """
        h, w = self.load_counts.shape
        rgba = np.zeros((h, w, 4), dtype=np.uint8)
        rgba[:, :, 3] = 255

        denom = float(np.log1p(self.load_count_norm_max))
        if denom <= 0.0 or not self.load_counts.any():
            return rgba

        t = (np.log1p(self.load_counts.astype(np.float32)) / denom).clip(0.0, 1.0)
        # "hot" colormap: R ramps over [0, 1/3], G over [1/3, 2/3], B over
        # [2/3, 1]. count == 0 → t == 0 → black, so no masking is needed.
        rgba[:, :, 0] = (np.clip(3.0 * t, 0.0, 1.0) * 255.0).astype(np.uint8)
        rgba[:, :, 1] = (np.clip(3.0 * t - 1.0, 0.0, 1.0) * 255.0).astype(np.uint8)
        rgba[:, :, 2] = (np.clip(3.0 * t - 2.0, 0.0, 1.0) * 255.0).astype(np.uint8)
        return rgba

    def _create_axis_labels(self):
        """Create axis coordinate labels as child items."""
        label_color = QBrush(QColor(150, 150, 150))
        rendered_w, rendered_h = self.get_rendered_size()
        xd, yd = self._spatial_dim_indices()

        # X-axis label: [min_x, max_x) below the image
        if xd is not None and xd < len(self.stats.min_coords):
            min_x = self.stats.min_coords[xd]
            max_x = self.stats.max_coords[xd]
            x_label_text = f"[{min_x}, {max_x})"
            self.x_axis_label = QGraphicsSimpleTextItem(x_label_text, self)
            self.x_axis_label.setBrush(label_color)
            # Center below the image
            label_width = self.x_axis_label.boundingRect().width()
            self.x_axis_label.setPos((rendered_w - label_width) / 2, rendered_h + 2)
        else:
            self.x_axis_label = None

        # Y-axis label: [min_y, max_y) to the left, rotated 90 degrees
        if yd is not None and yd < len(self.stats.min_coords):
            min_y = self.stats.min_coords[yd]
            max_y = self.stats.max_coords[yd]
            y_label_text = f"[{min_y}, {max_y})"
            self.y_axis_label = QGraphicsSimpleTextItem(y_label_text, self)
            self.y_axis_label.setBrush(label_color)
            self.y_axis_label.setRotation(-90)
            # Position to the left of the image, centered vertically
            label_height = (
                self.y_axis_label.boundingRect().width()
            )  # Width becomes height after rotation
            self.y_axis_label.setPos(-4, (rendered_h + label_height) / 2)
        else:
            self.y_axis_label = None

    def get_rendered_size(self) -> tuple[int, int]:
        """Calculate the actual rendered size based on zoom."""
        zoom = self.config.zoom
        h, w = self.data.shape[:2]

        if zoom >= 1:
            izoom = max(1, int(zoom))
            return w * izoom, h * izoom
        else:
            step = max(1, int(1 / zoom))
            return max(1, w // step), max(1, h // step)

    def _build_line_plot_rgba(self) -> np.ndarray:
        """Render a 1-D func as a value-vs-index plot (for LUTs like remap).

        Each column x plots its (normalized) value as a point; far more legible
        than a 1-pixel-tall grayscale strip. A faint zero axis is drawn when the
        normalization is signed. Rendered at native resolution and shown without
        zoom scaling (see _update_pixmap), since downsampling a plot is useless.
        """
        w = self.width
        plot_h = 80
        rgba = np.zeros((plot_h, w, 4), dtype=np.uint8)
        rgba[:, :, 0:3] = 20  # dark background
        rgba[:, :, 3] = 255

        if self.height == 1:
            vals = self.last_value[0]
        else:
            vals = np.nanmean(self.last_value, axis=0)
        valid = ~np.isnan(vals)
        if not valid.any():
            return rgba

        mn = self.config.min_value
        mx = self.config.max_value
        norm = self.config.normalization
        t = _normalize_values(np.nan_to_num(vals).astype(np.float32), mn, mx, norm)
        ys = np.clip(((1.0 - t) * (plot_h - 1)).astype(np.int32), 0, plot_h - 1)
        xs = np.arange(w)

        # Faint zero axis for signed/diverging data.
        if norm == "signed" and mn < 0.0 < mx:
            zt = _normalize_values(np.array([0.0], np.float32), mn, mx, norm)[0]
            zy = int(np.clip((1.0 - zt) * (plot_h - 1), 0, plot_h - 1))
            rgba[zy, :, 0:3] = 70

        rgba[ys[valid], xs[valid], 0] = 90
        rgba[ys[valid], xs[valid], 1] = 200
        rgba[ys[valid], xs[valid], 2] = 255
        return rgba

    def _update_pixmap(self):
        """
        Update the pixmap from the data array, or from a heatmap when in
        RECOMPUTE_HEAT / TEMPORAL display mode.
        """
        skip_scale = False
        if self.display_mode == DisplayMode.RECOMPUTE_HEAT:
            source = self._build_heat_rgba()
        elif self.display_mode == DisplayMode.TEMPORAL:
            source = self._build_temporal_rgba()
        elif self.display_mode == DisplayMode.REUSE_DIST:
            source = self._build_reuse_rgba()
        elif self.display_mode == DisplayMode.LOAD_COUNT:
            source = self._build_load_count_rgba()
        elif self.config.plot_1d and self.height == 1:
            source = self._build_line_plot_rgba()
            skip_scale = True  # plot is pre-sized; scaling would garble it
        else:
            source = self.data
        zoom = self.config.zoom
        h, w = source.shape[:2]

        if skip_scale:
            scaled = source
        elif zoom >= 1:
            # Scale up using repeat
            izoom = max(1, int(zoom))
            scaled = np.repeat(np.repeat(source, izoom, axis=0), izoom, axis=1)
        else:
            # Scale down using slicing (simple nearest-neighbor downscale)
            step = max(1, int(1 / zoom))
            scaled = source[::step, ::step, :]

        # Convert to QImage
        height, width = scaled.shape[:2]
        bytes_per_line = width * 4
        image = QImage(
            scaled.tobytes(),
            width,
            height,
            bytes_per_line,
            QImage.Format.Format_RGBA8888,
        )
        self.setPixmap(QPixmap.fromImage(image))

    def update_pixel(self, x: int, y: int, value: float, is_store: bool):
        """Update a single pixel value."""
        # Normalize coordinates
        px = x - self.min_x
        py = y - self.min_y

        if 0 <= px < self.width and 0 <= py < self.height:
            # Normalize value to 0-255
            min_v = self.config.min_value
            max_v = self.config.max_value
            if max_v > min_v:
                normalized = int(255 * (value - min_v) / (max_v - min_v))
                normalized = max(0, min(255, normalized))
            else:
                normalized = 128

            # Grayscale for now
            self.data[py, px, 0] = normalized
            self.data[py, px, 1] = normalized
            self.data[py, px, 2] = normalized
            self._dirty = True

    def refresh_pixmap(self):
        """Refresh the pixmap after batch updates, only if data changed."""
        if self._dirty:
            self._update_pixmap()
            self._dirty = False

    def itemChange(self, change, value):
        """Track position changes for config export."""
        if change == QGraphicsItem.GraphicsItemChange.ItemPositionHasChanged:
            pos = value
            self.config.position = (pos.x(), pos.y())
        return super().itemChange(change, value)


class TraceCanvas(QGraphicsView):
    """The main canvas for visualizing traces."""

    func_selected = Signal(str)  # Emitted when a func is clicked

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        # Surface clicks on a func to the display-properties panel.
        self.scene.selectionChanged.connect(self._on_selection_changed)

        # Enable scrollbars and smooth scrolling
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)

        # Background
        self.setBackgroundBrush(QBrush(QColor(30, 30, 30)))

        # Track func items and their labels
        self.func_items: dict[str, FuncItem] = {}
        self.func_labels: dict[str, QGraphicsItem] = {}

        # Zoom state
        self._zoom = 1.0
        self._min_zoom = 0.01
        self._max_zoom = 100.0

    def wheelEvent(self, event: QWheelEvent):
        """Handle wheel events: Ctrl/Cmd+scroll zooms, regular scroll pans."""
        # Check for Ctrl (or Cmd on macOS) modifier for zoom
        if event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            delta = event.angleDelta().y()
            if delta != 0:
                factor = 1.1 if delta > 0 else 1 / 1.1
                new_zoom = self._zoom * factor
                if self._min_zoom <= new_zoom <= self._max_zoom:
                    self._zoom = new_zoom
                    # Zoom toward mouse position
                    self.setTransformationAnchor(
                        QGraphicsView.ViewportAnchor.AnchorUnderMouse
                    )
                    self.scale(factor, factor)
                    self.setTransformationAnchor(
                        QGraphicsView.ViewportAnchor.AnchorViewCenter
                    )
            event.accept()
        else:
            # Regular scroll - let parent handle panning
            super().wheelEvent(event)

    def zoom_in(self):
        """Zoom in by a fixed factor."""
        factor = 1.25
        new_zoom = self._zoom * factor
        if new_zoom <= self._max_zoom:
            self._zoom = new_zoom
            self.scale(factor, factor)

    def zoom_out(self):
        """Zoom out by a fixed factor."""
        factor = 1 / 1.25
        new_zoom = self._zoom * factor
        if new_zoom >= self._min_zoom:
            self._zoom = new_zoom
            self.scale(factor, factor)

    def clear_funcs(self):
        """Remove all func items and labels."""
        for item in self.func_items.values():
            self.scene.removeItem(item)
        for label in self.func_labels.values():
            self.scene.removeItem(label)
        self.func_items.clear()
        self.func_labels.clear()

    def add_func(
        self, func_name: str, stats: FuncStats, config: FuncConfig
    ) -> FuncItem:
        """Add a Func visualization to the canvas."""
        item = FuncItem(func_name, stats, config)
        item.setPos(config.position[0], config.position[1])
        self.scene.addItem(item)
        self.func_items[func_name] = item

        # Add label as a child of the item so it moves with it.
        # ItemIgnoresTransformations keeps it at a fixed screen size when zoomed out.
        display_name = func_name.split(":")[-1] if ":" in func_name else func_name
        label = self.scene.addSimpleText(display_name)
        label.setParentItem(item)
        font = QFont()
        font.setPointSize(14)
        label.setFont(font)
        label.setFlag(QGraphicsItem.GraphicsItemFlag.ItemIgnoresTransformations)
        label.setBrush(QBrush(QColor(220, 220, 220)))
        label_h = label.boundingRect().height()
        label.setPos(0, -(label_h + 4))
        item.label = label
        self.func_labels[func_name] = label

        return item

    def get_func_item(self, func_name: str) -> FuncItem | None:
        """Get a func item by name."""
        return self.func_items.get(func_name)

    def _on_selection_changed(self):
        """Emit func_selected for the first selected FuncItem, if any."""
        for it in self.scene.selectedItems():
            if isinstance(it, FuncItem):
                self.func_selected.emit(it.func_name)
                return


class TimelineWidget(QWidget):
    """Timeline scrubber for navigating the trace."""

    time_changed = Signal(int)
    play_toggled = Signal(bool)  # True = playing, False = paused

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)

        self.play_button = QPushButton("Play")
        self.play_button.setCheckable(True)
        self.play_button.clicked.connect(self._on_play_clicked)
        layout.addWidget(self.play_button)

        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setMinimum(0)
        self.slider.setMaximum(0)
        self.slider.valueChanged.connect(self._on_slider_changed)
        layout.addWidget(self.slider, stretch=1)

        self.time_label = QLabel("0 / 0")
        self.time_label.setMinimumWidth(100)
        layout.addWidget(self.time_label)

    def set_range(self, max_time: int):
        """Set the maximum time value."""
        self.slider.setMaximum(max_time)
        self._update_label()

    def set_time(self, time: int):
        """Set the current time without emitting signal."""
        self.slider.blockSignals(True)
        self.slider.setValue(time)
        self.slider.blockSignals(False)
        self._update_label()

    def stop_playback(self):
        """Stop playback and reset button state."""
        self.play_button.setChecked(False)
        self.play_button.setText("Play")

    def _update_label(self):
        self.time_label.setText(f"{self.slider.value()} / {self.slider.maximum()}")

    def _on_slider_changed(self, value: int):
        self._update_label()
        self.time_changed.emit(value)

    def _on_play_clicked(self, checked: bool):
        self.play_button.setText("Pause" if checked else "Play")
        self.play_toggled.emit(checked)


class FuncListWidget(QWidget):
    """Sidebar widget showing list of Funcs."""

    func_visibility_changed = Signal(str, bool)

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)

        layout.addWidget(QLabel("Funcs"))

        self.list_widget = QListWidget()
        self.list_widget.itemChanged.connect(self._on_item_changed)
        layout.addWidget(self.list_widget)

    def set_funcs(self, funcs: dict[str, FuncStats]):
        """Populate the list with funcs."""
        self.list_widget.clear()
        for name, _stats in sorted(funcs.items()):
            short = name.split(":")[-1] if ":" in name else name
            item = QListWidgetItem(short)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked)
            item.setData(Qt.ItemDataRole.UserRole, name)  # full qualified name
            self.list_widget.addItem(item)

    def _on_item_changed(self, item: QListWidgetItem):
        name = item.data(Qt.ItemDataRole.UserRole)  # full qualified name
        visible = item.checkState() == Qt.CheckState.Checked
        self.func_visibility_changed.emit(name, visible)

    def update_recompute_stats(self, stats: dict[str, tuple[float, float]]):
        """Update item text with per-func recompute percentages and avg write counts."""
        self.list_widget.blockSignals(True)
        for i in range(self.list_widget.count()):
            item = self.list_widget.item(i)
            name = item.data(Qt.ItemDataRole.UserRole)
            short = name.split(":")[-1] if ":" in name else name
            if name in stats:
                pct, avg = stats[name]
                if pct > 0:
                    item.setText(f"{short}  ({pct:.0f}% recomp, +{avg:.2f}x)")
                    color = (
                        QColor(255, 100, 100) if pct >= 10 else QColor(255, 200, 100)
                    )
                    item.setForeground(QBrush(color))
                else:
                    item.setText(short)
                    item.setForeground(QBrush(QColor(200, 200, 200)))
            else:
                item.setText(short)
                item.setForeground(QBrush(QColor(200, 200, 200)))
        self.list_widget.blockSignals(False)


class DisplayPropertiesPanel(QDockWidget):
    """Dockable editor for the selected func's display properties.

    Bound to the canvas selection (TraceCanvas.func_selected). Edits the
    selected func's FuncConfig live and emits config_changed so the viewer can
    re-render. Only affects DisplayMode.VALUE; the heatmap modes are orthogonal.
    """

    # (func_name, needs_reinit): needs_reinit is True when the data array shape
    # may have changed (color-dim / interpretation edits) and the item must be
    # re-initialized before re-rendering.
    config_changed = Signal(str, bool)

    _INTERPRETATIONS = (
        "Grayscale",
        "Planar RGB",
        "Planar RGBA",
        "Interleaved RGB",
        "Interleaved RGBA",
    )
    _COLORMAPS = ("grayscale", "hot", "diverging", "viridis")
    _NORMALIZATIONS = ("linear", "log", "signed")

    def __init__(self, parent: QWidget | None = None):
        super().__init__("Display Properties", parent)
        self.setObjectName("DisplayPropertiesPanel")
        self._item: FuncItem | None = None
        self._loading = False  # suppress change signals while populating

        body = QWidget()
        form = QFormLayout(body)
        form.setContentsMargins(8, 8, 8, 8)

        self._name_label = QLabel("(no selection)")
        self._name_label.setWordWrap(True)
        form.addRow(self._name_label)

        self._interp = QComboBox()
        self._interp.addItems(self._INTERPRETATIONS)
        self._interp.currentIndexChanged.connect(self._on_interp_changed)
        form.addRow("Channels", self._interp)

        self._colormap = QComboBox()
        self._colormap.addItems(self._COLORMAPS)
        self._colormap.currentIndexChanged.connect(lambda _i: self._on_scalar_changed())
        form.addRow("Colormap", self._colormap)

        self._norm = QComboBox()
        self._norm.addItems(self._NORMALIZATIONS)
        self._norm.currentIndexChanged.connect(lambda _i: self._on_scalar_changed())
        form.addRow("Normalize", self._norm)

        self._min_spin = QDoubleSpinBox()
        self._max_spin = QDoubleSpinBox()
        for sb in (self._min_spin, self._max_spin):
            sb.setRange(-1e12, 1e12)
            sb.setDecimals(4)
            sb.setKeyboardTracking(False)
            sb.editingFinished.connect(self._on_range_changed)
        form.addRow("Min value", self._min_spin)
        form.addRow("Max value", self._max_spin)

        self._auto_btn = QPushButton("Auto from stats")
        self._auto_btn.clicked.connect(self._on_auto_range)
        form.addRow(self._auto_btn)

        self._plot_1d = QCheckBox("Line plot (1-D)")
        self._plot_1d.toggled.connect(self._on_scalar_changed)
        form.addRow(self._plot_1d)

        self.setWidget(body)
        self._set_enabled(False)

    def _set_enabled(self, on: bool):
        for w in (
            self._interp,
            self._colormap,
            self._norm,
            self._min_spin,
            self._max_spin,
            self._auto_btn,
            self._plot_1d,
        ):
            w.setEnabled(on)

    def set_func(self, item: FuncItem | None):
        """Populate the panel from a FuncItem's current config."""
        self._item = item
        if item is None:
            self._name_label.setText("(no selection)")
            self._set_enabled(False)
            return

        self._loading = True
        try:
            cfg = item.config
            short = item.func_name.split(":")[-1]
            ndims = len(item.stats.min_coords)
            self._name_label.setText(f"{short}  ({ndims}-D)")
            self._set_enabled(True)

            # Interpretation
            self._interp.setCurrentText(self._interp_label(cfg, ndims))
            self._colormap.setCurrentText(cfg.colormap)
            self._norm.setCurrentText(cfg.normalization)
            self._min_spin.setValue(float(cfg.min_value))
            self._max_spin.setValue(float(cfg.max_value))
            self._plot_1d.setChecked(bool(cfg.plot_1d))
            self._plot_1d.setEnabled(item.height == 1)
            # Scalar controls only matter when not in a color interpretation.
            scalar = cfg.color_dim < 0
            self._colormap.setEnabled(scalar)
            self._norm.setEnabled(True)
        finally:
            self._loading = False

    @staticmethod
    def _interp_label(cfg: FuncConfig, ndims: int) -> str:
        if cfg.color_dim < 0:
            return "Grayscale"
        rgba = len(cfg.channel_order) >= 4
        if cfg.color_dim == 0 and ndims >= 3:
            return "Interleaved RGBA" if rgba else "Interleaved RGB"
        return "Planar RGBA" if rgba else "Planar RGB"

    def _emit(self, needs_reinit: bool):
        if self._item is not None and not self._loading:
            self.config_changed.emit(self._item.func_name, needs_reinit)

    def _on_interp_changed(self, _idx: int):
        if self._item is None or self._loading:
            return
        cfg = self._item.config
        ndims = len(self._item.stats.min_coords)
        choice = self._interp.currentText()
        if choice == "Grayscale":
            cfg.color_dim = -1
        elif choice.startswith("Planar"):
            cfg.color_dim = max(0, ndims - 1)
            cfg.channel_order = "rgba" if choice.endswith("RGBA") else "rgb"
        else:  # Interleaved
            cfg.color_dim = 0
            cfg.channel_order = "rgba" if choice.endswith("RGBA") else "rgb"
        # Re-sync dependent control enablement.
        self._colormap.setEnabled(cfg.color_dim < 0)
        self._emit(needs_reinit=True)

    def _on_scalar_changed(self, *_args):
        if self._item is None or self._loading:
            return
        cfg = self._item.config
        cfg.colormap = self._colormap.currentText()
        cfg.normalization = self._norm.currentText()
        cfg.plot_1d = self._plot_1d.isChecked()
        self._emit(needs_reinit=False)

    def _on_range_changed(self):
        if self._item is None or self._loading:
            return
        cfg = self._item.config
        cfg.min_value = self._min_spin.value()
        cfg.max_value = self._max_spin.value()
        self._emit(needs_reinit=False)

    def _on_auto_range(self):
        if self._item is None:
            return
        st = self._item.stats
        lo = st.min_value if st.min_value is not None else 0.0
        hi = st.max_value if st.max_value is not None else 255.0
        if lo == hi:
            hi = lo + 1.0
        self._loading = True
        self._min_spin.setValue(float(lo))
        self._max_spin.setValue(float(hi))
        self._loading = False
        self._on_range_changed()


class TraceViewer(QMainWindow):
    """Main window for the trace viewer."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("neotrace - Halide Trace Viewer")
        self.resize(1600, 900)

        self.state = ViewerState()

        # Playback timer
        self._playback_timer = QTimer(self)
        self._playback_timer.timeout.connect(self._on_playback_tick)
        self._playback_step = 10000  # Packets per tick (increased for performance)

        # Scrub debounce — defer rendering until slider stops moving
        self._scrub_timer = QTimer(self)
        self._scrub_timer.setSingleShot(True)
        self._scrub_timer.setInterval(50)
        self._scrub_timer.timeout.connect(self._on_scrub_settled)
        self._pending_scrub_time: int = -1

        # Cache for func name lookups
        self._func_name_cache: dict[str, FuncItem | None] = {}
        # Cached packets list — trace.packets copies the C++ vector every access
        self._packets: list[TracePacket] = []
        # Cached load-pixel data for input funcs (no store events); populated
        # once per trace
        self._load_pixel_cache: dict = {}
        # Channel-aware load-pixel data per color_dim {cd: {func: (xs,ys,vals,cs)}}
        # for color input buffers; filled lazily and reused across renders.
        self._load_pixel_cache_cd: dict[int, dict] = {}
        # Cached whole-trace reuse distances {func: (xs, ys, mean_dist)};
        # populated once per trace, re-applied after backward scrubs
        self._reuse_dist_cache: dict = {}
        # Per-func whole-trace max load count {func: max} → fixed log color
        # scale for the load-count heatmap. Computed once per trace; the
        # per-pixel counts themselves are accumulated incrementally while the
        # mode is active.
        self._load_count_max_by_func: dict[str, float] = {}

        self._setup_ui()
        self._setup_menu()

    def _setup_ui(self):
        """Set up the UI components."""
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)

        # Splitter for sidebar and canvas
        splitter = QSplitter(Qt.Orientation.Horizontal)
        main_layout.addWidget(splitter, stretch=1)

        # Sidebar
        self.func_list = FuncListWidget()
        self.func_list.setMaximumWidth(250)
        self.func_list.func_visibility_changed.connect(self._on_func_visibility_changed)
        splitter.addWidget(self.func_list)

        # Canvas
        self.canvas = TraceCanvas()
        splitter.addWidget(self.canvas)

        splitter.setSizes([200, 1400])

        # Display-properties dock, bound to the canvas selection.
        self.props_panel = DisplayPropertiesPanel(self)
        self.props_panel.config_changed.connect(self._on_props_changed)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self.props_panel)
        self.canvas.func_selected.connect(self._on_func_selected)

        # Keyboard shortcuts for zoom
        zoom_in_shortcut = QShortcut(QKeySequence.StandardKey.ZoomIn, self)
        zoom_in_shortcut.activated.connect(self.canvas.zoom_in)
        zoom_in_shortcut2 = QShortcut(
            QKeySequence("Ctrl+="), self
        )  # For keyboards without +
        zoom_in_shortcut2.activated.connect(self.canvas.zoom_in)

        zoom_out_shortcut = QShortcut(QKeySequence.StandardKey.ZoomOut, self)
        zoom_out_shortcut.activated.connect(self.canvas.zoom_out)

        # Timeline
        self.timeline = TimelineWidget()
        self.timeline.time_changed.connect(self._on_time_changed)
        self.timeline.play_toggled.connect(self._on_play_toggled)
        main_layout.addWidget(self.timeline)

        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

    def _setup_menu(self):
        """Set up the menu bar."""
        menu_bar = self.menuBar()

        file_menu = menu_bar.addMenu("&File")
        file_menu.addAction("&Open...", self._open_file, "Ctrl+O")
        file_menu.addSeparator()
        file_menu.addAction("&Export Config...", self._export_config, "Ctrl+S")
        file_menu.addSeparator()
        file_menu.addAction("&Quit", self.close, "Ctrl+Q")

        view_menu = menu_bar.addMenu("&View")
        view_menu.addAction("&Reset Zoom", self._reset_zoom, "Ctrl+0")
        view_menu.addAction("&Fit All", self._fit_all, "Ctrl+F")
        view_menu.addSeparator()
        self._recompute_action = QAction("&Recompute Heatmap", self)
        self._recompute_action.setCheckable(True)
        self._recompute_action.setShortcut(QKeySequence("Ctrl+R"))
        self._recompute_action.triggered.connect(self._toggle_recompute_mode)
        view_menu.addAction(self._recompute_action)

        self._temporal_action = QAction("Temporal &Order", self)
        self._temporal_action.setCheckable(True)
        self._temporal_action.setShortcut(QKeySequence("Ctrl+T"))
        self._temporal_action.triggered.connect(self._toggle_temporal_mode)
        view_menu.addAction(self._temporal_action)

        self._reuse_action = QAction("Reuse &Distance", self)
        self._reuse_action.setCheckable(True)
        self._reuse_action.setShortcut(QKeySequence("Ctrl+U"))
        self._reuse_action.triggered.connect(self._toggle_reuse_mode)
        view_menu.addAction(self._reuse_action)

        self._load_count_action = QAction("&Load Count Heatmap", self)
        self._load_count_action.setCheckable(True)
        self._load_count_action.setShortcut(QKeySequence("Ctrl+L"))
        self._load_count_action.triggered.connect(self._toggle_load_count_mode)
        view_menu.addAction(self._load_count_action)

        view_menu.addSeparator()
        self._props_action = QAction("Display &Properties", self)
        self._props_action.setCheckable(True)
        self._props_action.setChecked(True)
        self._props_action.setShortcut(QKeySequence("Ctrl+P"))
        self._props_action.triggered.connect(
            lambda checked: self.props_panel.setVisible(checked)
        )
        # Keep the menu check in sync if the dock is closed via its title bar.
        self.props_panel.visibilityChanged.connect(self._props_action.setChecked)
        view_menu.addAction(self._props_action)

    def _open_file(self):
        """Open a trace file."""
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Trace File",
            "",
            "Trace Files (*.bin);;All Files (*)",
        )
        if path:
            self.load_trace(Path(path))

    def load_trace(self, path: Path):
        """Load a trace file and display it."""
        # Create progress dialog
        progress = QProgressDialog(f"Loading {path.name}...", "Cancel", 0, 100, self)
        progress.setWindowTitle("Loading Trace")
        progress.setMinimumDuration(500)  # Show after 500ms
        progress.setAutoClose(True)
        progress.setAutoReset(True)

        def on_progress(bytes_read: int, total_bytes: int):
            if progress.wasCanceled():
                raise InterruptedError("Loading cancelled")
            percent = int(100 * bytes_read / total_bytes) if total_bytes > 0 else 0
            progress.setValue(percent)
            QApplication.processEvents()

        try:
            trace = Trace.load(str(path), progress_callback=on_progress)
            progress.setValue(100)

            self.state.trace = trace
            self.state.current_time = -1  # Reset so first render processes all packets
            self._packets = (
                trace.packets
            )  # Cache once; avoids full C++ vector copy each access

            # Clear func name cache and load-pixel cache
            self._func_name_cache = {}
            self._load_pixel_cache_cd = {}
            load_pixels = trace.collect_load_pixels(0, len(self._packets))
            store_funcs = set(trace.collect_pixels(0, len(self._packets)).keys())
            # Only keep funcs that have NO store events — true input-only buffers.
            # Including funcs with stores would pre-populate last_value before their
            # stores arrive, making every subsequent store look like a redundant
            # recompute.
            self._load_pixel_cache = {
                k: v for k, v in load_pixels.items() if k not in store_funcs
            }
            # Whole-trace reuse distances (store → first subsequent load).
            self._reuse_dist_cache = trace.collect_reuse_distances(
                0, len(self._packets)
            )
            # Whole-trace load counts — only each func's max is kept, as a
            # fixed per-func color scale; the per-pixel counts are re-derived
            # incrementally while scrubbing (see _accumulate_load_counts).
            load_counts = trace.collect_load_counts(0, len(self._packets))
            self._load_count_max_by_func = {
                name: float(max(1, int(cs.max())))
                for name, (_xs, _ys, cs) in load_counts.items()
                if len(cs)
            }

            # Initialize func configs with auto-layout
            self._auto_layout()
            # Detect channel interpretation / colormaps from the declared shape.
            self._apply_detected_display_configs()

            # Update UI
            self.func_list.set_funcs(trace.funcs)
            self.timeline.set_range(len(self._packets) - 1)

            # Add func items to canvas
            self.canvas.clear_funcs()
            self._recompute_action.setChecked(False)
            self._temporal_action.setChecked(False)
            self._reuse_action.setChecked(False)
            self._load_count_action.setChecked(False)
            for name, stats in trace.funcs.items():
                config = self.state.func_configs.get(name)
                if config and config.visible:
                    self.canvas.add_func(name, stats, config)

            # Populate the static whole-trace reuse distances onto the items.
            self._apply_reuse_distances()
            # Stamp the fixed load-count color scale onto the items.
            self._apply_load_count_scale()

            # Don't render initially - let user scrub to desired time
            # This avoids the long initial render for large traces
            self.timeline.set_time(0)

            # Fit all funcs into view after the event loop has laid out the window
            QTimer.singleShot(0, self._fit_all)

            self.status_bar.showMessage(
                f"Loaded {path.name}: {len(self._packets)} packets, "
                f"{len(trace.funcs)} funcs"
            )
        except InterruptedError:
            self.status_bar.showMessage("Loading cancelled")
        except Exception as e:
            self.status_bar.showMessage(f"Error loading trace: {e}")
            raise

    def _calc_rendered_size(
        self, width: int, height: int, zoom: float
    ) -> tuple[int, int]:
        """Calculate actual rendered size matching FuncItem._update_pixmap logic."""
        if zoom >= 1:
            izoom = max(1, int(zoom))
            return width * izoom, height * izoom
        else:
            step = max(1, int(1 / zoom))
            return max(1, width // step), max(1, height // step)

    def _dag_layout(
        self, func_sizes: dict[str, tuple[int, int]]
    ) -> dict[str, tuple[float, float]] | None:
        """Compute layout positions using GraphViz based on DAG structure.

        Args:
            func_sizes: dict mapping func name to (rendered_width, rendered_height)

        Returns dict mapping func name to (x, y) position, or None if layout fails.
        """
        if not self.state.trace or not self.state.trace.dag_edges:
            return None

        trace = self.state.trace
        funcs = set(trace.funcs.keys())

        # GraphViz uses inches for dimensions (72 points = 1 inch)
        # We'll convert pixel sizes to inches for proper spacing
        DPI = 72.0

        try:
            dot = graphviz.Digraph(engine="dot")
            dot.attr(rankdir="LR")  # Left to right (inputs on left, outputs on right)
            dot.attr("node", shape="box")
            # Increase separation between nodes and ranks for clarity
            dot.attr("graph", nodesep="0.5", ranksep="1.0")

            # Use sanitized IDs to avoid GraphViz port syntax issues with colons
            # Map: sanitized_id -> original_name
            id_to_name: dict[str, str] = {}
            for name in funcs:
                # Replace colons with underscores for GraphViz node IDs
                node_id = name.replace(":", "_")
                id_to_name[node_id] = name
                short_name = name.split(":")[-1] if ":" in name else name

                # Tell GraphViz the actual size of each node so it can space properly
                if name in func_sizes:
                    pw, ph = func_sizes[name]
                    # Convert pixels to inches, add padding for labels
                    width_in = (pw + 20) / DPI
                    height_in = (ph + 30) / DPI  # Extra for label above
                else:
                    width_in = height_in = 1.0

                dot.node(
                    node_id,
                    short_name,
                    width=f"{width_in:.2f}",
                    height=f"{height_in:.2f}",
                    fixedsize="true",
                )

            for producer, consumers in trace.dag_edges.items():
                if producer in funcs:
                    producer_id = producer.replace(":", "_")
                    for consumer in consumers:
                        if consumer in funcs:
                            consumer_id = consumer.replace(":", "_")
                            dot.edge(producer_id, consumer_id)

            plain = dot.pipe(format="plain").decode("utf-8")

            positions: dict[str, tuple[float, float]] = {}

            for line in plain.split("\n"):
                parts = line.split()
                if len(parts) >= 5 and parts[0] == "node":
                    node_id = parts[1]
                    # GraphViz plain format: x and y are in inches, convert to pixels
                    x = float(parts[2]) * DPI
                    y = float(parts[3]) * DPI
                    # Map back to original name
                    if node_id in id_to_name:
                        positions[id_to_name[node_id]] = (x, y)

            return positions if positions else None
        except Exception as e:
            self.status_bar.showMessage(f"GraphViz layout failed: {e}")
            return None

    def _detect_display_config(
        self, stats: FuncStats
    ) -> tuple[int, str, str, str, bool]:
        """Infer sensible display defaults from a func's declared shape/values.

        Returns (color_dim, channel_order, colormap, normalization, plot_1d).

        Channel detection uses the clean declared extents (realize_extent),
        which — unlike min_coords/max_coords — are not polluted on the channel
        axis: a last (or first) dimension of extent 3/4 is taken as planar (or
        interleaved) RGB(A). Signed 1-D funcs default to a diverging line plot
        (e.g. the remap LUT).
        """
        ext = list(stats.realize_extent)
        if not ext:
            ext = [b - a for a, b in zip(stats.min_coords, stats.max_coords)]
        ndims = len(ext)

        # Only auto-detect extent-3 (RGB) as color. Extent 4 is ambiguous —
        # RGBA, but also a common pyramid/bin count (e.g. local_laplacian's
        # gPyramid has 4 intensity levels in its last dim) — so RGBA must be
        # opted into via the panel rather than guessed, to avoid colorizing
        # genuine k-slice funcs (those belong to channel decomposition).
        color_dim = -1
        channel_order = "rgb"
        if ndims >= 3:
            if ext[-1] == 3:
                color_dim = ndims - 1
            elif ext[0] == 3:
                color_dim = 0

        colormap = "grayscale"
        normalization = "linear"
        plot_1d = False
        if color_dim < 0:
            lo = stats.min_value if stats.min_value is not None else 0.0
            hi = stats.max_value if stats.max_value is not None else 0.0
            if lo < 0.0 < hi:
                colormap = "diverging"
                normalization = "signed"
            if ndims == 1:
                plot_1d = True
        return color_dim, channel_order, colormap, normalization, plot_1d

    def _apply_detected_display_configs(self):
        """Stamp detected display defaults onto every func's config."""
        if not self.state.trace:
            return
        for name, stats in self.state.trace.funcs.items():
            cfg = self.state.func_configs.get(name)
            if cfg is None:
                continue
            cd, order, cmap, norm, p1d = self._detect_display_config(stats)
            cfg.color_dim = cd
            cfg.channel_order = order
            cfg.colormap = cmap
            cfg.normalization = norm
            cfg.plot_1d = p1d

    def _auto_layout(self):
        """Automatically lay out funcs using DAG structure when available."""
        if not self.state.trace:
            return

        funcs = list(self.state.trace.funcs.items())
        n = len(funcs)
        if n == 0:
            return

        # Target cell size for layout
        target_cell_size = 250
        padding = 30
        label_height = 25

        # First pass: calculate zoom and rendered size for each func
        # Maps name -> (zoom, rendered_width, rendered_height, min_val, max_val)
        func_info: dict[str, tuple[float, int, int, float, float]] = {}
        func_sizes: dict[str, tuple[int, int]] = {}  # For GraphViz layout
        for name, stats in funcs:
            if stats.min_coords and stats.max_coords:
                width = max(1, stats.max_coords[0] - stats.min_coords[0])
                height = max(
                    1,
                    stats.max_coords[1] - stats.min_coords[1]
                    if len(stats.max_coords) > 1
                    else 1,
                )
                # Calculate zoom to fit in target cell (allow zoom < 1 for large funcs)
                available = target_cell_size - padding
                zoom_x = available / width
                zoom_y = available / height
                zoom = min(zoom_x, zoom_y)
                # Clamp zoom to reasonable range
                zoom = max(0.1, min(zoom, 8))
                # Calculate actual rendered size using same logic as FuncItem
                rendered_width, rendered_height = self._calc_rendered_size(
                    width, height, zoom
                )
            else:
                zoom = 4
                rendered_width, rendered_height = self._calc_rendered_size(1, 1, zoom)

            # Determine value range
            min_val = stats.min_value if stats.min_value is not None else 0.0
            max_val = stats.max_value if stats.max_value is not None else 255.0
            if min_val == max_val:
                max_val = min_val + 1

            func_info[name] = (zoom, rendered_width, rendered_height, min_val, max_val)
            func_sizes[name] = (rendered_width, rendered_height)

        # Try DAG-based layout first
        dag_positions = self._dag_layout(func_sizes)

        if dag_positions:
            # Use DAG positions directly - GraphViz already accounts for node sizes
            # Positions are node centers, so we offset to get top-left corner
            for name in func_info:
                zoom, rw, rh, min_val, max_val = func_info[name]
                if name in dag_positions:
                    cx, cy = dag_positions[name]
                    # Convert from center to top-left, add padding
                    px = cx - rw / 2 + padding
                    py = cy - rh / 2 + padding + label_height
                else:
                    # Func not in DAG, place at the end
                    px = padding
                    py = padding + label_height

                config = FuncConfig(
                    position=(px, py),
                    zoom=zoom,
                    min_value=min_val,
                    max_value=max_val,
                )
                self.state.func_configs[name] = config
        else:
            # Fallback to grid layout
            self._grid_layout(funcs, func_info, padding, label_height)

    def _grid_layout(
        self,
        funcs: list[tuple[str, FuncStats]],
        func_info: dict[str, tuple[float, int, int, float, float]],
        padding: int,
        label_height: int,
    ):
        """Lay out funcs in a simple grid."""
        n = len(funcs)
        cols = int(np.ceil(np.sqrt(n)))

        current_y = padding
        col = 0
        row_height = 0
        row_items: list[tuple[str, float, int, int, float, float]] = []

        for name, _stats in funcs:
            zoom, rw, rh, min_val, max_val = func_info[name]
            cell_height = rh + padding + label_height

            # Start new row if needed
            if col >= cols:
                # Position items in the completed row
                current_x = padding
                for (
                    item_name,
                    item_zoom,
                    item_rw,
                    _item_rh,
                    item_min,
                    item_max,
                ) in row_items:
                    config = FuncConfig(
                        position=(current_x, current_y + label_height),
                        zoom=item_zoom,
                        min_value=item_min,
                        max_value=item_max,
                    )
                    self.state.func_configs[item_name] = config
                    current_x += item_rw + padding

                current_y += row_height + padding
                row_items = []
                row_height = 0
                col = 0

            row_items.append((name, zoom, rw, rh, min_val, max_val))
            row_height = max(row_height, cell_height)
            col += 1

        # Position remaining items in last row
        current_x = padding
        for item_name, item_zoom, item_rw, _item_rh, item_min, item_max in row_items:
            config = FuncConfig(
                position=(current_x, current_y + label_height),
                zoom=item_zoom,
                min_value=item_min,
                max_value=item_max,
            )
            self.state.func_configs[item_name] = config
            current_x += item_rw + padding

    def _render_to_time(self, time: int):
        """Render the trace state at the given time index."""
        if not self.state.trace:
            return

        last_time = self.state.current_time

        self._prof_collect = 0.0
        self._prof_update_pixel = 0.0
        t0 = _time.perf_counter()

        # Determine rendering strategy
        if time > last_time:
            if last_time < 0:
                # Starting fresh: render from the beginning
                n_stores = self._render_range(0, time + 1)
            else:
                # Moving forward: incremental update
                n_stores = self._render_range(last_time + 1, time + 1)
        elif time < last_time:
            # Moving backward: must re-render from scratch
            for item in self.canvas.func_items.values():
                item._init_data_array()
            # _init_data_array cleared reuse_dist / load-count scale; re-apply
            # the static data.
            self._apply_reuse_distances()
            self._apply_load_count_scale()
            n_stores = self._render_range(0, time + 1)
        else:
            n_stores = 0

        # Load counts only accumulate while the heatmap is active, mirroring
        # the scrub position. The ranges below match the store strategy above:
        # forward extends incrementally; init and backward both start from a
        # zeroed load_counts (fresh items / _init_data_array), so they rescan
        # from 0; an unchanged time leaves the counts untouched (re-scanning
        # would double-count, since nothing re-zeroed them).
        if self._load_count_action.isChecked():
            if time > last_time:
                start = last_time + 1 if last_time >= 0 else 0
                self._accumulate_load_counts(start, time + 1)
            elif time < last_time:
                self._accumulate_load_counts(0, time + 1)

        t1 = _time.perf_counter()

        # Refresh all pixmaps
        for item in self.canvas.func_items.values():
            item.refresh_pixmap()

        self._apply_load_pixels()
        self._update_sidebar_stats()

        t2 = _time.perf_counter()

        self.state.current_time = time

        import sys

        if time > last_time:
            direction = "fwd" if last_time >= 0 else "init"
        elif time < last_time:
            direction = "bwd"
        else:
            direction = "="
        n_packets = time + 1 if direction in ("bwd", "init") else abs(time - last_time)
        cl = 1000 * self._prof_collect
        up = 1000 * self._prof_update_pixel
        msg = (
            f"[{direction}] packets={n_packets}  pixels={n_stores}  "
            f"process={1000 * (t1 - t0):.1f}ms "
            f"[collect={cl:.1f}ms  batch_write={up:.1f}ms]  "
            f"pixmap={1000 * (t2 - t1):.1f}ms  total={1000 * (t2 - t0):.1f}ms"
        )
        print(msg, file=sys.stderr)
        self.status_bar.showMessage(msg)

    # Profiling accumulators — reset each tick in _render_to_time
    _prof_collect: float = 0.0
    _prof_update_pixel: float = 0.0

    def _active_color_dims(self) -> list[int]:
        """Distinct color_dim values (>= 0) among the current func items."""
        dims = {
            item.config.color_dim
            for item in self.canvas.func_items.values()
            if item.config.color_dim >= 0
        }
        return sorted(dims)

    def _channel_slots(self, item: FuncItem, cs: np.ndarray) -> np.ndarray:
        """Map raw color-dim coordinates to RGBA slot indices (-1 = drop).

        channel_order[i] gives the display channel for channel index i; the
        index is the color coordinate minus the color dimension's min.
        """
        order = item.config.channel_order or "rgb"
        idx = cs - item.color_dim_min
        slots = np.full(len(cs), -1, dtype=np.int64)
        for i, ch in enumerate(order):
            slot = _CHANNEL_SLOT.get(ch, -1)
            if slot >= 0:
                slots[idx == i] = slot
        return slots

    def _apply_store_pixels(
        self,
        item: FuncItem,
        xs: np.ndarray,
        ys: np.ndarray,
        vals: np.ndarray,
        cs: np.ndarray | None = None,
    ) -> int:
        """Normalize + batch-write one func's stores into its data array.

        Routes through a scalar colormap (grayscale interpretation) or into a
        per-channel RGBA slot (color interpretation). The recompute / last_value
        / write_times bookkeeping stays channel-agnostic, keyed on (x, y), as
        the heatmap modes expect.
        """
        xs = xs - item.min_x
        ys = ys - item.min_y

        # Stamp each store with a monotonically increasing ordinal, in packet
        # order, before masking. Used for the temporal heatmap.
        times = np.arange(
            item._write_time_base, item._write_time_base + len(xs)
        ).astype(np.float32)
        item._write_time_base += len(xs)

        mask = (xs >= 0) & (xs < item.width) & (ys >= 0) & (ys < item.height)
        xs, ys = xs[mask], ys[mask]
        vals_m = vals[mask].astype(np.float32)
        times = times[mask]
        if not len(xs):
            return 0

        cfg = item.config
        t = _normalize_values(vals_m, cfg.min_value, cfg.max_value, cfg.normalization)
        if cs is not None and cfg.color_dim >= 0:
            slots = self._channel_slots(item, cs[mask])
            inten = (t * 255.0).astype(np.uint8)
            valid = slots >= 0
            if valid.any():
                item.data[ys[valid], xs[valid], slots[valid]] = inten[valid]
        else:
            r, g, b = _apply_colormap(t, cfg.colormap)
            item.data[ys, xs, 0] = r
            item.data[ys, xs, 1] = g
            item.data[ys, xs, 2] = b

        # Redundant only when the same value is written again. Different-value
        # overwrites (fold_storage reuse, multi-channel) are intentional.
        prev = item.last_value[ys, xs]
        is_recomp = ~np.isnan(prev) & (prev == vals_m)
        if is_recomp.any():
            np.add.at(item.counts, (ys[is_recomp], xs[is_recomp]), 1)
        item.last_value[ys, xs] = vals_m
        # Record first-write time only for pixels not yet stamped. Assign in
        # reverse so that for pixels written more than once within this batch
        # the earliest (smallest) time wins.
        first = np.isnan(item.write_times[ys, xs])
        if first.any():
            fi = np.where(first)[0][::-1]
            item.write_times[ys[fi], xs[fi]] = times[fi]
        item._dirty = True
        return len(xs)

    def _render_range(self, start: int, end: int) -> int:
        """Process packets in [start, end) and batch-write pixels.

        Returns total pixel count written.
        """
        if not self.state.trace:
            return 0

        end = min(end, len(self._packets))

        # Single C++ call: decode all store pixels in the range, grouped by func.
        ta = _time.perf_counter()
        pixel_data = self.state.trace.collect_pixels(start, end)
        self._prof_collect += _time.perf_counter() - ta

        # Batch-apply per func. Grayscale/scalar funcs use the default decode;
        # color funcs are handled in a second pass with their color_dim so the
        # channel coordinate is available and the spatial axes are correct.
        tp = _time.perf_counter()
        n_pixels = 0
        for func_name, (xs, ys, vals) in pixel_data.items():
            if len(xs) == 0:
                continue
            item = self._get_func_item_for_packet(func_name)
            if item is None or item.config.color_dim >= 0:
                continue
            n_pixels += self._apply_store_pixels(item, xs, ys, vals)

        for cd in self._active_color_dims():
            ta = _time.perf_counter()
            color_data = self.state.trace.collect_pixels(start, end, cd)
            self._prof_collect += _time.perf_counter() - ta
            for func_name, entry in color_data.items():
                xs, ys, vals, cs = entry
                if len(xs) == 0:
                    continue
                item = self._get_func_item_for_packet(func_name)
                if item is None or item.config.color_dim != cd:
                    continue
                n_pixels += self._apply_store_pixels(item, xs, ys, vals, cs)
        self._prof_update_pixel += _time.perf_counter() - tp
        return n_pixels

    def _set_display_mode(self, mode: DisplayMode, action: QAction, checked: bool):
        """Apply a display mode across all func items.

        Display mode is a single value, so the heatmap toggles are mutually
        exclusive: enabling one unchecks the others and falls back to VALUE when
        the active toggle is switched off.
        """
        if checked:
            for other in (
                self._recompute_action,
                self._temporal_action,
                self._reuse_action,
                self._load_count_action,
            ):
                if other is not action:
                    other.setChecked(False)
            new_mode = mode
        else:
            new_mode = DisplayMode.VALUE
        for item in self.canvas.func_items.values():
            item.display_mode = new_mode
            item._update_pixmap()

    def _toggle_recompute_mode(self, checked: bool):
        self._set_display_mode(
            DisplayMode.RECOMPUTE_HEAT, self._recompute_action, checked
        )

    def _toggle_temporal_mode(self, checked: bool):
        self._set_display_mode(DisplayMode.TEMPORAL, self._temporal_action, checked)

    def _toggle_reuse_mode(self, checked: bool):
        self._set_display_mode(DisplayMode.REUSE_DIST, self._reuse_action, checked)

    def _toggle_load_count_mode(self, checked: bool):
        # Load counts are only accumulated while the mode is active (see
        # _render_to_time), so on entering the mode rebuild them from scratch
        # up to the current scrub position.
        if checked:
            self._rebuild_load_counts(self.state.current_time)
        self._set_display_mode(DisplayMode.LOAD_COUNT, self._load_count_action, checked)

    def _load_pixels_for_cd(self, cd: int) -> dict:
        """Channel-aware whole-trace load pixels for a given color_dim, cached.

        Deterministic in cd, so it is computed once and reused across renders
        and config changes.
        """
        cache = self._load_pixel_cache_cd.get(cd)
        if cache is None and self.state.trace is not None:
            cache = self.state.trace.collect_load_pixels(0, len(self._packets), cd)
            self._load_pixel_cache_cd[cd] = cache
        return cache or {}

    def _write_load_pixels(
        self,
        item: FuncItem,
        xs: np.ndarray,
        ys: np.ndarray,
        vals: np.ndarray,
        cs: np.ndarray | None,
    ):
        """Write input-buffer load values into an item (no recompute tracking)."""
        xs_off = xs - item.min_x
        ys_off = ys - item.min_y
        mask = (
            (xs_off >= 0)
            & (xs_off < item.width)
            & (ys_off >= 0)
            & (ys_off < item.height)
        )
        xs_m, ys_m = xs_off[mask], ys_off[mask]
        vals_m = vals[mask].astype(np.float32)
        if not len(xs_m):
            return

        cfg = item.config
        t = _normalize_values(vals_m, cfg.min_value, cfg.max_value, cfg.normalization)
        if cs is not None and cfg.color_dim >= 0:
            slots = self._channel_slots(item, cs[mask])
            inten = (t * 255.0).astype(np.uint8)
            valid = slots >= 0
            if valid.any():
                item.data[ys_m[valid], xs_m[valid], slots[valid]] = inten[valid]
        else:
            r, g, b = _apply_colormap(t, cfg.colormap)
            item.data[ys_m, xs_m, 0] = r
            item.data[ys_m, xs_m, 1] = g
            item.data[ys_m, xs_m, 2] = b
        item.last_value[ys_m, xs_m] = vals_m  # counts stays zero (loads ≠ recompute)
        item._dirty = True

    def _apply_load_pixels(self):
        """
        Populate FuncItems for input-only buffers (no store events anywhere in
        the trace).

        The cache only contains funcs with zero stores, so there is no risk of
        pre-populating last_value before stores arrive and corrupting the
        redundant-recompute detection.  Called after every render so that
        backward scrubs (which reset last_value) are re-populated. Color input
        buffers (e.g. a planar RGB `input`) are routed per-channel using the
        channel-aware load cache for their color_dim.
        """
        for func_name, (xs, ys, vals) in self._load_pixel_cache.items():
            item = self._get_func_item_for_packet(func_name)
            if item is None:
                continue
            cd = item.config.color_dim
            if cd >= 0:
                entry = self._load_pixels_for_cd(cd).get(func_name)
                if entry is not None:
                    cxs, cys, cvals, ccs = entry
                    self._write_load_pixels(item, cxs, cys, cvals, ccs)
            else:
                self._write_load_pixels(item, xs, ys, vals, None)
            item.refresh_pixmap()

    def _apply_reuse_distances(self):
        """Stamp each FuncItem's reuse_dist array from the whole-trace cache.

        Reuse distance is a static property of the full execution, so it is
        applied once at load and re-applied after backward scrubs (which reset
        the per-item arrays in _init_data_array). Marks items dirty so the
        pixmap regenerates if the reuse-distance mode is active.
        """
        # Global min/max over every func's distances → shared color scale.
        global_min = np.inf
        global_max = 0.0
        for _name, (_xs, _ys, dists) in self._reuse_dist_cache.items():
            if len(dists):
                global_min = min(global_min, float(dists.min()))
                global_max = max(global_max, float(dists.max()))
        if not np.isfinite(global_min):
            global_min = 0.0

        for item in self.canvas.func_items.values():
            item.reuse_norm_min = global_min
            item.reuse_norm_max = global_max

        for func_name, (xs, ys, dists) in self._reuse_dist_cache.items():
            item = self._get_func_item_for_packet(func_name)
            if item is None:
                continue

            xs_off = xs - item.min_x
            ys_off = ys - item.min_y
            mask = (
                (xs_off >= 0)
                & (xs_off < item.width)
                & (ys_off >= 0)
                & (ys_off < item.height)
            )
            xs_m, ys_m = xs_off[mask], ys_off[mask]
            if len(xs_m):
                item.reuse_dist[ys_m, xs_m] = dists[mask].astype(np.float32)
                item._dirty = True

    def _apply_load_count_scale(self):
        """Set each item's fixed per-func load-count color scale.

        Each func's whole-trace max is computed once at load
        (`_load_count_max_by_func`) and stamped onto its item so the "hot"
        ramp uses a stable scale. Re-applied after backward scrubs, since
        _init_data_array resets it to the default.
        """
        for item in self.canvas.func_items.values():
            item.load_count_norm_max = 1.0
        for func_name, max_count in self._load_count_max_by_func.items():
            item = self._get_func_item_for_packet(func_name)
            if item is not None:
                item.load_count_norm_max = max_count

    def _accumulate_load_counts(self, start: int, end: int):
        """Add load counts for packets in [start, end) into each item.

        Each (x, y) appears once per func in the C++ result, so the in-place
        `+=` has no duplicate indices and correctly accumulates across the
        successive ranges produced by forward scrubbing.
        """
        if not self.state.trace or start >= end:
            return
        counts = self.state.trace.collect_load_counts(start, end)
        for func_name, (xs, ys, cs) in counts.items():
            if len(xs) == 0:
                continue
            item = self._get_func_item_for_packet(func_name)
            if item is None:
                continue
            xs_off = xs - item.min_x
            ys_off = ys - item.min_y
            mask = (
                (xs_off >= 0)
                & (xs_off < item.width)
                & (ys_off >= 0)
                & (ys_off < item.height)
            )
            xs_m, ys_m = xs_off[mask], ys_off[mask]
            if len(xs_m):
                item.load_counts[ys_m, xs_m] += cs[mask]
                item._dirty = True

    def _rebuild_load_counts(self, time: int):
        """Reset and recompute every item's load_counts for [0, time + 1].

        Used when entering the mode (which must establish the baseline at the
        current scrub position) and after backward scrubs.
        """
        for item in self.canvas.func_items.values():
            item.load_counts[:] = 0
        if time >= 0:
            self._accumulate_load_counts(0, time + 1)
        for item in self.canvas.func_items.values():
            item.refresh_pixmap()

    def _full_rerender(self):
        """Re-initialize every item and replay stores to the current time.

        Used when a display-property edit changes how stored values map to
        pixels (color interpretation, value range, colormap, normalization) —
        the per-pixel `data` array holds final colors, so it must be rebuilt.
        Mirrors the backward-scrub path in _render_to_time.
        """
        if not self.state.trace:
            return
        time = self.state.current_time
        for item in self.canvas.func_items.values():
            item._init_data_array()
        self._apply_reuse_distances()
        self._apply_load_count_scale()
        if time >= 0:
            self._render_range(0, time + 1)
        if self._load_count_action.isChecked():
            self._rebuild_load_counts(time)
        for item in self.canvas.func_items.values():
            item.refresh_pixmap()
        self._apply_load_pixels()
        self._update_sidebar_stats()

    def _on_func_selected(self, func_name: str):
        """Populate the display-properties panel from the clicked func."""
        self.props_panel.set_func(self.canvas.func_items.get(func_name))

    def _on_props_changed(self, func_name: str, needs_reinit: bool):
        """A display-property edit landed — rebuild the affected pixels.

        _full_rerender always re-initializes the data arrays, so it covers both
        shape changes (needs_reinit) and in-place recolors uniformly.
        """
        self._full_rerender()

    def _update_sidebar_stats(self):
        stats: dict[str, tuple[float, float]] = {}
        for func_name, item in self.canvas.func_items.items():
            written = int((~np.isnan(item.last_value)).sum())
            if written == 0:
                stats[func_name] = (0.0, 0.0)
                continue
            recomp_pixels = int((item.counts > 0).sum())
            pct = 100.0 * recomp_pixels / written
            # avg extra: total redundant writes / written pixels (0 = no waste)
            avg_extra = float(item.counts.sum()) / written
            stats[func_name] = (pct, avg_extra)
        self.func_list.update_recompute_stats(stats)

    def _get_func_item_for_packet(self, func_name: str) -> FuncItem | None:
        """Get the FuncItem for a packet's func name, with caching."""
        if func_name in self._func_name_cache:
            return self._func_name_cache[func_name]

        # collect_pixels returns bare names; canvas keys are qualified ("pkg:func").
        # Use exact-suffix matching on ":" to avoid "gPyramid_1" matching
        # "local_laplacian:gPyramid_0_clone_in_gPyramid_1$0" as a substring.
        suffix = f":{func_name}"
        for name, item in self.canvas.func_items.items():
            if name == func_name or name.endswith(suffix):
                self._func_name_cache[func_name] = item
                return item

        # Not found
        self._func_name_cache[func_name] = None
        return None

    def _on_time_changed(self, time: int):
        """
        Handle timeline scrubbing — debounced so only the settled position renders.
        """
        self._pending_scrub_time = time
        self._scrub_timer.start()  # restart resets the 50ms window

    def _on_scrub_settled(self):
        """
        Called 50ms after the last slider movement; renders the final position.
        """
        self._render_to_time(self._pending_scrub_time)

    def _on_play_toggled(self, playing: bool):
        """Handle play/pause toggle."""
        if playing:
            # Start playback - use 30ms interval for ~33 fps
            self._playback_timer.start(30)
            self.state.playing = True
        else:
            self._playback_timer.stop()
            self.state.playing = False

    def _on_playback_tick(self):
        """Advance playback by one step."""
        if not self.state.trace:
            return

        max_time = len(self._packets) - 1
        new_time = min(self.state.current_time + self._playback_step, max_time)

        if new_time >= max_time:
            # Reached the end - stop playback
            self._playback_timer.stop()
            self.state.playing = False
            self.timeline.stop_playback()

        self.timeline.set_time(new_time)
        self._render_to_time(new_time)

    def _on_func_visibility_changed(self, func_name: str, visible: bool):
        """Handle func visibility toggle."""
        if func_name in self.state.func_configs:
            self.state.func_configs[func_name].visible = visible
            # Show/hide the item and its label
            if func_name in self.canvas.func_items:
                self.canvas.func_items[func_name].setVisible(visible)
            if func_name in self.canvas.func_labels:
                self.canvas.func_labels[func_name].setVisible(visible)

    def _export_config(self):
        """Export the current layout configuration."""
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Export Configuration",
            "layout.yaml",
            "YAML Files (*.yaml *.yml);;All Files (*)",
        )
        if path:
            self._save_config(Path(path))

    def _save_config(self, path: Path):
        """Save configuration to a YAML file."""
        import yaml

        config = {
            "funcs": {
                name: {
                    "position": list(cfg.position),
                    "zoom": cfg.zoom,
                    "min_value": cfg.min_value,
                    "max_value": cfg.max_value,
                    "color_dim": cfg.color_dim,
                    "channel_order": cfg.channel_order,
                    "colormap": cfg.colormap,
                    "normalization": cfg.normalization,
                    "plot_1d": cfg.plot_1d,
                    "visible": cfg.visible,
                }
                for name, cfg in self.state.func_configs.items()
            }
        }
        with path.open("w") as f:
            yaml.dump(config, f, default_flow_style=False)
        self.status_bar.showMessage(f"Saved config to {path}")

    def _reset_zoom(self):
        """Reset canvas zoom to 100%."""
        self.canvas.resetTransform()
        self.canvas._zoom = 1.0

    def _fit_all(self):
        """Fit all items in view."""
        bounds = self.canvas.scene.itemsBoundingRect()
        self.canvas.fitInView(bounds, Qt.AspectRatioMode.KeepAspectRatio)


def run_viewer(trace_path: Path | None = None):
    """Run the interactive viewer."""
    app = QApplication.instance() or QApplication([])

    viewer = TraceViewer()
    viewer.show()

    if trace_path:
        viewer.load_trace(trace_path)

    return app.exec()
