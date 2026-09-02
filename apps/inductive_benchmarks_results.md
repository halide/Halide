Times are milliseconds per run: inductive = the folded inductive-Func form, RDom = the same algorithm as update definitions (materializing), baseline = the fastest non-Halide implementation available on this machine. Measured on a Threadripper 9970X (Zen 5, 32 cores) and an RTX 5060 Ti.

Every Halide form is verified against its app's reference before timing (the alignment rows byte-exact against ksw2, the rng rows bit-exact against Julia's rand! seeding, the GPU rows against serial references). Baselines are threaded across independent problems in the all-cores rows where the library allows it (ksw2, parasail, oneTBB); the rng hand kernel and Julia's rand! are single-threaded, so the rng all-cores baseline is the single-thread hand kernel. The mamba2 rows compare each side at its own best chunk (Triton prefers 256; the Halide backward is best at 128), with the tensor-core schedules (WMMA=true). Flash attention has no RDom form by construction; its materialized-scores alternative is cuBLAS + softmax + cuBLAS, about 13x slower. Chebyshev is the intended in-cache control, where folding buys nothing.

| App | Config | Inductive (ms) | RDom (ms) | Fastest baseline (ms) | Baseline | RDom / ind | Baseline / ind |
|---|---|---:|---:|---:|---|---:|---:|
| cpu_biquads | 8 sections, 32 ch x 8388608 samples, 1 thread | 161.8 | 1048.3 | 1972.1 | scipy.sosfilt | 6.48x | 12.19x |
| cpu_biquads | 8 sections, 256 ch x 1048576 samples, all cores | 46.0 | 436.7 | - | - | 9.48x | - |
| cpu_rng | 32 streams x 4194304 steps, 1 thread | 35.4 | 496.0 | 35.1 | Julia rand! | 14.03x | 0.99x |
| cpu_rng | 1024 streams x 131072 steps, all cores | 29.7 | 219.9 | 54.2 | hand AVX-512 kernel | 7.40x | 1.82x |
| cpu_alignment | 1024x1024 x 128 pairs, 1 thread, fill+traceback+cigar | 14.2 | 52.3 | 50.8 | parasail | 3.68x | 3.57x |
| cpu_alignment | 1024x1024 x 4096 pairs, all cores, fill+traceback+cigar | 40.2 | 728.3 | 46.3 | parasail | 18.11x | 1.15x |
| kalman_ll | 256 series x 16384 steps, 1 thread | 11.5 | 30.3 | - | - | 2.64x | - |
| kalman_ll | 256 series x 16384 steps, 32 threads | 0.889 | 6.2 | - | - | 7.00x | - |
| viterbi | 16 states, 4 symbols, T=320000, 1 thread | 5.7 | 13.4 | - | - | 2.34x | - |
| viterbi | 64 states, 8 symbols, T=50000, 1 thread | 9.8 | 13.1 | - | - | 1.34x | - |
| ode | Allen-Cahn D=1024, batch 1, T=32768, 1 thread | 6.0 | 26.0 | 28.9 | Boost.odeint | 4.32x | 4.80x |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 1 thread | 37.3 | 45.6 | 37.3 | oneTBB parallel_scan | 1.22x | 1.00x |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 32 threads | 2.3 | 3.5 | 3.1 | oneTBB parallel_scan | 1.54x | 1.37x |
| chebyshev | n=2048 dense SPD, 100 iterations, 1 thread (in-cache control) | 156.7 | 136.2 | 136.4 | hand-written mod-3 ring | 0.87x | 0.87x |
| cuda_mamba2 | fwd, seq 4096, state 128, 128 heads x 64, chunk 256 (Triton 256), RTX 5060 Ti | 1.6 | 1.7 | 1.6 | Triton (mamba_ssm) | 1.09x | 0.99x |
| cuda_mamba2 | bwd, seq 4096, state 128, 128 heads x 64, chunk 128 (Triton 256), RTX 5060 Ti | 4.5 | 8.0 | 4.4 | Triton (mamba_ssm) | 1.79x | 0.99x |
| flash_attention | 65536 queries x 1024 keys, depth 64, fp16, RTX 5060 Ti | 0.396 | - | 0.384 | FlashAttention-2 (torch SDPA) | - | 0.97x |
