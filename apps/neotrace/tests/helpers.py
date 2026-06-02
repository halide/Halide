"""Shared test helpers."""

from __future__ import annotations

import struct
from enum import IntEnum


class EventCode(IntEnum):
    """halide_trace_event_code_t values from HalideRuntime.h."""

    LOAD = 0
    STORE = 1
    BEGIN_REALIZATION = 2
    END_REALIZATION = 3
    PRODUCE = 4
    END_PRODUCE = 5
    CONSUME = 6
    END_CONSUME = 7
    BEGIN_PIPELINE = 8
    END_PIPELINE = 9
    TAG = 10


class TypeCode(IntEnum):
    """halide_type_code_t values (matches halide.TypeCode member integers)."""

    INT = 0
    UINT = 1
    FLOAT = 2
    HANDLE = 3


def make_packet(
    packet_id: int,
    event: EventCode,
    parent_id: int,
    func: str,
    type_code: TypeCode = TypeCode.FLOAT,
    type_bits: int = 32,
    type_lanes: int = 1,
    coordinates: tuple[int, ...] = (),
    values: tuple[float | int, ...] = (),
    trace_tag: str = "",
    value_index: int = 0,
) -> bytes:
    """Encode a single trace packet in the Halide binary format."""
    coords_bytes = struct.pack(f"<{len(coordinates)}i", *coordinates)

    if type_code == TypeCode.FLOAT and type_bits == 32:
        values_bytes = struct.pack(f"<{len(values)}f", *values)
    elif type_code == TypeCode.FLOAT and type_bits == 64:
        values_bytes = struct.pack(f"<{len(values)}d", *values)
    elif type_code == TypeCode.INT:
        fmt = {8: "b", 16: "h", 32: "i", 64: "q"}[type_bits]
        values_bytes = struct.pack(f"<{len(values)}{fmt}", *values)
    elif type_code == TypeCode.UINT:
        fmt = {8: "B", 16: "H", 32: "I", 64: "Q"}[type_bits]
        values_bytes = struct.pack(f"<{len(values)}{fmt}", *values)
    else:
        values_bytes = b""

    func_bytes = func.encode("utf-8") + b"\x00"
    tag_bytes = trace_tag.encode("utf-8") + b"\x00"

    header_size = 28
    payload_size = (
        len(coords_bytes) + len(values_bytes) + len(func_bytes) + len(tag_bytes)
    )
    total_size = header_size + payload_size

    header = struct.pack(
        "<IiBBHiiii",
        total_size,
        packet_id,
        int(type_code),
        type_bits,
        type_lanes,
        int(event),
        parent_id,
        value_index,
        len(coordinates),
    )
    return header + coords_bytes + values_bytes + func_bytes + tag_bytes
