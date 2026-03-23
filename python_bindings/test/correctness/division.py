import halide as hl

f32 = hl.Param(hl.Float(32), "f32", -32.0)
f64 = hl.Param(hl.Float(64), "f64", 64.0)
i16 = hl.Param(hl.Int(16), "i16", -16)
i32 = hl.Param(hl.Int(32), "i32", 32)
u16 = hl.Param(hl.UInt(16), "u16", 16)
u32 = hl.Param(hl.UInt(32), "u32", 32)


def test_types():
    # Verify that the types match the rules in match_types()
    assert (f32 / f64).type() == hl.Float(64)
    assert (f32 // f64).type() == hl.Float(64)

    assert (i16 / i32).type() == hl.Int(32)
    assert (i16 // i32).type() == hl.Int(32)

    assert (u16 / u32).type() == hl.UInt(32)
    assert (u16 // u32).type() == hl.UInt(32)

    # int / uint -> int
    assert (u16 / i32).type() == hl.Int(32)
    assert (i32 // u16).type() == hl.Int(32)

    # any / float -> float
    # float / any -> float
    assert (u16 / f32).type() == hl.Float(32)
    assert (u16 // f32).type() == hl.Float(32)

    assert (i16 / f64).type() == hl.Float(64)
    assert (i16 // f64).type() == hl.Float(64)


def test_division():
    # Verify that division semantics match those for Halide
    # (rather than python); this differs for int/int which
    # defaults to float (rather than floordiv) in Python3.
    # Also test that // always floors the result, even for float.
    assert hl.evaluate(f32 / f64) == -0.5
    assert hl.evaluate(f32 // f64) == -1.0

    assert hl.evaluate(i16 / i32) == -1
    assert hl.evaluate(i16 // i32) == -1
    assert hl.evaluate(i32 / i16) == -2

    assert hl.evaluate(u16 / u32) == 0
    assert hl.evaluate(u16 // u32) == 0

    assert hl.evaluate(u16 / i32) == 0
    assert hl.evaluate(i32 // u16) == 2

    assert hl.evaluate(u16 / f32) == -0.5
    assert hl.evaluate(u16 // f32) == -1.0

    assert hl.evaluate(i16 / f64) == -0.25
    assert hl.evaluate(i16 // f64) == -1.0


def test_division_tupled():
    # Same as test_division, but using the tuple variant
    assert hl.evaluate((f32 / f64, f32 // f64)) == (-0.5, -1.0)
    assert hl.evaluate((i16 / i32, i16 // i32, i32 / i16)) == (-1, -1, -2)
    assert hl.evaluate((u16 / u32, u16 // u32)) == (0, 0)
    assert hl.evaluate((u16 / i32, i32 // u16)) == (0, 2)
    assert hl.evaluate((u16 / f32, u16 // f32)) == (-0.5, -1.0)
    assert hl.evaluate((i16 / f64, i16 // f64)) == (-0.25, -1.0)


def test_division_gpu():
    # Allow GPU usage -- don't use f64 since not all GPU backends support that
    f = hl.cast(hl.Float(32), f64)
    assert hl.evaluate_may_gpu((f32 / f, f32 // f)) == (-0.5, -1.0)
    assert hl.evaluate_may_gpu((i16 / i32, i16 // i32, i32 / i16)) == (-1, -1, -2)
    assert hl.evaluate_may_gpu((u16 / u32, u16 // u32)) == (0, 0)
    assert hl.evaluate_may_gpu((u16 / i32, i32 // u16)) == (0, 2)
    assert hl.evaluate_may_gpu((u16 / f32, u16 // f32)) == (-0.5, -1.0)
    assert hl.evaluate_may_gpu((i16 / f, i16 // f)) == (-0.25, -1.0)


if __name__ == "__main__":
    test_types()
    test_division()
    test_division_tupled()
    test_division_gpu()
