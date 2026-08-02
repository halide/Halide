#include "report.h"

#include <cstdio>
#include <ostream>

void print_report_header(const std::string &title, const std::string &throughput_unit) {
    std::printf("\n=== %s (%s) ===\n", title.c_str(), throughput_unit.c_str());
    std::fflush(stdout);
}

void print_row(const BenchRow &row, const std::string &throughput_unit) {
    std::printf("  %-20s  reference=%-10s %8.1f ns  %8.2f %s\n", row.label.c_str(), row.ref_name.c_str(), row.ref_ns,
                row.ref_throughput, throughput_unit.c_str());
    if (row.candidates.empty()) {
        std::printf("  %-20s    (no candidates registered yet)\n", "");
    }
    for (const auto &c : row.candidates) {
        if (c.identical) {
            std::printf("  %-20s    %-12s  identical to reference\n", "", c.name.c_str());
            continue;
        }
        std::printf("  %-20s    %-12s  %8.1f ns  %8.2f %s  %6.2fx%s\n", "", c.name.c_str(), c.ns, c.throughput,
                    throughput_unit.c_str(), c.speedup, c.correct ? "" : "  [MISMATCH vs reference]");
    }
    std::fflush(stdout);
}

void print_report(const BenchReport &report) {
    print_report_header(report.title, report.throughput_unit);
    if (report.rows.empty()) {
        std::printf("  (nothing registered)\n");
        std::fflush(stdout);
        return;
    }
    for (const auto &row : report.rows) {
        print_row(row, report.throughput_unit);
    }
}

void write_report_csv(const BenchReport &report, std::ostream &out) {
    out << "table,label,role,name,ns,throughput_" << report.throughput_unit << ",speedup,identical,correct\n";
    for (const auto &row : report.rows) {
        out << report.title << ',' << row.label << ",reference," << row.ref_name << ',' << row.ref_ns << ','
            << row.ref_throughput << ",1.0,0,1\n";
        for (const auto &c : row.candidates) {
            out << report.title << ',' << row.label << ",candidate," << c.name << ',' << c.ns << ',' << c.throughput
                << ',' << c.speedup << ',' << (c.identical ? 1 : 0) << ',' << (c.correct ? 1 : 0) << '\n';
        }
    }
}
