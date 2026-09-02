# Times Julia's stdlib bulk fill of the same-shaped Float32 array. Julia's
# default RNG is xoshiro256++, and Random.XoshiroSimd fills arrays with
# eight SIMD-interleaved substreams, single-threaded - the same generator
# and the same multi-stream technique this benchmark uses.
using Random
# L counts float rows: two per stream, so 2x the generator's lanes param.
L = length(ARGS) >= 1 ? parse(Int, ARGS[1]) : 64
T = length(ARGS) >= 2 ? parse(Int, ARGS[2]) : 4 << 20
rng = Xoshiro(1234)
a = Array{Float32}(undef, L, T)
# The shared protocol (apps/support/bench_harness.h): HB_WARMUP untimed
# runs, HB_TRIALS timed ones, the best reported.
for _ in 1:parse(Int, get(ENV, "HB_WARMUP", "3"))
    rand!(rng, a)
end
best = Inf
for _ in 1:parse(Int, get(ENV, "HB_TRIALS", "30"))
    t0 = time_ns()
    rand!(rng, a)
    global best = min(best, (time_ns() - t0) / 1e3)
end
using Printf
@printf("  julia xoshiro %10.1f us  (Float32 rand!, single thread)\n", best)
