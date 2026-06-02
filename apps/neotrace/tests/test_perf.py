"""Performance tests for the trace rendering pipeline.

Each test measures a specific hot-path operation and fails if it exceeds a
generous budget — generous enough to survive slow CI, tight enough to catch
regressions that are 10-20x slower than expected.

Run with:
    uv run pytest tests/test_perf.py -v -s
"""

from __future__ import annotations

import bisect
import random
import time

import numpy as np
import pytest
from halide import Trace

from tests.helpers import EventCode, TypeCode, make_packet

# ---------------------------------------------------------------------------
# Synthetic trace builder
# ---------------------------------------------------------------------------

_N_STORES = 10_000
_N_STORES_LARGE = 100_000  # Simulates a real local_laplacian backward scrub
_IMG_W = 256
_IMG_H = 256


def _build_trace_bytes(
    n_stores: int = _N_STORES,
    width: int = _IMG_W,
    height: int = _IMG_H,
    seed: int = 42,
) -> bytes:
    """Return a binary trace with n_stores uint16 store packets for func 'f'."""
    rng = random.Random(seed)
    # Every trace needs a begin/end pipeline and a realization bracket.
    # The func_type_and_dim tag conveys coordinate bounds to the C++ parser.
    packets: list[bytes] = [
        make_packet(1, EventCode.BEGIN_PIPELINE, 0, "bench_pipe"),
        make_packet(
            2, EventCode.BEGIN_REALIZATION, 1, "f", coordinates=(0, width, 0, height)
        ),
    ]
    for i in range(n_stores):
        x = rng.randint(0, width - 1)
        y = rng.randint(0, height - 1)
        v = rng.randint(0, 65535)
        packets.append(
            make_packet(
                3 + i,
                EventCode.STORE,
                2,
                "f",
                type_code=TypeCode.UINT,
                type_bits=16,
                coordinates=(x, y),
                values=(v,),
            )
        )
    packets.append(make_packet(3 + n_stores, EventCode.END_REALIZATION, 2, "f"))
    packets.append(make_packet(4 + n_stores, EventCode.END_PIPELINE, 1, "bench_pipe"))
    return b"".join(packets)


# ---------------------------------------------------------------------------
# Module-scoped fixture so the trace is only loaded once
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def loaded_trace():
    data = _build_trace_bytes()
    return Trace.load_bytes(data)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_trace_load_perf():
    """Loading a 10K-store trace should complete in < 2 s."""
    data = _build_trace_bytes()
    t0 = time.perf_counter()
    trace = Trace.load_bytes(data)
    elapsed = time.perf_counter() - t0

    n_packets = len(trace)
    print(
        f"\n  load: {elapsed * 1000:.1f} ms  ({n_packets} packets, {_N_STORES} stores)"
    )
    assert elapsed < 2.0, f"trace load too slow: {elapsed * 1000:.0f} ms"
    assert len(trace.funcs) > 0


def test_store_indices_perf(loaded_trace):
    """store_indices() should complete in < 500 ms for a 10K-store trace."""
    trace = loaded_trace
    t0 = time.perf_counter()
    indices = trace.store_indices()
    elapsed = time.perf_counter() - t0

    print(f"\n  store_indices: {elapsed * 1000:.1f} ms  ({len(indices)} stores)")
    assert elapsed < 0.5, f"store_indices too slow: {elapsed * 1000:.0f} ms"
    assert len(indices) == _N_STORES


def test_packets_cache_perf(loaded_trace):
    """Materialising trace.packets (C++ vector → Python list) should be < 1 s."""
    trace = loaded_trace
    t0 = time.perf_counter()
    packets = trace.packets
    elapsed = time.perf_counter() - t0

    print(f"\n  packets materialise: {elapsed * 1000:.1f} ms  ({len(packets)} packets)")
    assert elapsed < 1.0, f"packet materialisation too slow: {elapsed * 1000:.0f} ms"


def test_bisect_range_lookup(loaded_trace):
    """bisect on store_indices should be sub-millisecond for any range."""
    trace = loaded_trace
    indices = trace.store_indices()
    n = len(trace)

    t0 = time.perf_counter()
    lo = bisect.bisect_left(indices, 0)
    hi = bisect.bisect_right(indices, n - 1)
    elapsed = time.perf_counter() - t0

    print(
        f"\n  bisect: {elapsed * 1e6:.1f} µs  "
        f"(range [{lo}, {hi}) in {len(indices)} entries)"
    )
    assert elapsed < 0.001, f"bisect unexpectedly slow: {elapsed * 1e6:.0f} µs"
    assert hi - lo == _N_STORES


def test_pybind11_packet_access_perf(loaded_trace):
    """Accessing get_values() / coordinates / type_lanes per store packet.

    Budget: 1 000 stores in < 2 s  (i.e. < 2 ms/store, extremely generous).
    The goal is to catch somebody accidentally re-introducing a full vector
    copy on every packet access.
    """
    trace = loaded_trace
    packets = trace.packets
    indices = trace.store_indices()

    # Only time the first 1 000 stores so the test is fast
    sample = indices[:1_000]
    t0 = time.perf_counter()
    for i in sample:
        p = packets[i]
        _ = p.get_values()
        _ = p.coordinates
        _ = p.type_lanes
    elapsed = time.perf_counter() - t0

    per_store_us = 1e6 * elapsed / len(sample)
    print(
        f"\n  pybind11 access: {elapsed * 1000:.1f} ms  "
        f"({len(sample)} stores, {per_store_us:.1f} µs/store)"
    )
    assert elapsed < 2.0, (
        f"pybind11 packet access too slow: {per_store_us:.0f} µs/store"
    )


def test_numpy_batch_write_perf():
    """Vectorised pixel-write kernel: 50 K pixels into a 512×512 RGBA array.

    This mirrors the hot inner loop of _render_range's batch-apply section.
    Budget: < 500 ms (normally < 5 ms on modern hardware).
    """
    rng = np.random.default_rng(42)
    W, H, N = 512, 512, 50_000
    data = np.zeros((H, W, 4), dtype=np.uint8)
    data[:, :, 3] = 255

    xs = rng.integers(0, W, size=N, dtype=np.intp)
    ys = rng.integers(0, H, size=N, dtype=np.intp)
    vals = rng.integers(0, 65535, size=N).astype(np.float64)
    min_v, max_v = 0.0, 65535.0

    t0 = time.perf_counter()
    normalized = np.clip((255.0 * (vals - min_v) / (max_v - min_v)), 0, 255).astype(
        np.uint8
    )
    mask = (xs >= 0) & (xs < W) & (ys >= 0) & (ys < H)
    xs, ys, normalized = xs[mask], ys[mask], normalized[mask]
    data[ys, xs, 0] = normalized
    data[ys, xs, 1] = normalized
    data[ys, xs, 2] = normalized
    elapsed = time.perf_counter() - t0

    rate = N / elapsed
    print(
        f"\n  batch write: {elapsed * 1000:.2f} ms  "
        f"({N} pixels, {rate / 1e6:.1f} Mpx/s)"
    )
    assert elapsed < 0.5, f"numpy batch write too slow: {elapsed * 1000:.0f} ms"


def test_full_render_range_perf(loaded_trace):
    """End-to-end _render_range equivalent: collect all stores + batch write.

    Uses real packet objects and real numpy arrays.  No Qt required.
    Budget: 10 K stores in < 5 s (normally < 200 ms).
    """
    trace = loaded_trace
    packets = trace.packets
    store_indices = trace.store_indices()

    W, H = _IMG_W, _IMG_H
    data = np.zeros((H, W, 4), dtype=np.uint8)
    data[:, :, 3] = 255
    min_v, max_v = 0.0, 65535.0

    px_list: list[int] = []
    py_list: list[int] = []
    val_list: list[float] = []

    # ---- collect phase (mirrors the loop in _render_range) ----
    t0 = time.perf_counter()
    lo = bisect.bisect_left(store_indices, 0)
    hi = bisect.bisect_right(store_indices, len(packets) - 1)
    for si in range(lo, hi):
        p = packets[store_indices[si]]
        values = p.get_values()
        if not values:
            continue
        coords = p.coordinates
        n_lanes = p.type_lanes
        dims = len(coords) // n_lanes if n_lanes else len(coords)
        for lane in range(n_lanes):
            if dims >= 2:
                x = coords[lane]
                y = coords[n_lanes + lane]
            elif dims == 1:
                x = coords[lane]
                y = 0
            else:
                x = y = 0
            if lane < len(values):
                px_list.append(x)
                py_list.append(y)
                val_list.append(values[lane])
    t_collect = time.perf_counter() - t0

    # ---- batch-write phase ----
    t1 = time.perf_counter()
    xs = np.asarray(px_list, dtype=np.intp)
    ys = np.asarray(py_list, dtype=np.intp)
    vals = np.asarray(val_list)
    normalized = np.clip((255.0 * (vals - min_v) / (max_v - min_v)), 0, 255).astype(
        np.uint8
    )
    mask = (xs >= 0) & (xs < W) & (ys >= 0) & (ys < H)
    xs, ys, normalized = xs[mask], ys[mask], normalized[mask]
    if len(xs):
        data[ys, xs, 0] = normalized
        data[ys, xs, 1] = normalized
        data[ys, xs, 2] = normalized
    t_write = time.perf_counter() - t1

    total = t_collect + t_write
    print(
        f"\n  full render: {total * 1000:.1f} ms total  "
        f"(collect={t_collect * 1000:.1f} ms, batch_write={t_write * 1000:.1f} ms)  "
        f"{len(px_list)} pixels from {_N_STORES} stores"
    )
    assert total < 5.0, f"full render too slow: {total * 1000:.0f} ms"


# ---------------------------------------------------------------------------
# Backward-scrub scenario: collect_pixels (C++ batch decode)
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def large_trace():
    """100K-store trace — representative of a real local_laplacian backward scrub."""
    data = _build_trace_bytes(n_stores=_N_STORES_LARGE)
    return Trace.load_bytes(data)


def test_collect_pixels_perf(large_trace):
    """collect_pixels() should decode 100K stores in < 2 s.

    This is the C++ replacement for the per-packet get_values()/coordinates/
    type_lanes pybind11 calls that drove the 540 ms coord overhead.  After
    optimisation the budget should be well under 200 ms; 2 s catches any
    catastrophic regression.
    """
    trace = large_trace
    n = len(trace)

    t0 = time.perf_counter()
    pixel_data = trace.collect_pixels(0, n)
    elapsed = time.perf_counter() - t0

    total_pixels = sum(len(xs) for xs, _ys, _vals in pixel_data.values())
    rate = total_pixels / elapsed if elapsed > 0 else float("inf")
    print(
        f"\n  collect_pixels: {elapsed * 1000:.1f} ms  "
        f"({_N_STORES_LARGE} stores → {total_pixels} pixels, "
        f"{rate / 1e6:.1f} Mpx/s)"
    )
    assert elapsed < 2.0, f"collect_pixels too slow: {elapsed * 1000:.0f} ms"
    assert total_pixels == _N_STORES_LARGE  # scalar trace: 1 pixel per store


def test_backward_scrub_end_to_end(large_trace):
    """Full backward-scrub pipeline: collect_pixels + numpy batch write.

    Mirrors what _render_range does after jumping back to t=0.
    Budget: 100K stores in < 5 s (normally < 500 ms after optimisation).
    """
    trace = large_trace
    n = len(trace)
    W, H = _IMG_W, _IMG_H
    data = np.zeros((H, W, 4), dtype=np.uint8)
    data[:, :, 3] = 255
    min_v, max_v = 0.0, 65535.0

    # ---- C++ collect phase ----
    t0 = time.perf_counter()
    pixel_data = trace.collect_pixels(0, n)
    t_collect = time.perf_counter() - t0

    # ---- numpy batch-write phase (one per func) ----
    total_pixels = 0
    t1 = time.perf_counter()
    for _func, (xs, ys, vals) in pixel_data.items():
        if len(xs) == 0:
            continue
        # Simulate offset by func.min_x/min_y (both 0 in this synthetic trace)
        normalized = np.clip((255.0 * (vals - min_v) / (max_v - min_v)), 0, 255).astype(
            np.uint8
        )
        mask = (xs >= 0) & (xs < W) & (ys >= 0) & (ys < H)
        xs_m, ys_m, n_m = xs[mask], ys[mask], normalized[mask]
        if len(xs_m):
            data[ys_m, xs_m, 0] = n_m
            data[ys_m, xs_m, 1] = n_m
            data[ys_m, xs_m, 2] = n_m
        total_pixels += len(xs_m)
    t_write = time.perf_counter() - t1

    total = t_collect + t_write
    print(
        f"\n  backward scrub e2e: {total * 1000:.1f} ms  "
        f"(collect={t_collect * 1000:.1f} ms, batch_write={t_write * 1000:.1f} ms)  "
        f"{total_pixels} pixels from {_N_STORES_LARGE} stores"
    )
    assert total < 5.0, f"backward scrub too slow: {total * 1000:.0f} ms"
