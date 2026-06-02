"""Functional tests for func display modes & the display-properties panel.

Covers the channel-aware C++ collectors (planar / interleaved RGB, channel-aware
load dedup, the clean `realize_extent`), the scalar colormap/normalization
helpers, and the viewer behavior built on top of them (channel detection,
color rendering of both stored and input-only buffers, and live interpretation
switching). All traces are synthetic and built in-memory, so the suite has no
dependency on a real .hltrace file.
"""

from __future__ import annotations

import os

# Must be set before any PySide6 import so the Qt tests run without a display.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import numpy as np
import pytest
from halide import Trace

from tests.helpers import EventCode, TypeCode, make_packet

# ---------------------------------------------------------------------------
# Synthetic trace builders
# ---------------------------------------------------------------------------

_PIPE = "pipe"

# Per-channel store/load values used by the color traces. Chosen so that, under
# the func's own [min, max] = [64, 192] auto-range, the channels normalize to
# distinct, strictly increasing intensities (R=0 < G=127 < B=255).
_CHAN_VALS = {0: 64, 1: 128, 2: 192}


class _Ids:
    """Monotonic packet-id allocator."""

    def __init__(self, start: int = 1):
        self._n = start

    def __call__(self) -> int:
        v = self._n
        self._n += 1
        return v


def _meta(
    pid: int,
    event: EventCode,
    parent: int,
    func: str,
    *,
    coordinates: tuple[int, ...] = (),
    trace_tag: str = "",
) -> bytes:
    """A value-less structural packet (pipeline/realization bracket or tag).

    type_lanes=0 so the encoded value section is empty and matches what the C++
    reader expects; otherwise the (default) 1-lane value size would misalign the
    trailing func-name and tag strings.
    """
    return make_packet(
        pid,
        event,
        parent,
        func,
        type_lanes=0,
        coordinates=coordinates,
        trace_tag=trace_tag,
    )


def _tag(num_dims_pairs: tuple[int, ...]) -> str:
    """func_type_and_dim tag for a single uint8 type with the given [min, ext] pairs."""
    ndims = len(num_dims_pairs) // 2
    body = " ".join(str(v) for v in num_dims_pairs)
    return f"func_type_and_dim: 1 1 8 1 {ndims} {body}"


def _color_func_packets(
    ids: _Ids,
    func: str,
    *,
    interleaved: bool,
    w: int,
    h: int,
    c: int,
    event: EventCode,
    dup: bool = False,
) -> list[bytes]:
    """Packets for a planar f(x,y,c) or interleaved f(c,x,y) RGB func.

    `event` is STORE for a computed buffer or LOAD for an input-only buffer.
    With dup=True every coordinate is emitted twice (to exercise load dedup).
    """
    pkts: list[bytes] = []
    tag = _tag((0, c, 0, w, 0, h)) if interleaved else _tag((0, w, 0, h, 0, c))
    pkts.append(_meta(ids(), EventCode.TAG, 1, func, trace_tag=tag))

    realize_id = ids()
    if event == EventCode.STORE:
        coords = (0, c, 0, w, 0, h) if interleaved else (0, w, 0, h, 0, c)
        pkts.append(
            _meta(realize_id, EventCode.BEGIN_REALIZATION, 1, func, coordinates=coords)
        )
        parent = realize_id
    else:
        parent = 1  # loads attach directly to the pipeline

    reps = 2 if dup else 1
    for cc in range(c):
        for y in range(h):
            for x in range(w):
                coord = (cc, x, y) if interleaved else (x, y, cc)
                for _ in range(reps):
                    pkts.append(
                        make_packet(
                            ids(),
                            event,
                            parent,
                            func,
                            type_code=TypeCode.UINT,
                            type_bits=8,
                            coordinates=coord,
                            values=(_CHAN_VALS[cc],),
                        )
                    )
    if event == EventCode.STORE:
        pkts.append(_meta(ids(), EventCode.END_REALIZATION, realize_id, func))
    return pkts


def _planar_store_trace(w: int = 4, h: int = 3, c: int = 3) -> bytes:
    ids = _Ids()
    assert ids() == 1  # reserve id 1 for the pipeline
    pkts = [_meta(1, EventCode.BEGIN_PIPELINE, 0, _PIPE)]
    pkts += _color_func_packets(
        ids, "img", interleaved=False, w=w, h=h, c=c, event=EventCode.STORE
    )
    pkts.append(_meta(ids(), EventCode.END_PIPELINE, 1, _PIPE))
    return b"".join(pkts)


def _interleaved_store_trace(w: int = 4, h: int = 3, c: int = 3) -> bytes:
    ids = _Ids()
    ids()
    pkts = [_meta(1, EventCode.BEGIN_PIPELINE, 0, _PIPE)]
    pkts += _color_func_packets(
        ids, "imgi", interleaved=True, w=w, h=h, c=c, event=EventCode.STORE
    )
    pkts.append(_meta(ids(), EventCode.END_PIPELINE, 1, _PIPE))
    return b"".join(pkts)


def _planar_load_trace(w: int = 4, h: int = 3, c: int = 3, dup: bool = False) -> bytes:
    ids = _Ids()
    ids()
    pkts = [_meta(1, EventCode.BEGIN_PIPELINE, 0, _PIPE)]
    pkts += _color_func_packets(
        ids, "input", interleaved=False, w=w, h=h, c=c, event=EventCode.LOAD, dup=dup
    )
    pkts.append(_meta(ids(), EventCode.END_PIPELINE, 1, _PIPE))
    return b"".join(pkts)


def _gray_func_packets(ids: _Ids, func: str, w: int, h: int) -> list[bytes]:
    """A plain 2-D grayscale store func."""
    pkts = [_meta(ids(), EventCode.TAG, 1, func, trace_tag=_tag((0, w, 0, h)))]
    realize_id = ids()
    pkts.append(
        _meta(
            realize_id, EventCode.BEGIN_REALIZATION, 1, func, coordinates=(0, w, 0, h)
        )
    )
    for y in range(h):
        for x in range(w):
            pkts.append(
                make_packet(
                    ids(),
                    EventCode.STORE,
                    realize_id,
                    func,
                    type_code=TypeCode.UINT,
                    type_bits=8,
                    coordinates=(x, y),
                    values=(x * 10,),
                )
            )
    pkts.append(_meta(ids(), EventCode.END_REALIZATION, realize_id, func))
    return pkts


def _signed_1d_func_packets(
    ids: _Ids, func: str, n: int = 9, lo: int = -4
) -> list[bytes]:
    """A 1-D float func whose values straddle zero (a remap-like signed LUT)."""
    tag = f"func_type_and_dim: 1 2 32 1 1 {lo} {n}"
    pkts = [_meta(ids(), EventCode.TAG, 1, func, trace_tag=tag)]
    realize_id = ids()
    pkts.append(
        _meta(realize_id, EventCode.BEGIN_REALIZATION, 1, func, coordinates=(lo, n))
    )
    for i in range(n):
        x = lo + i
        pkts.append(
            make_packet(
                ids(),
                EventCode.STORE,
                realize_id,
                func,
                type_code=TypeCode.FLOAT,
                type_bits=32,
                coordinates=(x,),
                values=(float(x) * 0.05,),  # spans negative → positive
            )
        )
    pkts.append(make_packet(ids(), EventCode.END_REALIZATION, realize_id, func))
    return pkts


def _combined_trace() -> bytes:
    """One pipeline with a planar store img, an input-only color buffer, a gray
    2-D func, and a signed 1-D LUT — exercises channel/colormap detection."""
    ids = _Ids()
    ids()  # reserve id 1
    pkts = [_meta(1, EventCode.BEGIN_PIPELINE, 0, _PIPE)]
    pkts += _color_func_packets(
        ids, "img", interleaved=False, w=4, h=3, c=3, event=EventCode.STORE
    )
    pkts += _color_func_packets(
        ids, "input", interleaved=False, w=4, h=3, c=3, event=EventCode.LOAD
    )
    pkts += _gray_func_packets(ids, "blur", 4, 3)
    pkts += _signed_1d_func_packets(ids, "lut")
    pkts.append(_meta(ids(), EventCode.END_PIPELINE, 1, _PIPE))
    return b"".join(pkts)


def _q(funcs: dict, short: str) -> str:
    """Find the qualified func name ending in :short."""
    suffix = f":{short}"
    for name in funcs:
        if name == short or name.endswith(suffix):
            return name
    raise KeyError(short)


# ---------------------------------------------------------------------------
# C++ collector tests (no Qt)
# ---------------------------------------------------------------------------


def test_realize_extent_is_clean_after_pollution():
    """realize_extent reflects the declared shape even though load/store coords
    widen min_coords/max_coords on the channel axis."""
    trace = Trace.load_bytes(_planar_store_trace(w=4, h=3, c=3))
    name = _q(trace.funcs, "img")
    st = trace.funcs[name]
    assert list(st.realize_extent) == [4, 3, 3]
    assert list(st.realize_min) == [0, 0, 0]


def test_collect_pixels_default_is_unchanged_3_tuple():
    trace = Trace.load_bytes(_planar_store_trace())
    data = trace.collect_pixels(0, len(trace))
    entry = data["img"]
    assert len(entry) == 3  # (xs, ys, vals) — no channel array


def test_collect_pixels_planar_channels():
    """color_dim=2 keeps x=dim0, y=dim1 and surfaces the channel coordinate."""
    w, h, c = 4, 3, 3
    trace = Trace.load_bytes(_planar_store_trace(w, h, c))
    data = trace.collect_pixels(0, len(trace), 2)
    xs, ys, vals, cs = data["img"]
    assert len(xs) == w * h * c
    assert sorted(set(cs.tolist())) == [0, 1, 2]
    assert int(xs.min()) == 0 and int(xs.max()) == w - 1
    assert int(ys.min()) == 0 and int(ys.max()) == h - 1
    # Each channel's value matches what we stored for that channel.
    for cc in (0, 1, 2):
        assert set(vals[cs == cc].astype(int).tolist()) == {_CHAN_VALS[cc]}


def test_collect_pixels_interleaved_channels():
    """color_dim=0 shifts spatial axes to dims 1,2 and reads the channel from dim0."""
    w, h, c = 4, 3, 3
    trace = Trace.load_bytes(_interleaved_store_trace(w, h, c))
    data = trace.collect_pixels(0, len(trace), 0)
    xs, ys, vals, cs = data["imgi"]
    assert len(xs) == w * h * c
    assert sorted(set(cs.tolist())) == [0, 1, 2]
    assert int(xs.min()) == 0 and int(xs.max()) == w - 1
    assert int(ys.min()) == 0 and int(ys.max()) == h - 1
    for cc in (0, 1, 2):
        assert set(vals[cs == cc].astype(int).tolist()) == {_CHAN_VALS[cc]}


def test_collect_load_pixels_channel_aware_dedup():
    """With a color dim, dedup keys on (x,y,channel) so every channel survives;
    without it, dedup collapses to one entry per (x,y)."""
    w, h, c = 4, 3, 3
    trace = Trace.load_bytes(_planar_load_trace(w, h, c, dup=True))
    n = len(trace)

    plain = trace.collect_load_pixels(0, n)
    pxs = plain["input"][0]
    assert len(pxs) == w * h  # channels collapsed onto one (x,y)

    color = trace.collect_load_pixels(0, n, 2)
    xs, ys, vals, cs = color["input"]
    assert len(xs) == w * h * c  # one per (x,y,channel) despite duplicate loads
    assert sorted(set(cs.tolist())) == [0, 1, 2]


# ---------------------------------------------------------------------------
# Colormap / normalization helper tests (import viewer; no QApplication needed)
# ---------------------------------------------------------------------------


def test_normalize_values_modes():
    from neotrace.viewer import _normalize_values

    lin = _normalize_values(np.array([0.0, 5.0, 10.0]), 0.0, 10.0, "linear")
    assert np.allclose(lin, [0.0, 0.5, 1.0])

    sgn = _normalize_values(np.array([-10.0, 0.0, 10.0]), -10.0, 10.0, "signed")
    assert np.allclose(sgn, [0.0, 0.5, 1.0])

    lg = _normalize_values(np.array([0.0, 100.0]), 0.0, 100.0, "log")
    assert lg[0] == pytest.approx(0.0) and lg[1] == pytest.approx(1.0)

    # Degenerate range → midgray.
    flat = _normalize_values(np.array([5.0, 5.0]), 5.0, 5.0, "linear")
    assert np.allclose(flat, 0.5)


def test_apply_colormap_endpoints():
    from neotrace.viewer import _apply_colormap

    t = np.array([0.0, 0.5, 1.0], dtype=np.float32)

    r, g, b = _apply_colormap(t, "grayscale")
    assert (r == g).all() and (g == b).all()
    assert list(r) == [0, 127, 255]

    r, g, b = _apply_colormap(t, "hot")
    assert (r[0], g[0], b[0]) == (0, 0, 0)  # black
    assert (r[2], g[2], b[2]) == (255, 255, 255)  # white

    r, g, b = _apply_colormap(t, "diverging")
    assert (r[0], g[0], b[0]) == (0, 0, 255)  # blue
    assert (r[2], g[2], b[2]) == (255, 0, 0)  # red
    assert g[1] > 200 and r[1] > 200 and b[1] > 200  # near-white center

    r, g, b = _apply_colormap(t, "viridis")
    assert (int(r[0]), int(g[0]), int(b[0])) == (68, 1, 84)
    assert (int(r[2]), int(g[2]), int(b[2])) == (253, 231, 37)


# ---------------------------------------------------------------------------
# Viewer-level tests (Qt offscreen)
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def qapp():
    from PySide6.QtWidgets import QApplication

    return QApplication.instance() or QApplication([])


def _load_viewer(qapp, tmp_path, data: bytes):
    from neotrace.viewer import TraceViewer

    path = tmp_path / "synthetic.hltrace"
    path.write_bytes(data)
    viewer = TraceViewer()
    viewer.load_trace(path)
    return viewer


def _item(viewer, short: str):
    for fn, it in viewer.canvas.func_items.items():
        if fn.split(":")[-1] == short:
            return it
    return None


def test_channel_and_colormap_detection(qapp, tmp_path):
    viewer = _load_viewer(qapp, tmp_path, _combined_trace())
    cfgs = viewer.state.func_configs

    img = cfgs[_q(cfgs, "img")]
    assert img.color_dim == 2 and img.channel_order == "rgb"

    inp = cfgs[_q(cfgs, "input")]
    assert inp.color_dim == 2  # input-only buffer detected as planar RGB

    blur = cfgs[_q(cfgs, "blur")]
    assert blur.color_dim == -1 and blur.colormap == "grayscale"

    lut = cfgs[_q(cfgs, "lut")]
    assert lut.color_dim == -1
    assert lut.colormap == "diverging" and lut.normalization == "signed"
    assert lut.plot_1d is True


def test_color_store_render_is_actually_colored(qapp, tmp_path):
    viewer = _load_viewer(qapp, tmp_path, _planar_store_trace(w=4, h=3, c=3))
    viewer._render_to_time(len(viewer._packets) - 1)
    item = _item(viewer, "img")
    assert item is not None
    assert item.data.shape == (3, 4, 4)  # H, W, RGBA (spatial dims 0,1)
    rgb = item.data[:, :, :3].astype(int)
    # Channels stored as R<G<B everywhere; the result must not be flat gray.
    assert (rgb[:, :, 0] <= rgb[:, :, 1]).all()
    assert (rgb[:, :, 1] <= rgb[:, :, 2]).all()
    assert (rgb[:, :, 0] != rgb[:, :, 2]).any()


def test_input_only_buffer_renders_in_color(qapp, tmp_path):
    viewer = _load_viewer(qapp, tmp_path, _planar_load_trace(w=4, h=3, c=3))
    viewer._render_to_time(len(viewer._packets) - 1)
    item = _item(viewer, "input")
    assert item is not None
    rgb = item.data[:, :, :3].astype(int)
    assert (rgb[:, :, 0] != rgb[:, :, 2]).any()  # color, not grayscale


def test_interpretation_switch_reshapes_data(qapp, tmp_path):
    viewer = _load_viewer(qapp, tmp_path, _planar_store_trace(w=4, h=3, c=3))
    viewer._render_to_time(len(viewer._packets) - 1)
    item = _item(viewer, "img")
    assert item.data.shape == (3, 4, 4)  # planar: spatial dims 0,1 → 4×3

    # Drive the panel exactly as a user would.
    viewer._on_func_selected(item.func_name)
    viewer.props_panel._interp.setCurrentText("Interleaved RGB")
    assert item.config.color_dim == 0
    # Interleaved: spatial dims become 1,2 → width=3 (dim1), height=3 (dim2).
    assert item.data.shape == (3, 3, 4)

    viewer.props_panel._interp.setCurrentText("Grayscale")
    assert item.config.color_dim == -1
    rgb = item.data[:, :, :3].astype(int)
    assert (rgb[:, :, 0] == rgb[:, :, 2]).all()  # collapsed back to gray

    viewer.props_panel._interp.setCurrentText("Planar RGB")
    assert item.config.color_dim == 2
    assert item.data.shape == (3, 4, 4)


def test_config_export_round_trips_display_fields(qapp, tmp_path):
    import yaml

    viewer = _load_viewer(qapp, tmp_path, _combined_trace())
    out = tmp_path / "layout.yaml"
    viewer._save_config(out)
    cfg = yaml.safe_load(out.read_text())["funcs"]

    sample = next(iter(cfg.values()))
    for key in ("color_dim", "channel_order", "colormap", "normalization", "plot_1d"):
        assert key in sample, f"export missing {key}"

    img = cfg[_q(cfg, "img")]
    assert img["color_dim"] == 2 and img["channel_order"] == "rgb"
    lut = cfg[_q(cfg, "lut")]
    assert lut["colormap"] == "diverging" and lut["plot_1d"] is True
