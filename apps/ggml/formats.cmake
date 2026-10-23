# The format table. One row per format (schemes/schemes.h):
#
#   type  ops  [acts  [target-features]]
#
# `target-features` (comma-separated, e.g. metal) are added to the libraries'
# target; the vec_dot and mul_mat libraries' names end in them
# (q4_0_f32_mul_mat_metal), so a format can have both a CPU and a GPU row.
#
# `type`: <ggml type>[.<codes>][.<rows>x<chunk>]. The optional codes pick the
# integer encoding (i4: two's-complement nibbles; default: GGML's offset
# binary), the layout interleaves `rows` rows' records in `chunk`-byte pieces
# (as GGML's repack q4_0_4x8, q8_0_4x4, q4_K_8x8, q5_K_8x8, q6_K_8x8, iq4_nl_4x4 etc.;
# q1_0.4x1 is GGML's q1_0_4x8, its interleave in bits; default: one row per record,
# AoS); its weights are checked against GGML's repack (harness/repack.cpp).
# q8_K.4x8 (GGML's block_q8_Kx4, an activation layout) is checked also
# against GGML's quantize_mat_q8_K_4x8 (its transcription, and GGML's own
# ggml_quantize_mat_t).
# `soa`: one planar array per field (a kernel input each), checked against
# GGML's bytes laid out alike. `acc16` (f16.acc16): the dot's partial sums
# approximated in f16 (an Accumulate), in the order and roundings of GGML's
# NEON f16 vec_dot (where the CPU has fp16 arithmetic and K % 32 == 0,
# checked bit-exact against it); else in f32. Its rows need strict_float (the
# order is its numerics: LLVM may otherwise reassociate the partial sums).
#
# qsi8cx (weights) and qai8dx (acts) are KleidiAI's per-row int8 formats
# (schemes/kleidiai.h; no GGML type): their mul_mat rows are timed under
# GGML's q8_0, whose weights the harness requantizes to qsi8cx at setup, as
# GGML's KleidiAI backend does on load. Their codec is checked against GGML's
# requantization and KleidiAI's packers.
#
# `ops`: codec (quantize + dequantize of a row, checked bit-exact against
# GGML, also with the scaled codes kernels may pick: checked variant only;
# activation-only types such as q8_1 have just this),
# vec_dot and mul_mat (kernels/matmul.cpp with weight=type op=<op>, one
# library per act in `acts`). An act is <storage>[:<compute>]: f32:q8_0 takes
# f32 activations and quantizes them to q8_0 inside the kernel (as GGML's CPU
# paths do); its library is <type>_f32_to_q8_0_<op>. A compute type with a
# layout (f32:q8_0.4x8, f32:q8_K.4x8) quantizes the whole groups of rows to
# its records, as GGML's repacked gemm does, and the rest to the AoS type (its
# gemv). Each generated library
# has a checked variant (default target: asserts and bounds queries on; used
# by --check and tests) and a bench variant
# (GGML_BENCH_FEATURES; what gets timed), both registered with the harness
# (harness/halide_providers.cpp, harness/codec.cpp).
set(GGML_FORMATS
    "q4_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q8_0 codec,vec_dot q8_0,f16,f32"
    "q8_0 mul_mat q8_0,f32:q8_0"
    "q4_1 codec,vec_dot,mul_mat q8_1,f16,f32,f32:q8_1"
    "q8_1 codec"
    "q8_K codec"
    "q4_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q6_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q5_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q2_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q3_K codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q5_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "q5_1 codec,vec_dot,mul_mat q8_1,f16,f32,f32:q8_1"
    "mxfp4 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq4_nl codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq4_xs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "nvfp4 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "tq2_0 codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "tq1_0 codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q1_0 codec,vec_dot,mul_mat q8_0,f16,f32,f32:q8_0"
    "iq2_xxs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq2_xs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq2_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq3_xxs codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq3_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq1_s codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "iq1_m codec,vec_dot,mul_mat q8_K,f16,f32,f32:q8_K"
    "q4_0.i4.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "q4_0.i4.4x8 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q4_0.i4.8x8 codec"
    "q4_0.soa codec,mul_mat q8_0,f32:q8_0"
    "q8_0.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "iq4_nl.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "mxfp4.4x4 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x4"
    "q1_0.4x1 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q8_0.4x8 codec,mul_mat q8_0,f32:q8_0,f32:q8_0.4x8"
    "q8_0.soa codec,mul_mat q8_0,f32:q8_0"
    "q8_K.4x8 codec"
    "q4_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "q5_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "q6_K.8x8 codec,mul_mat q8_K,f32:q8_K,f32:q8_K.4x8"
    "qsi8cx codec,mul_mat qai8dx,f32:qai8dx"
    "qsi8cx.4x8 codec,mul_mat qai8dx,f32:qai8dx"
    "qai8dx codec"
    "f16 codec,vec_dot,mul_mat f16,f32,f32:f16"
    "bf16 codec,vec_dot,mul_mat bf16,f32,f32:bf16"
    "f16.acc16 vec_dot,mul_mat f16,f32:f16 strict_float"
    "q4_0 mul_mat f32,f16,q8_0 metal"
    "q4_0.soa mul_mat f32,f16,q8_0 metal"
    "q8_0 mul_mat f32,f16,q8_0 metal"
    "q8_0.soa mul_mat f32,f16,q8_0 metal"
    "q4_1 mul_mat f32,f16,q8_1 metal"
    "q5_0 mul_mat f32,f16,q8_0 metal"
    "q5_1 mul_mat f32,f16,q8_1 metal"
    "mxfp4 mul_mat f32,f16,q8_0 metal"
    "iq4_nl mul_mat f32,f16,q8_0 metal"
    "f16 mul_mat f16,f32,f32:f16 metal"
    "bf16 mul_mat bf16,f32,f32:bf16 metal"
)
set(GGML_BENCH_FEATURES no_asserts no_bounds_query)

# Schedule parameters (kernels/matmul.cpp's GeneratorParams) of some of the
# rows' libraries: `type op act params...`. interleave=1 (one block per
# iteration of vec_dot and of mul_mat's gemv tiles, default 2) where measured
# faster on Apple M3: a q8_K act's block is 292 bytes, so a second block's
# loads fall outside ldur's immediate range and each needs its own address.
# sublanes=true (vec_dot: the sub-blocks across the vector lanes, their
# scales one vector multiply-add per 4 sub-blocks) where measured faster: it
# also cuts q4_K-q6_K's loops by ~15% in instructions, not in time (latency
# bound), and slows iq4_xs, iq2_s and iq1_m. slp=false (not a GeneratorParam:
# the generator runs with HL_LLVM_ARGS=-vectorize-slp=false) where LLVM's SLP
# vectorizer packs the scalar grid-index arithmetic (shifts and masks of qh)
# into vectors, each index then extracted lane by lane for its table load:
# iq1_s's and iq1_m's loops twice the SIMD instructions, ~15-30% slower.
# gpr_scales=true (vec_dot and the gemv tiles: the 6-bit sub-block scales
# and mins decoded in GPRs, GGML's SWAR; not the gemm tiles, where they cost
# q5_K's 20%): q4_K vec_dot +7-12%, q5_K vec_dot +4-7% and gemv +4-9% (one
# thread). An act of `*`: every act.
set(GGML_SCHEDULE_PARAMS
    "q4_K vec_dot * gpr_scales=true"
    "q4_K mul_mat * gpr_scales=true"
    "q5_K vec_dot * gpr_scales=true"
    "q5_K mul_mat * gpr_scales=true"
    "q2_K vec_dot q8_K interleave=1 sublanes=true"
    "iq2_xs vec_dot q8_K interleave=1 sublanes=true"
    "q3_K vec_dot q8_K interleave=1"
    "q4_K vec_dot q8_K interleave=1"
    "iq2_xxs vec_dot q8_K interleave=1"
    "iq3_xxs vec_dot q8_K interleave=1"
    "iq1_s vec_dot q8_K interleave=1 slp=false"
    "iq1_m vec_dot q8_K slp=false"
    "iq4_xs vec_dot q8_K interleave=1"
    "iq2_xxs mul_mat q8_K interleave=1"
    "iq2_xxs mul_mat f32:q8_K interleave=1"
    "iq3_xxs mul_mat q8_K interleave=1"
    "iq3_xxs mul_mat f32:q8_K interleave=1"
    "iq1_s mul_mat q8_K interleave=1 slp=false"
    "iq1_m mul_mat q8_K slp=false"
    "iq1_s mul_mat f32:q8_K interleave=1"
    "iq4_xs mul_mat q8_K interleave=1"
    "iq4_xs mul_mat f32:q8_K interleave=1"
)

set(GGML_HALIDE_LIBRARIES "")
set(registry "")

# Builds the checked and bench variants (`_ggml_variants`, if set: those),
# the generator run with `_ggml_env` (VAR=value...), if set.
function(_ggml_library name gen features)
    if (NOT _ggml_variants)
        set(_ggml_variants checked bench)
    endif ()
    set(features_checked "")
    if (NOT features STREQUAL "-")
        string(REPLACE "," ";" features_checked "${features}")
    endif ()
    set(features_bench ${features_checked} ${GGML_BENCH_FEATURES})
    foreach (variant IN LISTS _ggml_variants)
        add_halide_library(
            ${name}_${variant}
            FROM ggml.generators
            GENERATOR ${gen}
            FEATURES ${features_${variant}}
            PARAMS ${ARGN}
            ENVIRONMENT ${_ggml_env}
            USE_RUNTIME ggml_halide_runtime
        )
        list(APPEND GGML_HALIDE_LIBRARIES ${name}_${variant})
        string(APPEND registry "#include \"${name}_${variant}.h\"\n")
    endforeach ()
    set(GGML_HALIDE_LIBRARIES "${GGML_HALIDE_LIBRARIES}" PARENT_SCOPE)
    set(registry "${registry}" PARENT_SCOPE)
endfunction()

foreach (row IN LISTS GGML_FORMATS)
    separate_arguments(row UNIX_COMMAND "${row}")
    list(POP_FRONT row type ops acts features)
    if (NOT features)
        set(features "-")
    endif ()
    string(REPLACE "," ";" ops "${ops}")
    string(MAKE_C_IDENTIFIER "${type}" id)
    if ("codec" IN_LIST ops)
        _ggml_library(${id}_quantize codec ${features} type=${type} quantize=true)
        _ggml_library(${id}_dequantize codec ${features} type=${type} quantize=false)
        set(_ggml_variants checked)
        _ggml_library(
            ${id}_quantize_scaled codec ${features} type=${type} quantize=true scaled=true
        )
        _ggml_library(
            ${id}_dequantize_scaled codec ${features} type=${type} quantize=false scaled=true
        )
        unset(_ggml_variants)
        string(APPEND registry "GQ_CODEC(${id}, \"${type}\")\n")
    endif ()
    string(REPLACE "," ";" acts "${acts}")
    foreach (op IN ITEMS vec_dot mul_mat)
        if (NOT op IN_LIST ops)
            continue ()
        endif ()
        foreach (act IN LISTS acts)
            string(REPLACE ":" ";" at "${act}")
            list(GET at 0 storage)
            list(GET at -1 compute)
            string(REPLACE ":" "_to_" name "${id}_${act}_${op}")
            if (NOT features STREQUAL "-")
                string(REPLACE "," "_" suffix "${features}")
                string(APPEND name "_${suffix}")
            endif ()
            string(MAKE_C_IDENTIFIER "${name}" name)
            set(params "")
            foreach (sp IN LISTS GGML_SCHEDULE_PARAMS)
                separate_arguments(sp UNIX_COMMAND "${sp}")
                list(POP_FRONT sp sp_type sp_op sp_act)
                if (
                    sp_type STREQUAL type
                    AND sp_op STREQUAL op
                    AND (sp_act STREQUAL act
                        OR sp_act STREQUAL "*")
                )
                    list(APPEND params ${sp})
                endif ()
            endforeach ()
            unset(_ggml_env)
            if ("slp=false" IN_LIST params)
                list(REMOVE_ITEM params "slp=false")
                set(_ggml_env "HL_LLVM_ARGS=-vectorize-slp=false")
            endif ()
            _ggml_library(${name} matmul ${features} weight=${type} act=${act} op=${op} ${params})
            unset(_ggml_env)
            string(APPEND registry
                "GQ_HALIDE_${op}(${name}, \"${type}\", ${storage}, ${compute}, \"${features}\")\n"
            )
        endforeach ()
    endforeach ()
endforeach ()
file(CONFIGURE OUTPUT halide_kernels.inc CONTENT "${registry}")
