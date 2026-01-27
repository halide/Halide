"""
Interactive trace viewer using Qt.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import graphviz
import numpy as np
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import (
    QBrush,
    QColor,
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

from halide import FuncStats, Trace, TracePacket


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

        # RGBA array
        self.data = np.zeros((height, width, 4), dtype=np.uint8)
        self.data[:, :, 3] = 255  # Fully opaque
        self.width = width
        self.height = height
        self.min_x = self.stats.min_coords[0] if self.stats.min_coords else 0
        self.min_y = self.stats.min_coords[1] if len(self.stats.min_coords) > 1 else 0
        self._dirty = True  # Mark for pixmap regeneration

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
        """Update the pixmap from the data array."""
        zoom = self.config.zoom
        h, w = self.data.shape[:2]

        if zoom >= 1:
            # Scale up using repeat
            izoom = max(1, int(zoom))
            scaled = np.repeat(np.repeat(self.data, izoom, axis=0), izoom, axis=1)
        else:
            # Scale down using slicing (simple nearest-neighbor downscale)
            step = max(1, int(1 / zoom))
            scaled = self.data[::step, ::step, :]

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

        # Add label as a child of the item so it moves with it
        display_name = func_name.split(":")[-1] if ":" in func_name else func_name
        label = self.scene.addSimpleText(display_name)
        label.setParentItem(item)  # Make label a child of the func item
        label.setPos(0, -20)  # Position relative to parent (above it)
        label.setBrush(QBrush(QColor(200, 200, 200)))
        item.label = label  # Store reference on item
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
        for name, stats in sorted(funcs.items()):
            item = QListWidgetItem(name)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked)
            item.setData(Qt.ItemDataRole.UserRole, stats)
            self.list_widget.addItem(item)

    def _on_item_changed(self, item: QListWidgetItem):
        name = item.text()
        visible = item.checkState() == Qt.CheckState.Checked
        self.func_visibility_changed.emit(name, visible)


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

        # Cache for func name lookups
        self._func_name_cache: dict[str, FuncItem | None] = {}

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

            # Clear func name cache
            self._func_name_cache = {}

            # Initialize func configs with auto-layout
            self._auto_layout()

            # Update UI
            self.func_list.set_funcs(trace.funcs)
            self.timeline.set_range(len(trace.packets) - 1)

            # Add func items to canvas
            self.canvas.clear_funcs()
            for name, stats in trace.funcs.items():
                config = self.state.func_configs.get(name)
                if config and config.visible:
                    self.canvas.add_func(name, stats, config)

            # Don't render initially - let user scrub to desired time
            # This avoids the long initial render for large traces
            self.timeline.set_time(0)

            self.status_bar.showMessage(
                f"Loaded {path.name}: {len(trace.packets)} packets, {len(trace.funcs)} funcs"
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

        # Determine rendering strategy
        if time > last_time and last_time >= 0:
            # Moving forward: incremental update
            self._render_range(last_time + 1, time + 1)
        elif time < last_time:
            # Moving backward: must re-render from scratch
            for item in self.canvas.func_items.values():
                item._init_data_array()
            self._render_range(0, time + 1)
        # else: time == last_time, nothing to do

        # Refresh all pixmaps
        for item in self.canvas.func_items.values():
            item.refresh_pixmap()

        self.state.current_time = time

    def _render_range(self, start: int, end: int):
        """Process packets in the given range [start, end)."""
        if not self.state.trace:
            return

        packets = self.state.trace.packets
        for i in range(start, min(end, len(packets))):
            packet = packets[i]
            if packet.is_store:
                self._process_store(packet)

    def _process_store(self, packet: TracePacket):
        """Process a store packet."""
        # Use cached lookup if available
        func_name = packet.func
        item = self._get_func_item_for_packet(func_name)
        if item is None:
            return

        values = packet.get_values()
        if not values:
            return

        dims_per_lane = (
            packet.dimensions // packet.type.lanes
            if packet.type.lanes > 0
            else packet.dimensions
        )

        for lane in range(packet.type.lanes):
            # Get coordinates for this lane
            if dims_per_lane >= 2:
                x = packet.coordinates[0 * packet.type.lanes + lane]
                y = packet.coordinates[1 * packet.type.lanes + lane]
            elif dims_per_lane == 1:
                x = packet.coordinates[lane]
                y = 0
            else:
                x = y = 0

            if lane < len(values):
                item.update_pixel(x, y, values[lane], is_store=True)

    def _get_func_item_for_packet(self, func_name: str) -> FuncItem | None:
        """Get the FuncItem for a packet's func name, with caching."""
        if func_name in self._func_name_cache:
            return self._func_name_cache[func_name]

        # Search for matching item
        for name, item in self.canvas.func_items.items():
            if func_name in name or name.endswith(f":{func_name}"):
                self._func_name_cache[func_name] = item
                return item

        # Not found
        self._func_name_cache[func_name] = None
        return None

    def _on_time_changed(self, time: int):
        """Handle timeline scrubbing."""
        self._render_to_time(time)

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

        max_time = len(self.state.trace.packets) - 1
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
