"""Tests for the trace reader."""

import struct

from neotrace.trace import (
    _NATIVE_TRACE_AVAILABLE,
    EventCode,
    HalideType,
    Trace,
    TypeCode,
)


def test_native_availability():
    """Report which trace implementation is being used."""
    # This test just reports the status, doesn't assert
    print(f"Native trace module available: {_NATIVE_TRACE_AVAILABLE}")


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
    """Create a binary trace packet."""
    # Coordinates
    coords_bytes = struct.pack(f"<{len(coordinates)}i", *coordinates)

    # Values
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

    # Strings
    func_bytes = func.encode("utf-8") + b"\x00"
    tag_bytes = trace_tag.encode("utf-8") + b"\x00"

    # Header
    header_size = 28
    payload_size = (
        len(coords_bytes) + len(values_bytes) + len(func_bytes) + len(tag_bytes)
    )
    total_size = header_size + payload_size

    header = struct.pack(
        "<IiBBHiiii",
        total_size,
        packet_id,
        type_code,
        type_bits,
        type_lanes,
        event,
        parent_id,
        value_index,
        len(coordinates),
    )

    return header + coords_bytes + values_bytes + func_bytes + tag_bytes


def test_halide_type_str():
    """Test HalideType string representation."""
    assert str(HalideType(TypeCode.FLOAT, 32, 1)) == "float32"
    assert str(HalideType(TypeCode.FLOAT, 32, 4)) == "float32x4"
    assert str(HalideType(TypeCode.INT, 16, 1)) == "int16"
    assert str(HalideType(TypeCode.UINT, 8, 1)) == "uint8"


def test_empty_trace():
    """Test loading an empty trace."""
    trace = Trace.load_bytes(b"")
    assert len(trace) == 0
    assert len(trace.funcs) == 0


def test_single_packet():
    """Test loading a trace with a single store packet."""
    packets = [
        make_packet(1, EventCode.BEGIN_PIPELINE, 0, "test_pipeline"),
        make_packet(
            2,
            EventCode.STORE,
            1,
            "f",
            coordinates=(5, 10),
            values=(42.0,),
        ),
        make_packet(3, EventCode.END_PIPELINE, 1, "test_pipeline"),
    ]

    trace = Trace.load_bytes(b"".join(packets))
    assert len(trace) == 3

    # Check the store packet
    store = trace.packets[1]
    # event may be EventCode enum (Python) or int (native)
    assert int(store.event) == int(EventCode.STORE)
    assert store.func == "f"
    # coordinates may be tuple (Python) or list (native)
    assert list(store.coordinates) == [5, 10]
    assert store.get_values() == [42.0]


def test_func_stats():
    """Test that func statistics are computed correctly."""
    packets = [
        make_packet(1, EventCode.BEGIN_PIPELINE, 0, "pipeline"),
        make_packet(
            2, EventCode.BEGIN_REALIZATION, 1, "blur", coordinates=(0, 10, 0, 10)
        ),
        make_packet(3, EventCode.STORE, 2, "blur", coordinates=(0, 0), values=(100.0,)),
        make_packet(4, EventCode.STORE, 2, "blur", coordinates=(5, 5), values=(200.0,)),
        make_packet(5, EventCode.STORE, 2, "blur", coordinates=(9, 9), values=(50.0,)),
        make_packet(6, EventCode.END_REALIZATION, 2, "blur"),
        make_packet(7, EventCode.END_PIPELINE, 1, "pipeline"),
    ]

    trace = Trace.load_bytes(b"".join(packets))

    # Find the blur func
    blur_stats = None
    for name, stats in trace.funcs.items():
        if "blur" in name:
            blur_stats = stats
            break

    assert blur_stats is not None
    assert blur_stats.min_value == 50.0
    assert blur_stats.max_value == 200.0
    assert blur_stats.min_coords[0] == 0
    assert blur_stats.max_coords[0] == 10  # max is exclusive


def test_filter_loads_stores():
    """Test filtering for only loads and stores."""
    packets = [
        make_packet(1, EventCode.BEGIN_PIPELINE, 0, "test"),
        make_packet(2, EventCode.STORE, 1, "f", coordinates=(0,), values=(1.0,)),
        make_packet(3, EventCode.LOAD, 1, "f", coordinates=(0,), values=(1.0,)),
        make_packet(4, EventCode.END_PIPELINE, 1, "test"),
    ]

    trace = Trace.load_bytes(b"".join(packets))
    filtered = trace.filter_loads_stores()

    assert len(filtered) == 2
    assert all(p.is_load_or_store for p in filtered)
