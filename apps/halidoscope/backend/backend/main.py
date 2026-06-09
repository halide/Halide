from __future__ import annotations

import bisect
import logging
import time as _time
import uuid
from typing import Any

import numpy as np
import uvicorn
from fastapi import FastAPI, HTTPException, UploadFile
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
_func_name_cache: dict[
    str, dict[str, Any | None]
] = {}  # session_id -> {packet_func -> stats | None}


def _serialize_func_stats(stats: FuncStats) -> dict[str, Any]:
    return {
        "name": stats.name,
        "min_coords": list(stats.min_coords),
        "max_coords": list(stats.max_coords),
        "min_value": stats.min_value,
        "max_value": stats.max_value,
    }


def _register_trace(trace: Any) -> dict[str, Any]:
    session_id = str(uuid.uuid4())
    payload = {
        "session_id": session_id,
        "num_packets": len(trace),
        "funcs": {name: _serialize_func_stats(s) for name, s in trace.funcs.items()},
        "dag_edges": {k: list(v) for k, v in trace.dag_edges.items()},
        "pipelines": {str(k): v for k, v in trace.pipelines.items()},
    }
    _sessions[session_id] = payload
    # Cache once; avoids full C++ vector copy each access in render_ws.
    _packets[session_id] = trace.packets
    _store_indices[session_id] = list(trace.store_indices())
    _func_name_cache[session_id] = {}
    return payload


@app.post("/load")
async def load_trace(file: UploadFile) -> dict[str, Any]:
    data = await file.read()
    trace = Trace.load_bytes(bytes(data))
    return _register_trace(trace)


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
    store_indices = _store_indices[session_id]

    # pending: func_name -> [px_list, py_list, c_list, val_list, func_stats]
    pending: dict[str, list] = {}

    end = min(end, len(packets))

    lo = bisect.bisect_left(store_indices, start)
    hi = bisect.bisect_right(store_indices, end - 1)
    for si in range(lo, hi):
        i = store_indices[si]
        packet = packets[i]

        func_stats = _get_func_item_for_packet(session_id, packet.func)
        if func_stats is None:
            continue

        values = packet.get_values()
        if not values:
            continue

        coords = packet.coordinates
        n_lanes = packet.type_lanes
        dims_per_lane = len(coords) // n_lanes if n_lanes > 0 else len(coords)
        min_coords = func_stats["min_coords"]
        min_x = min_coords[0] if min_coords else 0
        min_y = min_coords[1] if len(min_coords) > 1 else 0

        if func_stats["name"] not in pending:
            pending[func_stats["name"]] = [[], [], [], [], func_stats]
        px_list, py_list, c_list, val_list, _ = pending[func_stats["name"]]

        for lane in range(n_lanes):
            if dims_per_lane >= 2:
                x = coords[lane] - min_x
                y = coords[n_lanes + lane] - min_y
            elif dims_per_lane == 1:
                x = coords[lane] - min_x
                y = -min_y
            else:
                x = -min_x
                y = -min_y
            c = (
                coords[2 * n_lanes + lane]
                if dims_per_lane >= 3 and 2 * n_lanes + lane < len(coords)
                else -1
            )
            if lane < len(values):
                px_list.append(x)
                py_list.append(y)
                c_list.append(c)
                val_list.append(values[lane])

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
        width = (
            max(1, max_coords[0] - min_coords[0]) if min_coords and max_coords else 1
        )
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


@app.get("/funcs/{session_id}")
async def get_funcs(session_id: str) -> dict[str, Any]:
    if session_id not in _sessions:
        raise HTTPException(status_code=404, detail="session not found")
    trace = _sessions[session_id]
    return {name: _serialize_func_stats(s) for name, s in trace.funcs.items()}


@app.delete("/session/{session_id}")
async def delete_session(session_id: str) -> dict[str, str]:
    if session_id not in _sessions:
        raise HTTPException(status_code=404, detail="session not found")
    del _sessions[session_id]
    del _packets[session_id]
    del _store_indices[session_id]
    del _func_name_cache[session_id]

    return {"deleted": session_id}


def run() -> None:
    logging.basicConfig(level=logging.INFO)
    uvicorn.run(
        "backend.main:app", host="127.0.0.1", port=8765, reload=False, log_level="info"
    )
