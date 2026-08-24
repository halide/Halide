import halide as hl


def test_autodiff():
    x = hl.Var("x")
    b = hl.Buffer(hl.Float(32), [3])
    p = hl.Param(hl.Float(32), "p", 1)
    b[0] = 1.0
    b[1] = 2.0
    b[2] = 3.0
    f, g, h = hl.Func("f"), hl.Func("g"), hl.Func("h")
    f[x] = b[x]
    f[0] = 4.0
    g[x] = f[x] * 5.0 * p
    r = hl.RDom([(0, 3)])
    h[()] = 0.0
    h[()] += g[r.x]

    d = hl.propagate_adjoints(h)

    # gradient w.r.t. the initialization of f
    d_f_init = d[f]
    d_f_init_buf = d_f_init.realize([3])
    assert d_f_init_buf[0] == 0.0
    assert d_f_init_buf[1] == 5.0
    assert d_f_init_buf[2] == 5.0
    d_f_init = d[f]  # test different interface
    d_f_init_buf = d_f_init.realize([3])
    assert d_f_init_buf[0] == 0.0
    assert d_f_init_buf[1] == 5.0
    assert d_f_init_buf[2] == 5.0

    # gradient w.r.t. the updated f
    d_f_update_0 = d[f, 0]
    d_f_update_0_buf = d_f_update_0.realize([3])
    assert d_f_update_0_buf[0] == 5.0
    assert d_f_update_0_buf[1] == 5.0
    assert d_f_update_0_buf[2] == 5.0
    d_f_update_0 = d[f, 0]
    d_f_update_0_buf = d_f_update_0.realize([3])
    assert d_f_update_0_buf[0] == 5.0
    assert d_f_update_0_buf[1] == 5.0
    assert d_f_update_0_buf[2] == 5.0

    # gradient w.r.t. the buffer
    d_b = d[b]
    d_b_buf = d_b.realize([3])
    assert d_b_buf[0] == 0.0
    assert d_b_buf[1] == 5.0
    assert d_b_buf[2] == 5.0
    d_b = d[b]
    d_b_buf = d_b.realize([3])
    assert d_b_buf[0] == 0.0
    assert d_b_buf[1] == 5.0
    assert d_b_buf[2] == 5.0

    # gradient w.r.t. the param
    d_p = d[p]
    d_p_buf = d_p.realize()
    # 5 * (4 + 2 + 3)
    assert abs(d_p_buf[()] - 45.0) < 1e-6
    d_p = d[p]
    d_p_buf = d_p.realize()
    assert abs(d_p_buf[()] - 45.0) < 1e-6


if __name__ == "__main__":
    test_autodiff()
