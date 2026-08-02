#pragma once

#include <iosfwd>
#include <string>
#include <vector>

struct BenchCandidate {
    std::string name;
    double ns = 0.0;
    double throughput = 0.0;
    double speedup = 0.0;    // reference.ns / ns
    bool identical = false;  // candidate fn pointer == reference fn pointer
    bool correct = true;     // output matched the reference within tolerance
};

struct BenchRow {
    std::string label;  // type name, or repack key label
    double ref_ns = 0.0;
    double ref_throughput = 0.0;
    std::string ref_name;
    std::vector<BenchCandidate> candidates;
};

struct BenchReport {
    std::string title;
    std::string throughput_unit;  // "GB/s" or "GFLOP/s"
    std::vector<BenchRow> rows;
};

// Incremental printing: call print_report_header() once, then print_row()
// as each row is computed (bench_*.cpp interleaves this with the actual
// benchmarking so results stream out immediately instead of only appearing
// after the whole category finishes). Both flush stdout so the stream is
// visible immediately even when redirected/piped, not just on a tty.
void print_report_header(const std::string &title, const std::string &throughput_unit);
void print_row(const BenchRow &row, const std::string &throughput_unit);

// Convenience wrapper for a fully-built report (used for the "nothing
// registered" case, and anywhere the whole report is already in hand).
void print_report(const BenchReport &report);

void write_report_csv(const BenchReport &report, std::ostream &out);
