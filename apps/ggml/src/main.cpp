#include <cstdio>
#include <fstream>
#include <string>

#include <ggml-cpu.h>

#include "benchmarks.h"
#include "ggml_provider.h"
#include "halide_provider.h"
#include "kernel_registry.h"

namespace {

// The repack buffer type logs a GGML_LOG_DEBUG line on every repack (see
// src/ggml-cpu/repack.cpp:4733) -- benign, but this benchmark triggers many
// of them (one per gemv/gemm sample weight built), so drop DEBUG/INFO noise
// and keep only warnings/errors.
void quiet_log_callback(ggml_log_level level, const char *text, void *) {
    if (level >= GGML_LOG_LEVEL_WARN) {
        std::fputs(text, stderr);
    }
}

void print_ggml_version() {
#ifdef KERNEL_BENCH_GGML_VERSION
    std::printf("GGML version: %s\n", KERNEL_BENCH_GGML_VERSION);
#else
    std::printf("GGML version: unknown (GGML_VERSION not set by ggml-config.cmake)\n");
#endif
}

void print_cpu_features() {
    std::printf("CPU features:");
#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
    if (ggml_cpu_has_avx()) std::printf(" avx");
    if (ggml_cpu_has_avx2()) std::printf(" avx2");
    if (ggml_cpu_has_avx512()) std::printf(" avx512");
    if (ggml_cpu_has_avx512_vnni()) std::printf(" avx512_vnni");
    if (ggml_cpu_has_fma()) std::printf(" fma");
    if (ggml_cpu_has_f16c()) std::printf(" f16c");
    if (ggml_cpu_has_amx_int8()) std::printf(" amx_int8");
#endif
#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM) || defined(_M_ARM64)
    if (ggml_cpu_has_neon()) std::printf(" neon");
    if (ggml_cpu_has_dotprod()) std::printf(" dotprod");
    if (ggml_cpu_has_matmul_int8()) std::printf(" matmul_int8");
    if (ggml_cpu_has_fp16_va()) std::printf(" fp16_va");
    if (ggml_cpu_has_sve()) std::printf(" sve(%d bytes)", ggml_cpu_get_sve_cnt());
    if (ggml_cpu_has_sme()) std::printf(" sme");  // codespell:ignore sme
#endif
    std::printf("\n");
}

void usage(const char *argv0) {
    std::printf("usage: %s [--quantize] [--dequantize] [--vecdot] [--repack] [--all] [--csv FILE]\n", argv0);
}

}  // namespace

int main(int argc, char **argv) {
    ggml_log_set(quiet_log_callback, nullptr);

    bool do_quantize = false, do_dequantize = false, do_vecdot = false, do_repack = false;
    std::string csv_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--quantize") do_quantize = true;
        else if (arg == "--dequantize")
            do_dequantize = true;
        else if (arg == "--vecdot")
            do_vecdot = true;
        else if (arg == "--repack")
            do_repack = true;
        else if (arg == "--all")
            do_quantize = do_dequantize = do_vecdot = do_repack = true;
        else if (arg == "--csv" && i + 1 < argc)
            csv_path = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            usage(argv[0]);
            return 1;
        }
    }
    if (!do_quantize && !do_dequantize && !do_vecdot && !do_repack) {
        do_quantize = do_dequantize = do_vecdot = do_repack = true;  // default: --all
    }

    print_ggml_version();
    print_cpu_features();

    KernelRegistries registries;
    register_ggml_provider(registries);
    register_halide_provider(registries);

    // Each run_*_benchmarks() call prints its own header and streams a row
    // to stdout as soon as that row is computed (see report.h/print_row) --
    // results become visible immediately rather than only after everything
    // finishes. The returned reports are only needed here for --csv.
    std::vector<BenchReport> reports;
    if (do_quantize) reports.push_back(run_quantize_benchmarks(registries));
    if (do_dequantize) reports.push_back(run_dequantize_benchmarks(registries));
    if (do_vecdot) reports.push_back(run_vecdot_benchmarks(registries));
    if (do_repack) {
        for (auto &r : run_repack_benchmarks(registries)) {
            reports.push_back(std::move(r));
        }
    }

    if (!csv_path.empty()) {
        std::ofstream out(csv_path);
        for (const auto &report : reports) {
            write_report_csv(report, out);
        }
        std::printf("\nwrote %s\n", csv_path.c_str());
    }

    return 0;
}
