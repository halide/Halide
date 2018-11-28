from __future__ import print_function
import halide as hl

# TODO: Func.evaluate() needs a wrapper added;
# this is a temporary equivalent for testing purposes
def _evaluate(e):
    # TODO: support zero-dim Func, Buffers
    buf = hl.Buffer(type = e.type(), sizes = [1])
    f = hl.Func();
    x = hl.Var()
    f[x] = e;
    f.realize(buf)
    return buf[0]

def test_division():
    f32 = hl.Param(hl.Float(32), 'f32', -32.0)
    f64 = hl.Param(hl.Float(64), 'f64', 64.0)
    i16 = hl.Param(hl.Int(16), 'i16', -16)
    i32 = hl.Param(hl.Int(32), 'i32', 32)
    u16 = hl.Param(hl.UInt(16), 'u16', 16)
    u32 = hl.Param(hl.UInt(32), 'u32', 32)

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

    # Verify that division semantics match those for Halide
    # (rather than python); this differs for int/int which
    # defaults to float (rather than floordiv) in Python3.
    # Also test that // always floors the result, even for float.
    assert _evaluate(f32 / f64) == -0.5
    assert _evaluate(f32 // f64) == -1.0

    assert _evaluate(i16 / i32) == -1
    assert _evaluate(i16 // i32) == -1
    assert _evaluate(i32 / i16) == -2

    assert _evaluate(u16 / u32) == 0
    assert _evaluate(u16 // u32) == 0

    assert _evaluate(u16 / i32) == 0
    assert _evaluate(i32 // u16) == 2

    assert _evaluate(u16 / f32) == -0.5
    assert _evaluate(u16 // f32) == -1.0

    assert _evaluate(i16 / f64) == -0.25
    assert _evaluate(i16 // f64) == -1.0

if __name__ == "__main__":
    test_division()
