Times are milliseconds per run; inductive = the folded inductive-Func form, RDom = the same algorithm as update definitions (materializing), baseline = the fastest non-Halide implementation available on this machine.

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
| ode | D=1024, B=1, T=32768 | - | - | - | Boost.odeint (needs libboost-dev) | - | - |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 1 thread | 44.5 | 54.4 | - | oneTBB (needs libtbb-dev) | 1.22x | - |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 32 threads | 2.5 | 4.6 | - | oneTBB (needs libtbb-dev) | 1.84x | - |
| chebyshev | n=2048 dense SPD, 100 iterations, 1 thread (in-cache control) | 156.7 | 136.2 | 136.4 | hand-written mod-3 ring | 0.87x | 0.87x |
| cuda_mamba2 | fwd, seq 4096, state 64, 128 heads x 64, chunk 64, RTX 5060 Ti | 1.2 | 2.0 | 1.7 | Triton (mamba_ssm) | 1.68x | 1.46x |
| cuda_mamba2 | bwd, seq 4096, state 64, 128 heads x 64, chunk 64, RTX 5060 Ti | 3.7 | 7.3 | 5.4 | Triton (mamba_ssm) | 2.00x | 1.46x |

Notes: every Halide form is verified against its app's reference before timing (the alignment rows byte-exact against ksw2; the rng rows bit-exact against Julia's rand! seeding). Baselines run threaded across independent problems in the all-cores rows where the library allows it (ksw2, parasail, the alignment runner's traceback); the rng hand kernel and Julia's rand! are single-threaded, so the rng all-cores baseline is the single-thread hand kernel. The ODE and prefixsum external baselines need libboost-dev and libtbb-dev; chebyshev is the intended in-cache control, where folding buys nothing.
