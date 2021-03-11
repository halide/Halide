
apply_schedule_simple = function( pipeline, target) 
    -- --- BEGIN machine-generated schedule
    gradient = pipeline:get_func(0)
    x = Var_new(gradient:get_schedule():dims()[1]:var())
    xi = Var_new("xi")
    y = Var_new(gradient:get_schedule():dims()[2]:var())
    yi = Var_new("yi")
    gradient
        :split(y, y, yi, 32, TailStrategy.ShiftInwards)
        :split(x, x, xi, 4, TailStrategy.ShiftInwards)
        :vectorize(xi)
        :compute_root()
        :reorder( xi, x, yi, y )
        :parallel(y)
    -- --- END machine-generated schedule
end
