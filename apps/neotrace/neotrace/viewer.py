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
    QFileDialog,
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


@dataclass
class FuncConfig:
    """Configuration for how a Func is displayed."""

    position: tuple[float, float] = (0.0, 0.0)
    zoom: float = 4.0
    min_value: float = 0.0
    max_value: float = 255.0
    color_dim: int = -1  # -1 = grayscale, otherwise index of color dimension
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

    def _init_data_array(self):
        """Initialize the numpy array for storing Func values."""
        if len(self.stats.min_coords) >= 2:
            width = self.stats.max_coords[0] - self.stats.min_coords[0]
            height = self.stats.max_coords[1] - self.stats.min_coords[1]
        elif len(self.stats.min_coords) == 1:
            width = self.stats.max_coords[0] - self.stats.min_coords[0]
            height = 1
        else:
            width = height = 1

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
        self.width = width
        self.height = height
        self.min_x = self.stats.min_coords[0] if self.stats.min_coords else 0
        self.min_y = self.stats.min_coords[1] if len(self.stats.min_coords) > 1 else 0
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

    def _create_axis_labels(self):
        """Create axis coordinate labels as child items."""
        label_color = QBrush(QColor(150, 150, 150))
        rendered_w, rendered_h = self.get_rendered_size()

        # X-axis label: [min_x, max_x) below the image
        if self.stats.min_coords:
            min_x = self.stats.min_coords[0]
            max_x = self.stats.max_coords[0] if self.stats.max_coords else min_x + 1
            x_label_text = f"[{min_x}, {max_x})"
            self.x_axis_label = QGraphicsSimpleTextItem(x_label_text, self)
            self.x_axis_label.setBrush(label_color)
            # Center below the image
            label_width = self.x_axis_label.boundingRect().width()
            self.x_axis_label.setPos((rendered_w - label_width) / 2, rendered_h + 2)
        else:
            self.x_axis_label = None

        # Y-axis label: [min_y, max_y) to the left, rotated 90 degrees
        if len(self.stats.min_coords) > 1:
            min_y = self.stats.min_coords[1]
            max_y = (
                self.stats.max_coords[1]
                if len(self.stats.max_coords) > 1
                else min_y + 1
            )
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

    def _update_pixmap(self):
        """
        Update the pixmap from the data array (or heatmap if in RECOMPUTE_HEAT mode).
        """
        source = (
            self._build_heat_rgba()
            if self.display_mode == DisplayMode.RECOMPUTE_HEAT
            else self.data
        )
        zoom = self.config.zoom
        h, w = source.shape[:2]

        if zoom >= 1:
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
            load_pixels = trace.collect_load_pixels(0, len(self._packets))
            store_funcs = set(trace.collect_pixels(0, len(self._packets)).keys())
            # Only keep funcs that have NO store events — true input-only buffers.
            # Including funcs with stores would pre-populate last_value before their
            # stores arrive, making every subsequent store look like a redundant
            # recompute.
            self._load_pixel_cache = {
                k: v for k, v in load_pixels.items() if k not in store_funcs
            }

            # Initialize func configs with auto-layout
            self._auto_layout()

            # Update UI
            self.func_list.set_funcs(trace.funcs)
            self.timeline.set_range(len(self._packets) - 1)

            # Add func items to canvas
            self.canvas.clear_funcs()
            self._recompute_action.setChecked(False)
            for name, stats in trace.funcs.items():
                config = self.state.func_configs.get(name)
                if config and config.visible:
                    self.canvas.add_func(name, stats, config)

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
            n_stores = self._render_range(0, time + 1)
        else:
            n_stores = 0

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

        # Batch-apply per func: normalise + fancy-index write.
        tp = _time.perf_counter()
        n_pixels = 0
        for func_name, (xs, ys, vals) in pixel_data.items():
            if len(xs) == 0:
                continue
            item = self._get_func_item_for_packet(func_name)
            if item is None:
                continue

            xs = xs - item.min_x
            ys = ys - item.min_y

            min_v = item.config.min_value
            max_v = item.config.max_value
            if max_v > min_v:
                normalized = np.clip(
                    (255.0 * (vals - min_v) / (max_v - min_v)), 0, 255
                ).astype(np.uint8)
            else:
                normalized = np.full(len(xs), 128, dtype=np.uint8)

            mask = (xs >= 0) & (xs < item.width) & (ys >= 0) & (ys < item.height)
            xs, ys, normalized = xs[mask], ys[mask], normalized[mask]
            vals_m = vals[mask].astype(np.float32)
            if len(xs):
                item.data[ys, xs, 0] = normalized
                item.data[ys, xs, 1] = normalized
                item.data[ys, xs, 2] = normalized
                # Redundant only when the same value is written again.
                # Different-value overwrites (fold_storage reuse, multi-channel)
                # are intentional and excluded.
                prev = item.last_value[ys, xs]
                is_recomp = ~np.isnan(prev) & (prev == vals_m)
                if is_recomp.any():
                    np.add.at(item.counts, (ys[is_recomp], xs[is_recomp]), 1)
                item.last_value[ys, xs] = vals_m
                item._dirty = True
            n_pixels += len(xs)
        self._prof_update_pixel += _time.perf_counter() - tp
        return n_pixels

    def _toggle_recompute_mode(self, checked: bool):
        mode = DisplayMode.RECOMPUTE_HEAT if checked else DisplayMode.VALUE
        for item in self.canvas.func_items.values():
            item.display_mode = mode
            item._update_pixmap()

    def _apply_load_pixels(self):
        """
        Populate FuncItems for input-only buffers (no store events anywhere in
        the trace).

        The cache only contains funcs with zero stores, so there is no risk of
        pre-populating last_value before stores arrive and corrupting the
        redundant-recompute detection.  Called after every render so that
        backward scrubs (which reset last_value) are re-populated.
        """
        for func_name, (xs, ys, vals) in self._load_pixel_cache.items():
            item = self._get_func_item_for_packet(func_name)
            if item is None:
                continue

            xs_off = xs - item.min_x
            ys_off = ys - item.min_y
            min_v = item.config.min_value
            max_v = item.config.max_value
            if max_v > min_v:
                normalized = np.clip(
                    (255.0 * (vals - min_v) / (max_v - min_v)), 0, 255
                ).astype(np.uint8)
            else:
                normalized = np.full(len(xs_off), 128, dtype=np.uint8)

            mask = (
                (xs_off >= 0)
                & (xs_off < item.width)
                & (ys_off >= 0)
                & (ys_off < item.height)
            )
            xs_m, ys_m = xs_off[mask], ys_off[mask]
            normalized = normalized[mask]
            vals_m = vals[mask].astype(np.float32)
            if len(xs_m):
                item.data[ys_m, xs_m, 0] = normalized
                item.data[ys_m, xs_m, 1] = normalized
                item.data[ys_m, xs_m, 2] = normalized
                item.last_value[ys_m, xs_m] = vals_m
                # counts stays zero — loads are not redundant recomputes
                item._dirty = True
            item.refresh_pixmap()

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
