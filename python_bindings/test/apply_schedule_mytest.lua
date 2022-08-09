-- --- BEGIN machine-generated schedule
f_2 = pipeline:get_func(3)
f_1 = pipeline:get_func(2)
f_0 = pipeline:get_func(1)
in = pipeline:get_func(0)
x = Var:new(f_2:get_schedule():dims()[0]:var())
xi = Var:new("xi")
xii = Var:new("xii")
f_2
    :split(x, x, xi, 32, TailStrategy.ShiftInwards)
    :split(xi, xi, xii, 4, TailStrategy.ShiftInwards)
    :unroll(xi)
    :vectorize(xii)
    :compute_root()
    :reorder({xii, xi, x})
    :parallel(x)
-- --- END machine-generated schedule
