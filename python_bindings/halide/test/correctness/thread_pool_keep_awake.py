import halide as hl


def make_pipeline():
    x, y = hl.Var("x"), hl.Var("y")
    f = hl.Func("f")
    f[x, y] = x * 3 + y
    f.parallel(y)
    return f


def check(buf, width, height):
    for y in range(height):
        for x in range(width):
            assert buf[x, y] == x * 3 + y, (x, y, buf[x, y])


def test_with_statement():
    f = make_pipeline()
    with hl.ThreadPoolKeepAwake() as keep_awake:
        assert repr(keep_awake) == "<halide.ThreadPoolKeepAwake>"
        for i in range(100):
            check(f.realize([16, 1 + i % 8]), 16, 1 + i % 8)
        # Holders nest.
        with hl.ThreadPoolKeepAwake():
            check(f.realize([16, 8]), 16, 8)
        check(f.realize([16, 8]), 16, 8)
    check(f.realize([16, 8]), 16, 8)


def test_exit():
    f = make_pipeline()
    keep_awake = hl.ThreadPoolKeepAwake()
    check(f.realize([16, 8]), 16, 8)
    # Releasing is idempotent, and leaving the with-statement afterwards
    # doesn't release a second reference.
    keep_awake.exit()
    keep_awake.exit()
    with hl.ThreadPoolKeepAwake() as k:
        k.exit()
        check(f.realize([16, 8]), 16, 8)
    check(f.realize([16, 8]), 16, 8)


def test_exception():
    f = make_pipeline()
    try:
        with hl.ThreadPoolKeepAwake():
            check(f.realize([16, 8]), 16, 8)
            raise ValueError("expected")
    except ValueError:
        pass
    check(f.realize([16, 8]), 16, 8)


def main():
    test_with_statement()
    test_exit()
    test_exception()


if __name__ == "__main__":
    main()
