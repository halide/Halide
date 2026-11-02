#!/usr/bin/env python3
"""Regenerates the animated tutorial figures from Halide trace files.

Each lesson is run with tracing enabled (HL_JIT_TARGET=host-trace_all), and
the resulting trace is rendered into gifs and mp4s in this directory.

Values of the visualized Funcs are drawn in grayscale. Stores flash yellow
and loads flash blue, and the flashes decay over time. Time advances by the
Func's store or load cost per vector lane accessed, and a frame is emitted
every timestep units of time.

Figures of parallel schedules depend on thread timing, and the inputs in
lesson 18 are random, so those figures differ from run to run.

Requires numpy, Pillow, and ffmpeg (for the mp4s). The lessons are built with
the top-level Makefile, so LLVM_CONFIG etc. are taken from the environment.
"""

import argparse
import math
import mmap
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FIGURES_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(FIGURES_DIR))

# halide_trace_event_code_t
LOAD, STORE, BEGIN_REALIZATION, END_REALIZATION = 0, 1, 2, 3
BEGIN_PIPELINE, END_PIPELINE, TAG = 8, 9, 10

STORE_COLOR = (255, 221, 68)
LOAD_COLOR = (68, 221, 255)

# Labels are drawn in fixed-size character cells, with the bottom-left corner
# of the first cell at the label's position.
CHAR_WIDTH, CHAR_HEIGHT = 12, 32


def load_font():
    for path in ["/usr/share/fonts/truetype/inconsolata/Inconsolata.otf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"]:
        if os.path.exists(path):
            return ImageFont.truetype(path, 21)
    return ImageFont.load_default(21)


FONT = load_font()


@dataclass
class FuncLayout:
    """Where and how to draw one Func."""
    pos: tuple
    zoom: float = 1
    min: float = 0
    max: float = 1
    # Screen-space (x, y) step per unit of each dimension of the Func.
    strides: tuple = ((1, 0), (0, 1))
    # Time taken per vector lane stored to or loaded from the Func.
    store_cost: int = 1
    load_cost: int = 0
    # Color of newly-allocated storage, or None to leave the screen untouched.
    uninit: tuple = (0, 0, 0)
    # Whether to blank out storage when it is freed.
    blank: bool = False
    labels: list = field(default_factory=list)  # (text, pos, fade-in frames)


class Figure:
    """Renders one figure from the trace events of the Funcs added to it."""

    def __init__(self, output, size, timestep=1, decay=(1, 2), hold=4, gif_delay=None):
        self.output = output
        self.gif_delay = gif_delay
        self.width, self.height = size
        self.timestep = timestep
        # Each frame, the highlight of the accesses since the last frame has
        # its alpha divided by decay[0], and older highlights by decay[1].
        self.decay_during, self.decay_after = decay
        # The trace end is held for hold - 1 timesteps.
        self.hold = hold
        # Func name -> layout. A name without a pipeline prefix binds to the
        # first pipeline that contains a Func of that name.
        self.funcs = {}
        # Qualified (pipeline:func) name -> layout.
        self.bound = {}
        self.num_realizations = {}
        self.first_draw = set()

        h, w = self.height, self.width
        self.image = np.zeros((h, w, 3), np.uint8)
        self.anim = np.zeros((h, w, 4), np.uint8)
        self.anim_decay = np.zeros((h, w, 4), np.uint8)
        self.text = np.zeros((h, w, 4), np.uint8)
        self.halide_clock = 0
        self.video_clock = 0
        self.labels_being_drawn = []  # (text, pos, fade-in frames, first draw clock)
        self.frames = []

    def func(self, name, pos, label=None, label_pos=None, label_fade=1, **kwargs):
        """Draws the Func with the given name at pos. The label, if any, is
        drawn with its bottom-left corner at label_pos, which defaults to just
        above the Func."""
        layout = FuncLayout(tuple(pos), **kwargs)
        if label:
            if label_pos is None:
                label_pos = (pos[0], pos[1] - 8)
            layout.labels.append((label, tuple(label_pos), label_fade))
        self.funcs[name] = layout

    def bind_pipeline(self, pipeline, func_names):
        """Binds unqualified Func names to Funcs in a newly-begun pipeline.
        Returns whether this figure draws any Func in the pipeline."""
        relevant = False
        for f in func_names:
            qualified = f"{pipeline}:{f}"
            if qualified in self.bound:
                relevant = True
            elif qualified in self.funcs:
                self.bound[qualified] = self.funcs.pop(qualified)
                relevant = True
            elif f in self.funcs:
                self.bound[qualified] = self.funcs.pop(f)
                relevant = True
        return relevant

    def screen_box(self, cfg, x_off, y_off):
        x = int(x_off * cfg.zoom + cfg.pos[0])
        y = int(y_off * cfg.zoom + cfg.pos[1])
        return x, y

    def fill(self, dst, x, y, zoom, color):
        iz = math.ceil(zoom)
        x0, x1 = max(x, 0), min(x + iz, self.width)
        y0, y1 = max(y, 0), min(y + iz, self.height)
        if x0 < x1 and y0 < y1:
            dst[y0:y1, x0:x1] = color

    def fill_realization(self, cfg, coords, color):
        offsets = [(0, 0)]
        for d in range(len(coords) // 2):
            mn, extent = coords[2 * d], coords[2 * d + 1]
            sx, sy = cfg.strides[d] if d < len(cfg.strides) else (0, 0)
            offsets = [(ox + sx * c, oy + sy * c)
                       for ox, oy in offsets for c in range(mn, mn + extent)]
        for ox, oy in set(offsets):
            x, y = self.screen_box(cfg, ox, oy)
            self.fill(self.image, x, y, cfg.zoom, color)

    def draw_text(self, text, pos, gray):
        w = CHAR_WIDTH * len(text)
        cell = Image.new("L", (w, CHAR_HEIGHT), 0)
        draw = ImageDraw.Draw(cell)
        for c, ch in enumerate(text):
            draw.text((CHAR_WIDTH * c, CHAR_HEIGHT - 8), ch, fill=255, font=FONT, anchor="ls")
        alpha = np.asarray(cell)
        x, y = pos[0], pos[1] - CHAR_HEIGHT + 1
        x0, y0 = max(x, 0), max(y, 0)
        x1, y1 = min(x + w, self.width), min(y + CHAR_HEIGHT, self.height)
        if x0 >= x1 or y0 >= y1:
            return
        region = self.text[y0:y1, x0:x1]
        region[..., :3] = gray
        region[..., 3] = alpha[y0 - y:y1 - y, x0 - x:x1 - x]

    @staticmethod
    def composite(under, over):
        """Composites RGBA 'over' over 'under' (RGB or RGBA)."""
        a = over[..., 3:4].astype(np.uint32)
        u = under.astype(np.uint32)
        o = over.astype(np.uint32)
        out = u.copy()
        out[..., :3] = (a * o[..., :3] + (255 - a) * u[..., :3]) // 255
        if under.shape[-1] == 4:
            out[..., 3:4] = 255 - ((255 - u[..., 3:4]) * (255 - a)) // 255
        return out.astype(np.uint8)

    @staticmethod
    def decay(layer, factor):
        if factor != 1:
            inv = int((1 << 24) / factor)
            layer[..., 3] = (layer[..., 3].astype(np.uint64) * inv) >> 24

    def emit_frames(self):
        if self.halide_clock < self.video_clock:
            return
        while self.halide_clock >= self.video_clock:
            still_fading = []
            for label in self.labels_being_drawn:
                text, pos, fade_in, first_clock = label
                frames_since = (self.halide_clock - first_clock) // self.timestep
                if frames_since < fade_in:
                    gray = min(255, ((1 + frames_since) * 255) // max(1, fade_in))
                    still_fading.append(label)
                else:
                    gray = 255
                self.draw_text(text, pos, gray)
            self.labels_being_drawn = still_fading

            self.anim_decay = self.composite(self.anim_decay, self.anim)
            blend = self.composite(self.image, self.anim_decay)
            blend = self.composite(blend, self.text)
            self.frames.append(blend)

            self.video_clock += self.timestep
            self.decay(self.anim_decay, self.decay_after)
            self.decay(self.anim, self.decay_during)
        self.anim[:] = 0

    def handle(self, qualified, event, coords, lanes, values):
        cfg = self.bound[qualified]
        self.emit_frames()

        visible = event in (LOAD, STORE) or (event == BEGIN_REALIZATION and cfg.uninit is not None)
        if visible and qualified not in self.first_draw:
            self.first_draw.add(qualified)
            for text, pos, fade_in in cfg.labels:
                self.labels_being_drawn.append((text, pos, fade_in, self.halide_clock))

        if event in (LOAD, STORE):
            if event == STORE:
                self.halide_clock += cfg.store_cost * lanes
                anim_color = STORE_COLOR
            else:
                self.halide_clock += cfg.load_cost * lanes
                anim_color = LOAD_COLOR
            draw_value = event == STORE or self.num_realizations.get(qualified, 0) == 0
            dims = min(len(coords) // lanes, len(cfg.strides))
            z = cfg.zoom
            for lane in range(lanes):
                x, y = cfg.pos
                for d in range(dims):
                    c = coords[d * lanes + lane]
                    sx, sy = cfg.strides[d]
                    x = int(x + z * sx * c)
                    y = int(y + z * sy * c)
                if (x < 0 or y < 0 or x + z - 1 >= self.width or y + z - 1 >= self.height):
                    continue
                if draw_value:
                    v = 255.0 * (values[lane] - cfg.min) / (cfg.max - cfg.min)
                    gray = int(max(0.0, min(255.0, v)))
                    self.fill(self.image, x, y, z, (gray, gray, gray))
                self.fill(self.anim, x, y, z, anim_color + (255,))
        elif event == BEGIN_REALIZATION:
            self.num_realizations[qualified] = self.num_realizations.get(qualified, 0) + 1
            if cfg.uninit is not None:
                self.fill_realization(cfg, coords, cfg.uninit)
        elif event == END_REALIZATION and cfg.blank:
            self.fill_realization(cfg, coords, (0, 0, 0))

    def finish(self):
        self.emit_frames()
        for _ in range(self.hold - 1):
            self.halide_clock += self.timestep
            self.emit_frames()
        for name in self.funcs:
            print(f"warning: {self.output}: Func {name} not found in trace", file=sys.stderr)

    def save(self, output_dir):
        path = os.path.join(output_dir, self.output)
        if not self.frames:
            print(f"warning: {self.output}: no frames rendered", file=sys.stderr)
            return
        if path.endswith(".gif"):
            images = [Image.fromarray(f).quantize(256) for f in self.frames]
            images[0].save(path, save_all=True, append_images=images[1:],
                           duration=self.gif_delay * 10, loop=0, optimize=True)
        else:
            cmd = ["ffmpeg", "-loglevel", "error", "-y",
                   "-f", "rawvideo", "-pix_fmt", "rgb24",
                   "-s", f"{self.width}x{self.height}", "-r", "25", "-i", "-",
                   "-c:v", "libx264", "-pix_fmt", "yuv420p", path]
            proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
            for f in self.frames:
                proc.stdin.write(f.tobytes())
            proc.stdin.close()
            if proc.wait() != 0:
                raise RuntimeError(f"ffmpeg failed writing {path}")
        print(f"Wrote {path} ({len(self.frames)} frames)")


VALUE_FORMATS = {
    (0, 8): "b", (0, 16): "h", (0, 32): "i", (0, 64): "q",
    (1, 1): "B", (1, 8): "B", (1, 16): "H", (1, 32): "I", (1, 64): "Q",
    (2, 16): "e", (2, 32): "f", (2, 64): "d",
}


def decode_values(mm, off, type_code, bits, lanes):
    if type_code == 4 and bits == 16:  # bfloat16
        raw = struct.unpack_from(f"<{lanes}H", mm, off)
        return [struct.unpack("<f", struct.pack("<I", r << 16))[0] for r in raw]
    fmt = VALUE_FORMATS.get((type_code, bits))
    if fmt is None:
        return [0.0] * lanes
    return [float(v) for v in struct.unpack_from(f"<{lanes}{fmt}", mm, off)]


def read_name(mm, off):
    end = mm.find(b"\0", off)
    return mm[off:end].decode(), end + 1


def render_trace(trace_path, figures):
    """Feeds the trace through all the figures in a single pass. Pipelines that
    no figure draws from are skipped without decoding their packets."""
    header = struct.Struct("<IiiiBBHi")
    size_and_event = struct.Struct("<Ii")
    with open(trace_path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
        n = len(mm)
        off = 0
        pipelines = []  # stack of (id, name, interested figures)
        while off < n:
            size, event, parent_id, pid, type_code, bits, lanes, dims = header.unpack_from(mm, off)
            name_off = off + 24 + 4 * dims
            if event in (LOAD, STORE):
                name_off += lanes * ((bits + 7) // 8)

            if event == BEGIN_PIPELINE:
                pipeline, _ = read_name(mm, name_off)
                # The func_type_and_dim tags that follow name every Func in the pipeline.
                funcs = []
                tag_off = off + size
                while tag_off < n:
                    tag_size, tag_event = size_and_event.unpack_from(mm, tag_off)
                    if tag_event != TAG:
                        break
                    tag_dims = struct.unpack_from("<i", mm, tag_off + 20)[0]
                    funcs.append(read_name(mm, tag_off + 24 + 4 * tag_dims)[0])
                    tag_off += tag_size
                interested = [fig for fig in figures if fig.bind_pipeline(pipeline, funcs)]
                if interested:
                    pipelines.append((pid, pipeline, interested))
                else:
                    # Skip to the end of this pipeline.
                    off += size
                    while off < n:
                        size, event = size_and_event.unpack_from(mm, off)
                        off += size
                        if event == END_PIPELINE and struct.unpack_from("<i", mm, off - size + 8)[0] == pid:
                            break
                    continue
            elif event == END_PIPELINE:
                if pipelines and pipelines[-1][0] == parent_id:
                    pipelines.pop()
            elif pipelines and event in (LOAD, STORE, BEGIN_REALIZATION, END_REALIZATION, 4, 5, 6, 7):
                func, _ = read_name(mm, name_off)
                qualified = f"{pipelines[-1][1]}:{func}"
                targets = [fig for fig in pipelines[-1][2] if qualified in fig.bound]
                if targets:
                    coords = struct.unpack_from(f"<{dims}i", mm, off + 24)
                    values = None
                    if event in (LOAD, STORE):
                        values = decode_values(mm, off + 24 + 4 * dims, type_code, bits, lanes)
                    for fig in targets:
                        fig.handle(qualified, event, coords, lanes, values)
            off += size
    for fig in figures:
        fig.finish()


# Each lesson function is named after the tutorial it traces, and returns the
# figures to render from that trace.

# The figures for lessons 5, 8, and 9 don't draw newly-allocated storage.
OLD_STYLE = dict(uninit=None)


def lesson_05_scheduling_1():
    figures = []
    for output, func, size, max_value, store_cost, hold, delay in [
        ("lesson_05_row_major.gif", "gradient", (192, 192), 6, 2, 4, 10),
        ("lesson_05_col_major.gif", "gradient_col_major", (192, 192), 6, 2, 4, 10),
        ("lesson_05_vectors.gif", "gradient_in_vectors", (320, 192), 11, 1, 4, 10),
        ("lesson_05_split_7_by_3.gif", "gradient_split_7x2", (288, 128), 9, 2, 4, 10),
        ("lesson_05_tiled.gif", "gradient_tiled", (320, 320), 14, 1, 10, 8),
        ("lesson_05_parallel_tiles.gif", "gradient_fused_tiles", (320, 320), 14, 1, 4, 8),
    ]:
        fig = Figure(output, size, decay=(256, 256), hold=hold, gif_delay=delay)
        fig.func(func, (32, 32), zoom=32, max=max_value, store_cost=store_cost, **OLD_STYLE)
        figures.append(fig)

    fig = Figure("lesson_05_fast.mp4", (700, 500), timestep=1000, decay=(256, 2), hold=30)
    fig.func("gradient_fast", (1, 1), zoom=2, max=600, **OLD_STYLE)
    figures.append(fig)
    return figures


def lesson_08_scheduling_2():
    figures = []
    for name, suffix, zoom, blank, hold, delay in [
        ("compute_root", "root", 32, False, 4, 15),
        ("compute_y", "y", 32, True, 4, 20),
        ("store_root_compute_y", "root_y", 32, False, 4, 20),
        ("store_root_compute_x", "root_x", 32, False, 4, 20),
        ("tile", "tile", 16, True, 10, 10),
    ]:
        fig = Figure(f"lesson_08_{name}.gif", (400, 236), decay=(256, 256), hold=hold,
                     gif_delay=delay)
        common = dict(zoom=zoom, min=-1, max=1, **OLD_STYLE)
        fig.func(f"producer_{suffix}", (32, 48), label="producer", blank=blank, **common)
        fig.func(f"consumer_{suffix}", (240, 64), label="consumer", label_pos=(240, 40),
                 **common)
        figures.append(fig)

    fig = Figure("lesson_08_mixed.mp4", (800, 400), timestep=200, decay=(256, 3), hold=30)
    common = dict(zoom=2, min=-1.5, max=1.5, **OLD_STYLE)
    fig.func("producer_mixed", (40, 48), label="producer", blank=True, **common)
    fig.func("consumer_mixed", (440, 48), label="consumer", **common)
    figures.append(fig)
    return figures


def lesson_09_update_definitions():
    # Most Funcs in this lesson are unnamed, so they are referred to by the
    # names Halide generates for them, in order of construction.
    figures = []
    fig = Figure("lesson_09_update.gif", (192, 192), decay=(256, 256), hold=10, gif_delay=15)
    fig.func("g", (32, 32), zoom=32, max=10, store_cost=2, **OLD_STYLE)
    figures.append(fig)

    fig = Figure("lesson_09_update_rdom.mp4", (364, 364), timestep=100, decay=(256, 2), hold=40)
    fig.func("f1", (32, 32), zoom=3, max=2, **OLD_STYLE)
    figures.append(fig)

    fig = Figure("lesson_09_update_schedule.mp4", (320, 320), decay=(256, 2), hold=40)
    fig.func("f2", (32, 32), zoom=16, min=-20, max=200, **OLD_STYLE)
    figures.append(fig)

    # Producer above consumer, both one-dimensional.
    for name, producer, consumer, producer_max, consumer_max, store_cost, width, delay in [
        ("inline_reduction", "f3", "f4", 60, 60, 2, 384, 15),
        ("compute_at_pure", "f5", "f6", 340, 340, 1, 384, 15),
        ("compute_at_update", "f7", "f8", 200, 200, 1, 384, 15),
        ("compute_at_pure_and_update", "f9", "f10", 150, 170, 1, 384, 15),
        ("compute_at_rvar", "f16", "f17", 16, 50, 1, 544, 10),
    ]:
        fig = Figure(f"lesson_09_{name}.gif", (width, 144), decay=(256, 256), hold=5,
                     gif_delay=delay)
        common = dict(zoom=32, strides=((1, 0),), **OLD_STYLE)
        fig.func(producer, (32, 32), max=producer_max, store_cost=store_cost,
                 label="producer", label_pos=(32, 32), **common)
        fig.func(consumer, (32, 96), max=consumer_max,
                 label="consumer", label_pos=(32, 96), **common)
        figures.append(fig)

    fig = Figure("lesson_09_compute_at_multiple_updates.mp4", (576, 304),
                 decay=(256, 256), hold=40)
    common = dict(zoom=24, **OLD_STYLE)
    # producer_1, producer_2, and consumer_2
    fig.func("f13", (32, 32), max=15, label="producer", label_pos=(32, 32), **common)
    fig.func("f14", (32, 32), max=15, **common)
    fig.func("f15", (304, 32), max=20, label="consumer", label_pos=(304, 32), **common)
    figures.append(fig)
    return figures


def lesson_17_predicated_rdom():
    figures = []
    fig = Figure("lesson_17_rdom_circular.mp4", (236, 236), decay=(1, 256), hold=30)
    fig.func("circle", (13, 13), zoom=30, max=20, store_cost=2)
    figures.append(fig)

    fig = Figure("lesson_17_rdom_triangular.mp4", (236, 236), decay=(1, 256), hold=30)
    fig.func("triangle", (18, 18), zoom=20, max=32, store_cost=2)
    figures.append(fig)

    fig = Figure("lesson_17_rdom_calls_in_predicate.mp4", (400, 236), decay=(1, 256), hold=60)
    common = dict(zoom=32, min=-1, max=13, store_cost=3, load_cost=2)
    fig.func("f", (20, 48), label="f", label_pos=(32, 40), **common)
    fig.func("g", (228, 48), label="g", label_pos=(240, 40), **common)
    figures.append(fig)
    return figures


def lesson_18_parallel_associative_reductions():
    common = dict(zoom=20, store_cost=2, uninit=(50, 50, 100))
    figures = []
    fig = Figure("lesson_18_hist_serial.mp4", (230, 280), decay=(2, 4), hold=10)
    fig.func("hist_serial:input", (10, 40), max=256, label="Input", **common)
    fig.func("hist_serial", (10, 246), max=25, strides=((1, 0),), label="Histogram", **common)
    figures.append(fig)

    for name, final, intm, size, intm_strides, hist_y in [
        ("hist_manual_par", "merge_par_manual", "intm_par_manual", (460, 300),
         ((1, 0), (0, 1)), 246),
        ("hist_rfactor_par", "hist_rfactor_par", "hist_rfactor_par_intm", (460, 300),
         ((1, 0), (0, 1)), 246),
        ("hist_rfactor_vec", "hist_rfactor_vec", "hist_rfactor_vec_intm", (460, 300),
         ((1, 0), (0, 1)), 246),
        ("hist_rfactor_tile", "hist_rfactor_tile", "hist_rfactor_tile_intm", (650, 200),
         ((1, 0), (11, 0), (0, 2)), 158),
    ]:
        fig = Figure(f"lesson_18_{name}.mp4", size, decay=(2, 4), hold=10)
        fig.func(f"{final}:input", (20, 40), max=256, label="Input", **common)
        fig.func(intm, (230, 40), max=4, strides=intm_strides, label="Partial Histograms",
                 **common)
        fig.func(final, (230, hist_y), max=10, strides=((1, 0),), label="Histogram", **common)
        figures.append(fig)
    return figures


def lesson_19_wrapper_funcs():
    figures = []
    fig = Figure("lesson_19_wrapper_local.mp4", (412, 172), decay=(1, 256), hold=20)
    common = dict(zoom=20, max=16, store_cost=2)
    fig.func("g_local:f_local", (24, 48), label="f", **common)
    fig.func("g_local:f_local_in_g_local$0", (156, 48), label="f.in(g)", label_fade=5, **common)
    fig.func("g_local", (288, 48), label="g", **common)
    figures.append(fig)

    fig = Figure("lesson_19_wrapper_global.mp4", (544, 172), decay=(1, 256), hold=20)
    fig.func("h_global:f_global", (24, 48), label="f", **common)
    fig.func("h_global:f_global_in$0", (156, 48), label="f.in()", label_fade=5,
             **common)
    fig.func("h_global:g_global", (288, 48), label="g", **common)
    fig.func("h_global", (420, 48), label="h", **common)
    figures.append(fig)

    fig = Figure("lesson_19_wrapper_unique.mp4", (412, 312), decay=(1, 256), hold=20)
    common = dict(zoom=20, max=20, store_cost=2)
    fig.func("h_unique:f_unique", (24, 114), label="f", **common)
    fig.func("h_unique:f_unique_in_g_unique$0", (156, 48), label="f.in(g)", label_fade=5,
             **common)
    fig.func("h_unique:f_unique_in_h_unique$1", (288, 48), label="f.in(h)", label_fade=5,
             **common)
    fig.func("h_unique:g_unique", (156, 188), label="g", **common)
    fig.func("h_unique", (288, 188), label="h", **common)
    figures.append(fig)

    fig = Figure("lesson_19_wrapper_vary_schedule.mp4", (336, 368), decay=(2, 256), hold=20)
    common = dict(zoom=16, max=20, store_cost=2)
    fig.func("h_sched:f_sched_in_g_sched$0", (24, 48), label="f.in(g)", **common)
    # This wrapper's coordinates are offset, so shift it back on screen.
    fig.func("h_sched:f_sched_in_h_sched$1", (184 - 1488, 48 + 1392), label="f.in(h)",
             label_pos=(184, 40), label_fade=5, **common)
    fig.func("h_sched:g_sched", (24, 216), label="g", **common)
    fig.func("h_sched:h_sched", (184, 216), label="h", **common)
    figures.append(fig)

    fig = Figure("lesson_19_transpose.mp4", (592, 232), decay=(1, 256), hold=20)
    common = dict(zoom=10, min=-1.1, max=1.1)
    fig.func("g_transpose:f_transpose", (24, 48), label="f", store_cost=0, **common)
    fig.func("g_transpose:f_transpose_in_g_transpose$0", (216, 48), label="f.in(g)",
             label_fade=5, blank=True, **common)
    fig.func("g_transpose", (408, 48), label="g", **common)
    figures.append(fig)

    fig = Figure("lesson_19_group_updates.mp4", (496, 200), decay=(1, 256), hold=30)
    common = dict(zoom=16, min=-0.9, max=0.9, store_cost=2)
    fig.func("g_group:f_group", (24, 48), label="f", blank=True, **common)
    fig.func("g_group:f_group_in_g_group$0", (184, 48), label="f.in(g)", **common)
    fig.func("g_group", (344, 48), label="g", **common)
    figures.append(fig)
    return figures


LESSONS = [
    lesson_05_scheduling_1,
    lesson_08_scheduling_2,
    lesson_09_update_definitions,
    lesson_17_predicated_rdom,
    lesson_18_parallel_associative_reductions,
    lesson_19_wrapper_funcs,
]


def record_trace(lesson, trace_path):
    env = dict(os.environ, HL_JIT_TARGET="host-trace_all", HL_TRACE_FILE=trace_path)
    print(f"Tracing {lesson} into {trace_path}")
    subprocess.run(["make", "-C", ROOT_DIR, f"tutorial_{lesson}"], env=env, check=True,
                   stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--trace-dir",
                        help="Directory in which to keep lesson traces. Existing traces are "
                             "reused. By default, traces go in a temporary directory.")
    parser.add_argument("--output-dir", default=FIGURES_DIR)
    parser.add_argument("only", nargs="*",
                        help="Lessons (e.g. lesson_05_scheduling_1) or figures "
                             "(e.g. lesson_05_tiled.gif) to generate. Defaults to all.")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        trace_dir = os.path.abspath(args.trace_dir or tmp)
        os.makedirs(trace_dir, exist_ok=True)
        for lesson_figures in LESSONS:
            lesson = lesson_figures.__name__
            figures = [f for f in lesson_figures()
                       if not args.only or lesson in args.only or f.output in args.only]
            if not figures:
                continue
            trace_path = os.path.join(trace_dir, f"{lesson}.bin")
            if not os.path.exists(trace_path):
                record_trace(lesson, trace_path)
            render_trace(trace_path, figures)
            for fig in figures:
                fig.save(args.output_dir)


if __name__ == "__main__":
    main()
