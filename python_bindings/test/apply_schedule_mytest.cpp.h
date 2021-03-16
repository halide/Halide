// --- BEGIN machine-generated schedule
Func f_2 = pipeline.get_func(3);
Func f_1 = pipeline.get_func(2);
Func f_0 = pipeline.get_func(1);
Func in = pipeline.get_func(0);
Var x(f_2.get_schedule().dims()[0].var);
Var xi("xi");
Var xii("xii");
f_2
    .split(x, x, xi, 32, TailStrategy::ShiftInwards)
    .split(xi, xi, xii, 4, TailStrategy::ShiftInwards)
    .unroll(xi)
    .vectorize(xii)
    .compute_root()
    .reorder({xii, xi, x})
    .parallel(x);
// --- END machine-generated schedule
