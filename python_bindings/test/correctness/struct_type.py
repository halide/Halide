import halide as hl


# A block_q4_0-shaped struct, per doc/StructTypeDesign.md: one float32 scale
# followed by 8 packed byte-wide "codes". This mirrors the C++
# test/correctness/struct_type.cpp fixture (simplified to float32, since the
# Python bindings have no float16 literal support).
def make_block_type():
    return hl.Type.Struct([("d", hl.Float(32)), ("qs", hl.UInt(8), 8)])


def test_type_struct_basics():
    block_a = make_block_type()
    block_b = make_block_type()  # independently constructed, same layout
    reordered = hl.Type.Struct([("qs", hl.UInt(8), 8), ("d", hl.Float(32))])
    different_field = hl.Type.Struct([("d", hl.Float(32)), ("qs", hl.UInt(8), 4)])

    assert block_a.is_struct()
    assert block_b.is_struct()
    assert block_a.bytes() == 4 + 8
    assert block_a.code() == hl.TypeCode.UInt
    assert block_a.bits() == 8
    assert block_a.lanes() == 1

    assert block_a == block_b
    assert block_a.same_struct_type(block_b)
    assert block_a != reordered
    assert block_a != different_field
    assert block_a != hl.UInt(8)

    # with_bits/with_code/with_lanes must preserve struct-ness on a no-op change.
    assert block_a.with_bits(8).is_struct()
    assert not block_a.with_bits(16).is_struct()
    assert block_a.with_code(hl.TypeCode.UInt).is_struct()
    assert block_a.with_lanes(1).is_struct()

    info = block_a.struct_type
    assert info is not None
    assert info.total_bytes == 12
    assert [f.name for f in info.fields] == ["d", "qs"]
    assert info.fields[0].type == hl.Float(32)
    assert info.fields[1].type == hl.UInt(8)
    assert info.fields[1].array_extent == 8
    assert info.fields[0].array_extent is None
    assert list(info.offsets) == [0, 4]
    assert info.find_field("qs") == 1
    assert info.find_field("nope") == -1

    assert hl.Int(32).struct_type is None
    assert not hl.Int(32).is_struct()


# Struct types are treated as an ordinary opaque element type throughout the
# compiler: a Func whose value is pack_struct(...), consumed via field() by
# another Func, must produce identical results whether the producer is left
# at its default (inlined) schedule or explicitly compute_root()'d (which
# forces a real struct-typed buffer to be materialized).
def _build_pipeline(schedule):
    block_t = make_block_type()
    blk = hl.Var("blk")
    k = hl.Var("k")

    producer = hl.Func("producer")
    qs = [hl.u8(blk * 3 + kk) for kk in range(8)]
    producer[blk] = hl.pack_struct(block_t, [hl.f32(blk) + 0.5, *qs])

    schedule(producer)

    delta = hl.Func("delta")
    delta[blk] = hl.f32(hl.field(producer[blk], "d"))

    codes = hl.Func("codes")
    codes[k, blk] = hl.i32(hl.field(producer[blk], "qs")[k])

    return delta, codes


def _check_results(delta, codes, num_blocks):
    delta_buf = delta.realize([num_blocks])
    codes_buf = codes.realize([8, num_blocks])
    for b in range(num_blocks):
        assert abs(delta_buf[b] - (b + 0.5)) < 1e-6
        for k in range(8):
            assert codes_buf[k, b] == (b * 3 + k) % 256


def test_struct_pack_and_field_inlined():
    delta, codes = _build_pipeline(lambda f: None)
    _check_results(delta, codes, num_blocks=6)


def test_struct_pack_and_field_compute_root():
    delta, codes = _build_pipeline(lambda f: f.compute_root())
    _check_results(delta, codes, num_blocks=6)


# FieldRef itself: scalar fields implicitly convert to Expr, array fields
# support __getitem__/size()/__len__.
def test_field_ref_array_indexing():
    block_t = make_block_type()
    blk = hl.Var("blk")

    producer = hl.Func("producer")
    qs = [hl.u8(blk + kk) for kk in range(8)]
    producer[blk] = hl.pack_struct(block_t, [hl.f32(blk), *qs])

    qs_field = hl.field(producer[blk], "qs")
    assert qs_field.size() == 8
    assert len(qs_field) == 8

    out = hl.Func("out")
    k = hl.Var("k")
    out[k, blk] = hl.i32(qs_field[k])

    out_buf = out.realize([8, 3])
    for b in range(3):
        for k_ in range(8):
            assert out_buf[k_, b] == b + k_


if __name__ == "__main__":
    test_type_struct_basics()
    test_struct_pack_and_field_inlined()
    test_struct_pack_and_field_compute_root()
    test_field_ref_array_indexing()
