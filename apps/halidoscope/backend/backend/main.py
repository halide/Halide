from __future__ import annotations

import logging
import time as _time
import uuid
from typing import Any

import numpy as np
import uvicorn
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from fastapi.middleware.cors import CORSMiddleware
from fastapi.websockets import WebSocket, WebSocketDisconnect
from halide import FuncStats, Trace

log = logging.getLogger(__name__)

app = FastAPI(title="Halidoscope Backend")

origins = ["http://localhost:1420"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

_sessions: dict[str, Any] = {}
_packets: dict[str, Any] = {}
_store_indices: dict[str, list[int]] = {}
_load_indices: dict[str, list[int]] = {}
_func_name_cache: dict[
    str, dict[str, Any | None]
] = {}  # session_id -> {packet_func -> stats | None}


def _analyze_packets(
    session_id: str, trace: Any, funcs: dict[str, FuncStats]
) -> dict[str, Any]:
    # Delegate the packet scan to C++ for performance; the result maps each
    # qualified func name to its per-func max store and load counts.
    max_counts = trace.compute_max_load_store_counts()

    result: dict[str, Any] = {}
    global_max_store = 0
    global_max_load = 0

    for func_name, func_stats in funcs.items():
        counts = max_counts.get(func_name)
        if counts is None:
            continue

        max_store: int = counts["max_store_count"]
        max_load: int = counts["max_load_count"]

        min_coords = list(func_stats.min_coords)
        max_coords = list(func_stats.max_coords)
        width = max_coords[0] - min_coords[0]
        height = (
            max_coords[1] - min_coords[1]
            if len(min_coords) > 1 and len(max_coords) > 1
            else 1
        )

        entry: dict[str, Any] = {
            "name": func_name,
            "width": width,
            "height": height,
            "min_coords": min_coords,
            "max_coords": max_coords,
            "min_value": func_stats.min_value,
            "max_value": func_stats.max_value,
            "max_store_count": max_store,
            "max_load_count": max_load,
        }
        result[func_name] = entry
        _func_name_cache[session_id][func_name] = entry

        global_max_store = max(global_max_store, max_store)
        global_max_load = max(global_max_load, max_load)

    return result, global_max_store, global_max_load


def _register_trace(trace: Any) -> dict[str, Any]:
    session_id = str(uuid.uuid4())

    # Cache once; avoids full C++ vector copy on each access.
    _packets[session_id] = trace.packets
    _store_indices[session_id] = np.array(trace.store_indices())
    _load_indices[session_id] = np.array(trace.load_indices())
    _func_name_cache[session_id] = {}

    # Analyze packets to compute load/store counts and other stats per Func.
    funcs, global_max_store, global_max_load = _analyze_packets(
        session_id, trace, trace.funcs
    )

    payload = {
        "session_id": session_id,
        "num_packets": len(trace),
        "funcs": funcs,
        "dag_edges": {k: list(v) for k, v in trace.dag_edges.items()},
        "pipelines": {str(k): v for k, v in trace.pipelines.items()},
        "global_max_store_count": global_max_store,
        "global_max_load_count": global_max_load,
    }
    _sessions[session_id] = payload

    return payload


class LoadPathRequest(BaseModel):
    path: str


@app.post("/load-path")
async def load_trace_path(request: LoadPathRequest) -> dict[str, Any]:
    try:
        with open(request.path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise HTTPException(status_code=400, detail=str(e))

    trace = Trace.load_bytes(data)
    return _register_trace(trace)


def _get_func_item_for_packet(session_id: str, func_name: str) -> Any:
    cache = _func_name_cache[session_id]
    if func_name in cache:
        return cache[func_name]

    funcs = _sessions[session_id]["funcs"]
    for name, stats in funcs.items():
        if name == func_name or name.endswith(f":{func_name}"):
            cache[func_name] = stats
            return stats

    cache[func_name] = None
    return None


def _render_range(session_id: str, start: int, end: int) -> list[dict[str, Any]]:
    packets = _packets[session_id]
    indices = _store_indices[session_id]

    # pending: func_name -> [px_list, py_list, c_list, val_list, func_stats]
    pending: dict[str, list] = {}

    end = min(end, len(packets))

    lo = np.searchsorted(indices, start, side="left")
    hi = np.searchsorted(indices, end - 1, side="right")
    for si in range(lo, hi):
        # Grab the next load or store packet in the requested range.
        packet = packets[indices[si]]

        func_stats = _get_func_item_for_packet(session_id, packet.func)
        coords = np.asarray(packet.coordinates)
        values_arr = np.asarray(packet.get_values())
        n_lanes = packet.type_lanes
        dims_per_lane = len(coords) // n_lanes
        min_coords = func_stats["min_coords"]
        min_x = min_coords[0] if min_coords else 0
        min_y = min_coords[1] if len(min_coords) > 1 else 0
        n = min(n_lanes, len(values_arr))

        # Check to see if there are pending updates for this Func; if not,
        # initialize the lists and cache the func_stats.
        if func_stats["name"] not in pending:
            pending[func_stats["name"]] = [[], [], [], [], func_stats]
        px_list, py_list, c_list, val_list, _ = pending[func_stats["name"]]

        xs = coords[:n_lanes] - min_x
        ys = (
            coords[n_lanes : 2 * n_lanes] - min_y
            if dims_per_lane >= 2
            else np.full(n_lanes, -min_y, dtype=np.intp)
        )
        cs = (
            coords[2 * n_lanes : 3 * n_lanes]
            if dims_per_lane >= 3
            else np.full(n_lanes, -1, dtype=np.intp)
        )

        px_list.extend(xs[:n].tolist())
        py_list.extend(ys[:n].tolist())
        c_list.extend(cs[:n].tolist())
        val_list.extend(values_arr[:n].tolist())

    updates = []
    for func_name, (px_list, py_list, c_list, val_list, func_stats) in pending.items():
        if not px_list:
            continue

        xs = np.asarray(px_list, dtype=np.intp)
        ys = np.asarray(py_list, dtype=np.intp)
        vals = np.asarray(val_list)

        min_v = func_stats["min_value"] or 0.0
        max_v = func_stats["max_value"] or 255.0
        if max_v > min_v:
            normalized = np.clip(
                (255.0 * (vals - min_v) / (max_v - min_v)), 0, 255
            ).astype(np.uint8)
        else:
            normalized = np.full(len(xs), 128, dtype=np.uint8)

        min_coords = func_stats["min_coords"]
        max_coords = func_stats["max_coords"]
        width = max_coords[0] - min_coords[0]
        height = (
            max(1, max_coords[1] - min_coords[1])
            if len(min_coords) > 1 and len(max_coords) > 1
            else 1
        )

        mask = (xs >= 0) & (xs < width) & (ys >= 0) & (ys < height)
        xs = xs[mask]
        ys = ys[mask]
        normalized = normalized[mask]

        is_color = (
            len(min_coords) >= 3
            and len(max_coords) >= 3
            and max_coords[2] - min_coords[2] >= 3
        )

        if is_color:
            cs = np.asarray(c_list, dtype=np.intp)[mask]
            update: dict[str, Any] = {"func": func_name}
            for ch_idx, key in [(0, "r"), (1, "g"), (2, "b")]:
                m = cs == ch_idx
                if m.any():
                    update[key] = {
                        "xs": xs[m].tolist(),
                        "ys": ys[m].tolist(),
                        "values": normalized[m].tolist(),
                    }
            if len(update) > 1:
                updates.append(update)
        elif len(xs):
            updates.append(
                {
                    "func": func_name,
                    "xs": xs.tolist(),
                    "ys": ys.tolist(),
                    "values": normalized.tolist(),
                }
            )

    return updates


@app.websocket("/ws/{session_id}")
async def render_ws(websocket: WebSocket, session_id: str) -> None:
    await websocket.accept()

    try:
        if session_id not in _sessions:
            await websocket.close(code=4004, reason="session not found")
            return

        log.info("ws connected: session=%s", session_id)

        while True:
            msg = await websocket.receive_json()
            start: int = msg["start"]
            end: int = msg["end"]
            log.info("ws range request: start=%d end=%d", start, end)

            t0 = _time.perf_counter()
            updates = _render_range(session_id, start, end)
            t1 = _time.perf_counter()

            await websocket.send_json(
                {"updates": updates, "done": True, "start": start, "end": end}
            )
            t2 = _time.perf_counter()

            log.info(
                "render=%dms send=%dms total=%dms funcs=%d start=%d end=%d",
                1000 * (t1 - t0),
                1000 * (t2 - t1),
                1000 * (t2 - t0),
                len(updates),
                start,
                end,
            )

    except WebSocketDisconnect:
        pass
    except Exception:
        log.exception("WebSocket error for session %s", session_id)
        await websocket.close(code=1011, reason="internal error")


def _track_stores(session_id: str, start: int, end: int) -> list[dict[str, Any]]:
    packets = _packets[session_id]
    store_indices = _store_indices[session_id]

    end = min(end, len(packets))
    lo = np.searchsorted(store_indices, start, side="left")
    hi = np.searchsorted(store_indices, end - 1, side="right")

    # func_name -> [xs, ys, func_stats]
    pending: dict[str, list] = {}

    for store_i in range(lo, hi):
        packet = packets[store_indices[store_i]]
        func_stats = _get_func_item_for_packet(session_id, packet.func)
        if func_stats is None:
            continue

        func_name = func_stats["name"]
        if func_name not in pending:
            pending[func_name] = [[], [], func_stats]
        xs_list, ys_list, _ = pending[func_name]

        min_coords = func_stats["min_coords"]
        max_coords = func_stats["max_coords"]
        min_x = min_coords[0] if min_coords else 0
        min_y = min_coords[1] if len(min_coords) > 1 else 0
        width = max_coords[0] - min_coords[0]
        height = (
            max(1, max_coords[1] - min_coords[1])
            if len(min_coords) > 1 and len(max_coords) > 1
            else 1
        )

        coords = np.asarray(packet.coordinates)
        n_lanes = packet.type_lanes
        dims_per_lane = len(coords) // n_lanes

        xs = coords[:n_lanes] - min_x
        ys = (
            coords[n_lanes : 2 * n_lanes] - min_y
            if dims_per_lane >= 2
            else np.full(n_lanes, -min_y, dtype=np.intp)
        )
        mask = (xs >= 0) & (xs < width) & (ys >= 0) & (ys < height)
        xs_list.extend(xs[mask].tolist())
        ys_list.extend(ys[mask].tolist())

    return [
        {"func": func_name, "xs": xs_list, "ys": ys_list}
        for func_name, (xs_list, ys_list, _) in pending.items()
        if xs_list
    ]


def _track_loads(session_id: str, start: int, end: int) -> list[dict[str, Any]]:
    packets = _packets[session_id]
    load_indices = _load_indices[session_id]

    end = min(end, len(packets))
    lo = np.searchsorted(load_indices, start, side="left")
    hi = np.searchsorted(load_indices, end - 1, side="right")

    # func_name -> [xs, ys, func_stats]
    pending: dict[str, list] = {}

    for load_i in range(lo, hi):
        packet = packets[load_indices[load_i]]
        func_stats = _get_func_item_for_packet(session_id, packet.func)
        if func_stats is None:
            continue

        func_name = func_stats["name"]
        if func_name not in pending:
            pending[func_name] = [[], [], func_stats]
        xs_list, ys_list, _ = pending[func_name]

        min_coords = func_stats["min_coords"]
        max_coords = func_stats["max_coords"]
        min_x = min_coords[0] if min_coords else 0
        min_y = min_coords[1] if len(min_coords) > 1 else 0
        width = max_coords[0] - min_coords[0]
        height = (
            max(1, max_coords[1] - min_coords[1])
            if len(min_coords) > 1 and len(max_coords) > 1
            else 1
        )

        coords = np.asarray(packet.coordinates)
        n_lanes = packet.type_lanes
        dims_per_lane = len(coords) // n_lanes

        xs = coords[:n_lanes] - min_x
        ys = (
            coords[n_lanes : 2 * n_lanes] - min_y
            if dims_per_lane >= 2
            else np.full(n_lanes, -min_y, dtype=np.intp)
        )
        mask = (xs >= 0) & (xs < width) & (ys >= 0) & (ys < height)
        xs_list.extend(xs[mask].tolist())
        ys_list.extend(ys[mask].tolist())

    return [
        {"func": func_name, "xs": xs_list, "ys": ys_list}
        for func_name, (xs_list, ys_list, _) in pending.items()
        if xs_list
    ]


@app.websocket("/ws/{session_id}/loads")
async def render_loads_ws(websocket: WebSocket, session_id: str) -> None:
    await websocket.accept()

    try:
        if session_id not in _sessions:
            await websocket.close(code=4004, reason="session not found")
            return

        log.info("ws connected: session=%s", session_id)

        while True:
            msg = await websocket.receive_json()
            start: int = msg["start"]
            end: int = msg["end"]
            log.info("ws loads range request: start=%d end=%d", start, end)

            t0 = _time.perf_counter()
            updates = _track_loads(session_id, start, end)
            t1 = _time.perf_counter()

            await websocket.send_json(
                {"updates": updates, "done": True, "start": start, "end": end}
            )
            t2 = _time.perf_counter()

            log.info(
                "track_loads render=%dms send=%dms total=%dms funcs=%d start=%d end=%d",
                1000 * (t1 - t0),
                1000 * (t2 - t1),
                1000 * (t2 - t0),
                len(updates),
                start,
                end,
            )

    except WebSocketDisconnect:
        pass
    except Exception:
        log.exception("WebSocket error for session %s", session_id)
        await websocket.close(code=1011, reason="internal error")


@app.websocket("/ws/{session_id}/stores")
async def render_stores_ws(websocket: WebSocket, session_id: str) -> None:
    await websocket.accept()

    try:
        if session_id not in _sessions:
            await websocket.close(code=4004, reason="session not found")
            return

        log.info("ws connected: session=%s", session_id)

        while True:
            msg = await websocket.receive_json()
            start: int = msg["start"]
            end: int = msg["end"]
            log.info("ws stores range request: start=%d end=%d", start, end)

            t0 = _time.perf_counter()
            updates = _track_stores(session_id, start, end)
            t1 = _time.perf_counter()

            await websocket.send_json(
                {"updates": updates, "done": True, "start": start, "end": end}
            )
            t2 = _time.perf_counter()

            log.info(
                "track_stores render=%dms send=%dms total=%dms funcs=%d start=%d end=%d",
                1000 * (t1 - t0),
                1000 * (t2 - t1),
                1000 * (t2 - t0),
                len(updates),
                start,
                end,
            )

    except WebSocketDisconnect:
        pass
    except Exception:
        log.exception("WebSocket error for session %s", session_id)
        await websocket.close(code=1011, reason="internal error")


@app.delete("/session/{session_id}")
async def delete_session(session_id: str) -> dict[str, str]:
    if session_id not in _sessions:
        raise HTTPException(status_code=404, detail="session not found")
    del _sessions[session_id]
    del _packets[session_id]
    del _store_indices[session_id]
    del _load_indices[session_id]
    del _func_name_cache[session_id]

    return {"deleted": session_id}


def run() -> None:
    logging.basicConfig(level=logging.INFO)
    uvicorn.run(
        "backend.main:app", host="127.0.0.1", port=8765, reload=False, log_level="info"
    )
