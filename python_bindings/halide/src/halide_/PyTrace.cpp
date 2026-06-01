#include "PyTrace.h"

#include "HalideRuntime.h"

#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Halide::PythonBindings {

// Statistics about a traced Func
struct FuncStats {
    std::string name;
    std::vector<int> min_coords;
    std::vector<int> max_coords;
    std::optional<double> min_value;
    std::optional<double> max_value;
};

// A single trace packet
struct TracePacket {
    int32_t id;
    int32_t event;
    int32_t parent_id;
    int32_t value_index;
    uint8_t type_code;
    uint8_t type_bits;
    uint16_t type_lanes;
    std::vector<int32_t> coordinates;
    std::vector<uint8_t> value;
    std::string func;
    std::string trace_tag;

    bool is_load() const {
        return event == halide_trace_load;
    }
    bool is_store() const {
        return event == halide_trace_store;
    }
    bool is_load_or_store() const {
        return is_load() || is_store();
    }

    py::object get_values() const {
        if (value.empty()) {
            return py::list();
        }

        py::list result;
        const size_t elem_size = (type_bits + 7) / 8;
        const size_t count = type_lanes;

        for (size_t i = 0; i < count && (i + 1) * elem_size <= value.size(); ++i) {
            const uint8_t *ptr = value.data() + i * elem_size;
            if (type_code == halide_type_float) {
                if (type_bits == 32) {
                    float v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                } else if (type_bits == 64) {
                    double v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                }
            } else if (type_code == halide_type_int) {
                if (type_bits == 8) {
                    result.append(static_cast<int8_t>(*ptr));
                } else if (type_bits == 16) {
                    int16_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                } else if (type_bits == 32) {
                    int32_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                } else if (type_bits == 64) {
                    int64_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                }
            } else if (type_code == halide_type_uint) {
                if (type_bits == 8) {
                    result.append(*ptr);
                } else if (type_bits == 16) {
                    uint16_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                } else if (type_bits == 32) {
                    uint32_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                } else if (type_bits == 64) {
                    uint64_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    result.append(v);
                }
            }
        }
        return result;
    }
};

// A complete Halide trace
class Trace {
public:
    static Trace load(const std::string &path,
                      std::optional<std::function<void(size_t, size_t)>> progress_callback = std::nullopt) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Failed to open trace file: " + path);
        }

        const size_t total_size = file.tellg();
        file.seekg(0);

        std::vector<uint8_t> data(total_size);
        file.read(reinterpret_cast<char *>(data.data()), total_size);

        return load_from_memory(data.data(), total_size, progress_callback);
    }

    static Trace load_from_memory(const uint8_t *data, size_t total_size,
                                  std::optional<std::function<void(size_t, size_t)>> progress_callback = std::nullopt) {
        Trace trace;

        // String interning table
        std::map<std::string, size_t> string_to_index;
        auto intern_string = [&](const std::string &s) -> size_t {
            auto it = string_to_index.find(s);
            if (it != string_to_index.end()) {
                return it->second;
            }
            size_t idx = trace.strings_.size();
            trace.strings_.push_back(s);
            string_to_index[s] = idx;
            return idx;
        };

        // Pipeline tracking for qualified names
        std::map<int32_t, std::string> parent_to_pipeline;
        // For DAG inference: packet_id -> (event, qualified_name, parent_id)
        std::map<int32_t, std::tuple<int32_t, std::string, int32_t>> id_to_info;
        // LOADs to process for DAG
        std::vector<std::pair<std::string, int32_t>> load_packets;

        size_t pos = 0;
        size_t last_progress = 0;
        const size_t progress_interval = std::max(size_t(1), total_size / 100);

        while (pos + sizeof(halide_trace_packet_t) <= total_size) {
            const auto *pkt_ptr = reinterpret_cast<const halide_trace_packet_t *>(data + pos);

            if (pkt_ptr->size < sizeof(halide_trace_packet_t) || pos + pkt_ptr->size > total_size) {
                break;
            }

            // Use the halide_trace_packet_t helper methods
            std::string func_name(pkt_ptr->func());
            std::string trace_tag(pkt_ptr->trace_tag());
            const auto ev = static_cast<halide_trace_event_code_t>(pkt_ptr->event);

            // Track pipeline hierarchy
            if (ev == halide_trace_begin_pipeline) {
                trace.pipelines_[pkt_ptr->id] = func_name;
                parent_to_pipeline[pkt_ptr->id] = func_name;
            } else if (ev == halide_trace_end_pipeline) {
                parent_to_pipeline.erase(pkt_ptr->parent_id);
            } else if (parent_to_pipeline.count(pkt_ptr->parent_id)) {
                parent_to_pipeline[pkt_ptr->id] = parent_to_pipeline[pkt_ptr->parent_id];
            }

            // Build qualified name
            std::string qualified;
            auto pipeline_it = parent_to_pipeline.find(pkt_ptr->parent_id);
            if (pipeline_it != parent_to_pipeline.end() && !pipeline_it->second.empty()) {
                qualified = pipeline_it->second + ":" + func_name;
            } else {
                qualified = func_name;
            }

            // Record for DAG inference
            id_to_info[pkt_ptr->id] = {pkt_ptr->event, qualified, pkt_ptr->parent_id};

            // Handle event types
            if (ev == halide_trace_tag && trace_tag.rfind("func_type_and_dim:", 0) == 0) {
                parse_func_type_and_dim(qualified, trace_tag, trace.funcs_);
            } else if (ev == halide_trace_begin_realization) {
                if (trace.funcs_.find(qualified) == trace.funcs_.end()) {
                    trace.funcs_[qualified] = FuncStats{qualified};
                }
                if (pipeline_it != parent_to_pipeline.end()) {
                    parent_to_pipeline[pkt_ptr->id] = pipeline_it->second;
                }
            } else if (ev == halide_trace_produce || ev == halide_trace_consume ||
                       ev == halide_trace_end_produce || ev == halide_trace_end_consume) {
                if (pipeline_it != parent_to_pipeline.end()) {
                    parent_to_pipeline[pkt_ptr->id] = pipeline_it->second;
                }
            } else if (ev == halide_trace_load) {
                load_packets.emplace_back(func_name, pkt_ptr->parent_id);
                if (trace.funcs_.find(qualified) == trace.funcs_.end()) {
                    trace.funcs_[qualified] = FuncStats{qualified};
                }
                update_stats_inline(pkt_ptr, trace.funcs_[qualified]);
            } else if (ev == halide_trace_store) {
                if (trace.funcs_.find(qualified) == trace.funcs_.end()) {
                    trace.funcs_[qualified] = FuncStats{qualified};
                }
                update_stats_inline(pkt_ptr, trace.funcs_[qualified]);
            }

            // Build packet
            TracePacket pkt;
            pkt.id = pkt_ptr->id;
            pkt.event = pkt_ptr->event;
            pkt.parent_id = pkt_ptr->parent_id;
            pkt.value_index = pkt_ptr->value_index;
            pkt.type_code = pkt_ptr->type.code;
            pkt.type_bits = pkt_ptr->type.bits;
            pkt.type_lanes = pkt_ptr->type.lanes;

            // Copy coordinates using the helper method
            if (pkt_ptr->dimensions > 0) {
                pkt.coordinates.resize(pkt_ptr->dimensions);
                std::memcpy(pkt.coordinates.data(), pkt_ptr->coordinates(),
                            pkt_ptr->dimensions * sizeof(int32_t));
            }

            // Copy value bytes using the helper method
            const size_t value_bytes = pkt_ptr->type.lanes * pkt_ptr->type.bytes();
            if (value_bytes > 0) {
                pkt.value.resize(value_bytes);
                std::memcpy(pkt.value.data(), pkt_ptr->value(), value_bytes);
            }

            // Intern strings
            pkt.func = trace.strings_[intern_string(func_name)];
            if (!trace_tag.empty()) {
                pkt.trace_tag = trace.strings_[intern_string(trace_tag)];
            }

            trace.packets_.push_back(std::move(pkt));

            pos += pkt_ptr->size;

            // Progress callback
            if (progress_callback && pos - last_progress >= progress_interval) {
                (*progress_callback)(pos, total_size);
                last_progress = pos;
            }
        }

        // DAG inference
        for (const auto &[func_name, load_parent_id] : load_packets) {
            auto pipeline_it = parent_to_pipeline.find(load_parent_id);
            std::string loaded_func;
            if (pipeline_it != parent_to_pipeline.end() && !pipeline_it->second.empty()) {
                loaded_func = pipeline_it->second + ":" + func_name;
            } else {
                loaded_func = func_name;
            }

            int32_t current_id = load_parent_id;
            while (id_to_info.count(current_id)) {
                const auto &[ev, producing_func, next_parent] = id_to_info[current_id];
                if (ev == halide_trace_produce) {
                    if (loaded_func != producing_func) {
                        trace.dag_edges_[loaded_func].insert(producing_func);
                    }
                    break;
                }
                current_id = next_parent;
            }
        }

        if (progress_callback) {
            (*progress_callback)(total_size, total_size);
        }

        return trace;
    }

    size_t size() const {
        return packets_.size();
    }

    const TracePacket &operator[](size_t i) const {
        if (i >= packets_.size()) {
            throw std::out_of_range("Packet index out of range");
        }
        return packets_[i];
    }

    const std::map<std::string, FuncStats> &funcs() const {
        return funcs_;
    }
    const std::map<int32_t, std::string> &pipelines() const {
        return pipelines_;
    }
    const std::map<std::string, std::set<std::string>> &dag_edges() const {
        return dag_edges_;
    }
    const std::vector<TracePacket> &packets() const {
        return packets_;
    }

    std::vector<TracePacket> filter_loads_stores() const {
        std::vector<TracePacket> result;
        for (const auto &p : packets_) {
            if (p.is_load_or_store()) {
                result.push_back(p);
            }
        }
        return result;
    }

    std::string dag_as_dot() const {
        std::ostringstream ss;
        ss << "digraph dag {\n";
        ss << "  rankdir=\"LR\";\n";
        ss << "  node [shape=box];\n";

        auto sanitize = [](const std::string &name) {
            std::string result = name;
            for (char &c : result) {
                if (c == ':') c = '_';
            }
            return result;
        };

        auto label = [](const std::string &name) {
            auto pos = name.rfind(':');
            return (pos != std::string::npos) ? name.substr(pos + 1) : name;
        };

        for (const auto &[func, _] : funcs_) {
            ss << "  " << sanitize(func) << " [label=\"" << label(func) << "\"];\n";
        }

        for (const auto &[src, dsts] : dag_edges_) {
            for (const auto &dst : dsts) {
                ss << "  " << sanitize(src) << " -> " << sanitize(dst) << ";\n";
            }
        }

        ss << "}\n";
        return ss.str();
    }

private:
    std::vector<TracePacket> packets_;
    std::map<std::string, FuncStats> funcs_;
    std::map<int32_t, std::string> pipelines_;
    std::map<std::string, std::set<std::string>> dag_edges_;
    std::vector<std::string> strings_;  // Interned strings

    static void parse_func_type_and_dim(const std::string &qualified,
                                        const std::string &trace_tag,
                                        std::map<std::string, FuncStats> &funcs) {
        std::istringstream iss(trace_tag);
        std::string prefix;
        iss >> prefix;  // "func_type_and_dim:"

        int num_types;
        if (!(iss >> num_types)) return;

        // Skip type info
        for (int i = 0; i < num_types * 3; ++i) {
            int dummy;
            if (!(iss >> dummy)) return;
        }

        int num_dims;
        if (!(iss >> num_dims)) return;

        std::vector<int> min_coords, max_coords;
        for (int i = 0; i < num_dims; ++i) {
            int min_val, extent;
            if (!(iss >> min_val >> extent)) break;
            min_coords.push_back(min_val);
            max_coords.push_back(min_val + extent);
        }

        if (!min_coords.empty()) {
            if (funcs.find(qualified) == funcs.end()) {
                funcs[qualified] = FuncStats{qualified};
            }
            funcs[qualified].min_coords = std::move(min_coords);
            funcs[qualified].max_coords = std::move(max_coords);
        }
    }

    static void update_stats_inline(const halide_trace_packet_t *pkt,
                                    FuncStats &stats) {
        // Update coordinate ranges using the helper method
        if (pkt->dimensions > 0) {
            const int *coords = pkt->coordinates();
            if (stats.min_coords.empty()) {
                stats.min_coords.resize(pkt->dimensions);
                stats.max_coords.resize(pkt->dimensions);
                for (int i = 0; i < pkt->dimensions; ++i) {
                    stats.min_coords[i] = coords[i];
                    stats.max_coords[i] = coords[i] + 1;
                }
            } else {
                for (int i = 0; i < pkt->dimensions && i < static_cast<int>(stats.min_coords.size()); ++i) {
                    stats.min_coords[i] = std::min(stats.min_coords[i], coords[i]);
                    stats.max_coords[i] = std::max(stats.max_coords[i], coords[i] + 1);
                }
            }
        }

        // Update value ranges using the helper method
        const uint8_t *val_ptr = static_cast<const uint8_t *>(pkt->value());
        const size_t elem_size = pkt->type.bytes();

        for (uint16_t i = 0; i < pkt->type.lanes; ++i) {
            double val = 0;
            const uint8_t *ptr = val_ptr + i * elem_size;

            if (pkt->type.code == halide_type_float) {
                if (pkt->type.bits == 32) {
                    float v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = v;
                } else if (pkt->type.bits == 64) {
                    std::memcpy(&val, ptr, sizeof(val));
                } else {
                    continue;
                }
            } else if (pkt->type.code == halide_type_int) {
                if (pkt->type.bits == 8) {
                    val = static_cast<int8_t>(*ptr);
                } else if (pkt->type.bits == 16) {
                    int16_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = v;
                } else if (pkt->type.bits == 32) {
                    int32_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = v;
                } else if (pkt->type.bits == 64) {
                    int64_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = static_cast<double>(v);
                } else {
                    continue;
                }
            } else if (pkt->type.code == halide_type_uint) {
                if (pkt->type.bits == 8) {
                    val = *ptr;
                } else if (pkt->type.bits == 16) {
                    uint16_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = v;
                } else if (pkt->type.bits == 32) {
                    uint32_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = v;
                } else if (pkt->type.bits == 64) {
                    uint64_t v;
                    std::memcpy(&v, ptr, sizeof(v));
                    val = static_cast<double>(v);
                } else {
                    continue;
                }
            } else {
                continue;
            }

            if (!stats.min_value.has_value()) {
                stats.min_value = val;
                stats.max_value = val;
            } else {
                stats.min_value = std::min(*stats.min_value, val);
                stats.max_value = std::max(*stats.max_value, val);
            }
        }
    }
};

void define_trace(py::module &m) {
    py::class_<FuncStats>(m, "FuncStats")
        .def_readonly("name", &FuncStats::name)
        .def_readonly("min_coords", &FuncStats::min_coords)
        .def_readonly("max_coords", &FuncStats::max_coords)
        .def_property_readonly("min_value", [](const FuncStats &s) -> py::object {
            return s.min_value.has_value() ? py::cast(*s.min_value) : py::none();
        })
        .def_property_readonly("max_value", [](const FuncStats &s) -> py::object {
            return s.max_value.has_value() ? py::cast(*s.max_value) : py::none();
        });

    py::class_<TracePacket>(m, "TracePacket")
        .def_readonly("id", &TracePacket::id)
        .def_readonly("event", &TracePacket::event)
        .def_readonly("parent_id", &TracePacket::parent_id)
        .def_readonly("value_index", &TracePacket::value_index)
        .def_readonly("type_code", &TracePacket::type_code)
        .def_readonly("type_bits", &TracePacket::type_bits)
        .def_readonly("type_lanes", &TracePacket::type_lanes)
        .def_readonly("coordinates", &TracePacket::coordinates)
        .def_readonly("func", &TracePacket::func)
        .def_readonly("trace_tag", &TracePacket::trace_tag)
        .def_property_readonly("is_load", &TracePacket::is_load)
        .def_property_readonly("is_store", &TracePacket::is_store)
        .def_property_readonly("is_load_or_store", &TracePacket::is_load_or_store)
        .def("get_values", &TracePacket::get_values);

    py::class_<Trace>(m, "Trace")
        .def_static("load", [](const std::string &path, py::object progress_callback) {
            if (progress_callback.is_none()) {
                return Trace::load(path);
            }
            return Trace::load(path, [&](size_t bytes_read, size_t total_bytes) {
                progress_callback(bytes_read, total_bytes);
            }); }, py::arg("path"), py::arg("progress_callback") = py::none())
        .def_static("load_bytes", [](py::bytes data) {
            std::string str = data;
            return Trace::load_from_memory(
                reinterpret_cast<const uint8_t *>(str.data()),
                str.size(),
                std::nullopt); }, py::arg("data"))
        .def("__len__", &Trace::size)
        .def("__getitem__", &Trace::operator[], py::arg("index"))
        .def_property_readonly("funcs", &Trace::funcs)
        .def_property_readonly("pipelines", &Trace::pipelines)
        .def_property_readonly("dag_edges", &Trace::dag_edges)
        .def_property_readonly("packets", &Trace::packets)
        .def("filter_loads_stores", &Trace::filter_loads_stores)
        .def("dag_as_dot", &Trace::dag_as_dot);
}

}  // namespace Halide::PythonBindings
