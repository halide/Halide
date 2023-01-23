import halide as hl


def test_var():
    v1 = hl.Var()
    v2 = hl.Var()
    assert len(v1.name()) > 0
    assert len(v2.name()) > 0
    assert not v1.same_as(v2)

    v1 = hl.Var.implicit(1)
    assert v1.name() == "_1"
    v2 = hl.Var("_1")
    assert v1.same_as(v2)
    v3 = hl._1
    assert v1.same_as(v3)
    v4 = hl.Var("v4")
    assert not v1.same_as(v4)

    assert v1.is_implicit()
    assert v2.is_implicit()
    assert v3.is_implicit()
    assert not v4.is_implicit()

    assert hl.Var("_1").is_implicit()
    assert not hl.Var("v4").is_implicit()

    assert v1.implicit_index() == 1
    assert v2.implicit_index() == 1
    assert v3.implicit_index() == 1
    assert v4.implicit_index() == -1

    assert hl.Var("_1").implicit_index() == 1
    assert hl.Var("v4").implicit_index() == -1

    ph = hl._
    assert ph.name() == "_"
    assert ph.is_placeholder()
    assert hl.Var.is_placeholder(ph)
    assert not v1.is_placeholder()

    outermost = hl.Var.outermost()
    assert outermost.name() == "__outermost"

    # repr() and str()
    x = hl.Var("x")
    assert str(x) == "x"
    assert repr(x) == "<halide.Var 'x'>"

    # This verifies that halide.Var is implicitly convertible to halide.Expr
    r = hl.random_int(x)

    # This verifies that halide.Var is explicitly convertible to halide.Expr
    r = hl.random_int(hl.Expr(x))


if __name__ == "__main__":
    test_var()
