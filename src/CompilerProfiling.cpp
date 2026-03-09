#include "CompilerProfiling.h"

#ifdef WITH_COMPILER_PROFILING

#include "Util.h"

#if !defined(_WIN32)
#include <cxxabi.h>
#else
#include <windows.h>
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <fstream>
#include <iomanip>
#include <map>

namespace Halide {
namespace Internal {
namespace Profiling {

static std::string demangle(const char *name) {
#if !defined(_WIN32)
    int status = 0;
    char *p = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string ret(p);
    free(p);
    return ret;
#else
    char demangled_name[8192];
    if (UnDecorateSymbolName(name, demangled_name, sizeof(demangled_name),
                             UNDNAME_COMPLETE)) {
        std::string ret(demangled_name);
        return ret;
    } else {
        DWORD error = GetLastError();
        std::cout << "UnDecorateSymbolName error: " << error << std::endl;
        return name;
    }
#endif
}

static std::string cleanup_name(std::string name) {
    debug(2) << "Cleaned  " << name << "   =>   ";
    {
        std::string_view profiled = "Halide::Internal::Profiling::Profiled<";
        if (size_t idx = name.find(profiled); idx != std::string::npos) {
            size_t cl = idx + 1;
            int num_open = 1;
            while (num_open != 0 && cl < name.size()) {
                if (name[cl] == '<') {
                    num_open++;
                } else if (name[cl] == '>') {
                    num_open--;
                }
                cl++;
            }
            name = name.substr(idx + profiled.size(), cl - idx - profiled.size() - 1);
        }
    }
    name = replace_all(name, "Halide::Internal::", "");
    name = replace_all(name, "(anonymous namespace)::", "");
    debug(2) << name << "\n";
    return name;
}

Context::Context() {
    if (std::string file = get_env_variable("HL_COMPILER_TRACE_FILE"); !file.empty()) {
        if (std::string bits = get_env_variable("HL_COMPILER_TRACE_BITS"); !bits.empty()) {
            active_bits = BIT_GENERIC;
            std::vector<std::string> sp = split_string(bits, ",");
            for (const std::string &s : sp) {
                if (s == "stmt") {
                    active_bits |= BIT_STMT;
                } else if (s == "expr") {
                    active_bits |= BIT_EXPR;
                }
            }
        } else {
            active_bits = BIT_GENERIC | BIT_STMT;
        }
    }
}
Context::~Context() {
    if (std::string file = get_env_variable("HL_COMPILER_TRACE_FILE"); !file.empty()) {
        Profiling::write_halide_profiling_trace(file);
    }
}

void write_halide_profiling_trace(const std::string &file) {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(ctx.mutex);  // Ensure no threads are born while serializing

    debug(1) << "Emitting trace.json events: " << file << "\n";

    // Pass 1: Find the absolute global t=0 across all threads
    uint64_t global_start_cycles = static_cast<uint64_t>(-1);
    for (const auto &trace : ctx.traces) {
        if (trace.events.empty()) {
            continue;
        }

        uint64_t first_cycle = trace.start_cycles_64;
        uint32_t first_timer = trace.events[0].timer;
        uint32_t anchor_lower = static_cast<uint32_t>(first_cycle & 0xFFFFFFFF);

        // Did the 32-bit timer roll over between thread creation and the first event?
        if (first_timer < anchor_lower) {
            first_cycle += 0x100000000ULL;
        }
        first_cycle = (first_cycle & 0xFFFFFFFF00000000ULL) | first_timer;

        if (first_cycle < global_start_cycles) {
            global_start_cycles = first_cycle;
        }
    }

    std::map<const char *, std::string> demangled_names;

    std::ofstream out(file);
    out << "[\n";
    bool first = true;

    double counter_freq = performance_counter_frequency();

    for (const auto &trace : ctx.traces) {
        uint64_t current_cycles = trace.start_cycles_64;
        uint32_t last_timer = static_cast<uint32_t>(current_cycles & 0xFFFFFFFF);

        for (const auto &ev : trace.events) {
            // Reconstruct the 64-bit timeline for this specific thread
            if (ev.timer < last_timer) {
                current_cycles += 0x1'0000'0000ULL;
            }
            current_cycles = (current_cycles & 0xFFFFFFFF00000000ULL) | ev.timer;
            last_timer = ev.timer;

            double ts_us = static_cast<double>(current_cycles - global_start_cycles) / counter_freq;

            if (!first) {
                out << ",\n";
            }
            first = false;

            out << "  {";
            if (ev.event_type == 0) {
                const char *src_tag = ev.src_tag;
                if (
#if !defined(_WIN32)
                    std::strncmp(src_tag, "N6Halide", 8) == 0
#else
                    std::strncmp(src_tag, "class Halide", 12) == 0 ||
                    std::strncmp(src_tag, "struct Halide", 13) == 0
#endif
                ) {
                    if (auto it = demangled_names.find(src_tag); it == demangled_names.end()) {
                        std::string dn = demangle(src_tag);
                        dn = cleanup_name(dn);
                        auto result = demangled_names.emplace(src_tag, std::move(dn));
                        src_tag = result.first->second.c_str();
                    } else {
                        src_tag = it->second.c_str();
                    }
                }
                // Only add strings to the start event.
                if (ev.tag == Event::Visitor) {
                    const char *node_name = IRNodeType_string((IRNodeType)ev.data);
                    out << "\"name\": \"" << node_name << "\", ";
                    out << "\"cat\": \"" << src_tag << "\", ";
                } else if (ev.tag == Event::Generic) {
                    out << "\"name\": \"" << src_tag << "\", ";
                    out << "\"args\": {";
                    out << "\"data\": \"" << ev.data << "\"";
                    out << "}, ";
                }
            }
            out << "\"ph\": \"" << (ev.event_type == 0 ? "B" : "E") << "\", ";
            out << "\"pid\": 1, ";
            out << "\"tid\": " << trace.tid << ", ";  // Same TID forces perfectly nested flame-graph boxes
            out << "\"ts\": " << std::fixed << std::setprecision(3) << ts_us;

            out << "}\n";  // Let's put a newline to not kill bad parsers.
        }
    }

    out << "\n]\n";
}

}  // namespace Profiling
}  // namespace Internal
}  // namespace Halide

#endif
