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

def test_unscheduled(suppress):
    x = hl.Var()
    f = hl.Func("f_%s" % str(suppress))
    f[x] = 0
    f[x] += 5
    f.vectorize(x, 8)
    if suppress:
        f.update(0).unscheduled()

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        f.realize([1024])

    buffer.seek(0)
    stdout_lines = buffer.readlines()
    if suppress:
        assert len(stdout_lines) == 0
    else:
        expected_warning = "Warning: Update definition 0 of function f_False has not been scheduled"
        assert len(stdout_lines) > 0
        for line in stdout_lines:
            assert line.startswith(expected_warning), "\n%s\n%s" % (line, expected_warning)

if __name__ == "__main__":
    test_warnings()
    test_unscheduled(True)
    test_unscheduled(False)
