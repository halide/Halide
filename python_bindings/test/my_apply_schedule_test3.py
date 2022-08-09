import halide as hl

def apply_schedule_test3 (pipeline, target):
    # --- BEGIN machine-generated schedule
    consumer = pipeline.get_func(1)
    producer = pipeline.get_func(0)
    x = hl.Var(consumer.get_schedule_dim_var_name(0))
    xi = hl.Var("xi")
    xii = hl.Var("xii")
    y = hl.Var(consumer.get_schedule_dim_var_name(1))
    yi = hl.Var("yi")
    consumer \
        .split(y, y, yi, 5, hl.TailStrategy.ShiftInwards) \
        .split(x, x, xi, 8, hl.TailStrategy.ShiftInwards) \
        .split(xi, xi, xii, 4, hl.TailStrategy.ShiftInwards) \
        .unroll(xi) \
        .unroll(yi) \
        .vectorize(xii) \
        .compute_root() \
        .reorder( xii, xi, yi, x, y ) \
        .parallel(y)

    producer \
        .store_in(hl.MemoryType.Stack) \
        .split(x, x, xi, 4, hl.TailStrategy.RoundUp) \
        .vectorize(xi) \
        .compute_at(consumer, x) \
        .reorder( xi, x, y )
    
    # --- END machine-generated schedule
