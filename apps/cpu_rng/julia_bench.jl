# Times Julia's stdlib bulk fill of the same-shaped Float32 array. Julia's
# default RNG is xoshiro256++, and Random.XoshiroSimd fills arrays with
# eight SIMD-interleaved substreams, single-threaded - the same generator
# and the same multi-stream technique this benchmark uses.
using Random
L = length(ARGS) >= 1 ? parse(Int, ARGS[1]) : 32
T = length(ARGS) >= 2 ? parse(Int, ARGS[2]) : 4 << 20
rng = Xoshiro(1234)
a = Array{Float32}(undef, L, T)
rand!(rng, a)
best = Inf
for _ in 1:3
    t0 = time_ns()
    rand!(rng, a)
    global best = min(best, (time_ns() - t0) / 1e3)
end
using Printf
@printf("  julia xoshiro %10.1f us  (Float32 rand!, single thread)\n", best)
