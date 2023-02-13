import halide as hl
import io
import contextlib


def test_warnings():
    i = hl.Var()
    f = hl.Func("f")
    f[i] = 0.0
    f.bound(i, 0, 127)

    g = hl.Func("g")
    g[i] = f[i * i]

    expected_warning = (
        "Warning: It is meaningless to bound dimension v0 of function f to be within [0, 127] because "
        "the function is scheduled inline.\n"
    )

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        g.realize([16])

    buffer.seek(0)
    stdout_lines = buffer.readlines()
    assert len(stdout_lines) > 0
    for line in stdout_lines:
        assert line == expected_warning


if __name__ == "__main__":
    test_warnings()
