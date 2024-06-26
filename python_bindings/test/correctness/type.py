import halide as hl


def test_type():
    t1 = hl.Type()
    assert t1.code() == hl.TypeCode.Handle
    assert t1.bits() == 0
    assert t1.lanes() == 0

    t1 = hl.Type(hl.TypeCode.Int, 32, 1)
    t2 = hl.Int(32)
    assert t1 == t2
    assert t2.code() == hl.TypeCode.Int
    assert t2.bits() == 32
    assert t2.lanes() == 1
    assert t2.bytes() == 4

    t1 = t2.with_code(hl.TypeCode.UInt)
    assert t1 == hl.UInt(32)

    t1 = t2.with_bits(16)
    assert t1 == hl.Int(16)
    assert t1 != t2

    t1 = t2.with_lanes(8)
    assert t1 == hl.Int(32, 8)
    assert t1 != t2
    assert t1.element_of() == hl.Int(32)

    b1 = hl.Bool()
    f32 = hl.Float(32)
    h64 = hl.Handle()
    i32 = hl.Int(32)
    u32 = hl.UInt(32)
    vi32x8 = hl.Int(32, 8)
    u64 = hl.UInt(64)
    i64 = hl.Int(64)

    assert b1.is_bool()
    assert f32.is_float()
    assert h64.is_handle()
    assert i32.is_int()
    assert u32.is_uint()
    assert i64.is_int()
    assert u64.is_uint()

    assert not vi32x8.is_scalar()
    assert vi32x8.is_vector()

    h2 = hl.Handle()
    assert h64.same_handle_type(h2)

    assert f32.can_represent(b1)
    assert not b1.can_represent(f32)

    assert not b1.is_max(2)
    assert b1.is_max(1)
    assert b1.is_min(0)
    assert not b1.is_min(-1)

    assert i32.is_max(2147483647)
    assert i32.is_min(-2147483648)

    assert not u32.is_max(4294967296)
    assert u32.is_max(4294967295)
    assert u32.is_min(0)
    assert not u32.is_min(-1)
    assert not u32.is_min(1)

    assert i64.is_max(9223372036854775807)
    assert i64.is_min(-9223372036854775808)

    # Python doesn't have unsigned integers, so we can't really express this value.
    # assert u64.is_max(0xFFFFFFFFFFFFFFFF)
    assert u64.is_min(0)

    # repr() and str()
    assert str(i32) == "int32"
    assert repr(i32) == "<halide.Type int32>"
    assert str(vi32x8) == "int32x8"
    assert repr(vi32x8) == "<halide.Type int32x8>"


if __name__ == "__main__":
    test_type()
