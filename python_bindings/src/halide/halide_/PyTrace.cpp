#include "PyTrace.h"

#include "HalideRuntime.h"

#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Halide::PythonBindings {

// Statistics about a traced Func
struct FuncStats {
    std::string name;
    std::vector<int> min_coords;
    std::vector<int> max_coords;
    std::optional<double> min_value;
    std::optional<double> max_value;
    // Authoritative declared per-dimension shape, captured from the
    // func_type_and_dim trace tag (or begin_realization as a fallback) and
    // never widened by load/store coordinates. Unlike min_coords/max_coords,
    // which aggregate raw coordinates and can be polluted on the channel axis
    // (e.g. input reports a dim-2 max of 192), these stay clean — so they are
    // the reliable source for "the last dimension has extent 3 → planar RGB".
    std::vector<int> realize_min;
    std::vector<int> realize_extent;
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
                // begin_realization coordinates are [min, extent] pairs per
                // dimension — the authoritative declared shape. Capture it once
                // (the tag is preferred when present; this is the fallback).
                auto &st = trace.funcs_[qualified];
                if (st.realize_extent.empty() && pkt_ptr->dimensions > 0) {
                    const int *co = pkt_ptr->coordinates();
                    const int ndims = pkt_ptr->dimensions / 2;
                    st.realize_min.resize(ndims);
                    st.realize_extent.resize(ndims);
                    for (int i = 0; i < ndims; ++i) {
                        st.realize_min[i] = co[2 * i];
                        st.realize_extent[i] = co[2 * i + 1];
                    }
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

            if (ev == halide_trace_store) {
                trace.store_indices_.push_back(trace.packets_.size());
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

    // Returns the indices of all store packets, in order.
    const std::vector<size_t> &store_indices() const {
        return store_indices_;
    }

    // Pick the first two dimension indices that are not the color dimension.
    // With color_dim == -1 this yields (0, 1) — the default x = dim0, y = dim1
    // layout. For planar RGB f(x, y, c) (color_dim == 2) it stays (0, 1); for
    // interleaved RGB f(c, x, y) (color_dim == 0) it becomes (1, 2).
    static void spatial_dims(int dims, int color_dim, int &xd, int &yd) {
        xd = -1;
        yd = -1;
        for (int d = 0; d < dims; ++d) {
            if (d == color_dim) continue;
            if (xd < 0) {
                xd = d;
            } else if (yd < 0) {
                yd = d;
                break;
            }
        }
    }

    // Returns {func_name: (xs, ys, values)} for all store packets with index
    // in [start, end).  xs and ys are int32 numpy arrays of absolute
    // coordinates; values is a float64 array of decoded scalar values.
    // Lane-vectorized stores are expanded: each lane becomes one row.
    //
    // When color_dim >= 0 the spatial axes are taken from the two dimensions
    // that are not color_dim, and a fourth int32 array `cs` (the value of the
    // color dimension per element) is appended to each tuple, so the caller can
    // route values into separate display channels. With color_dim == -1
    // (default) the behavior and 3-tuple return are unchanged.
    py::dict collect_pixels(size_t start, size_t end, int color_dim = -1) const {
        struct FuncData {
            std::vector<int32_t> xs;
            std::vector<int32_t> ys;
            std::vector<int32_t> cs;
            std::vector<double> values;
        };
        std::map<std::string, FuncData> per_func;

        if (start >= end) {
            return py::dict();
        }

        auto it_lo = std::lower_bound(store_indices_.begin(), store_indices_.end(), start);
        auto it_hi = std::upper_bound(store_indices_.begin(), store_indices_.end(), end - 1);

        for (auto it = it_lo; it != it_hi; ++it) {
            const auto &pkt = packets_[*it];
            auto &fd = per_func[pkt.func];

            const int n_lanes = pkt.type_lanes;
            const int n_coords = static_cast<int>(pkt.coordinates.size());
            const int dims = n_lanes > 0 ? n_coords / n_lanes : n_coords;
            const size_t elem_size = (pkt.type_bits + 7) / 8;

            int xd, yd;
            spatial_dims(dims, color_dim, xd, yd);
            const bool has_color = color_dim >= 0 && color_dim < dims;

            for (int lane = 0; lane < n_lanes; ++lane) {
                // Decode coordinates (same layout as HalideTraceViz / viewer.py)
                int32_t x = xd >= 0 ? pkt.coordinates[xd * n_lanes + lane] : 0;
                int32_t y = yd >= 0 ? pkt.coordinates[yd * n_lanes + lane] : 0;
                int32_t c = has_color ? pkt.coordinates[color_dim * n_lanes + lane] : 0;

                // Decode value to float64
                double val = 0.0;
                if (!pkt.value.empty() && (lane + 1) * elem_size <= pkt.value.size()) {
                    const uint8_t *ptr = pkt.value.data() + lane * elem_size;
                    if (pkt.type_code == halide_type_float) {
                        if (pkt.type_bits == 32) {
                            float v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            std::memcpy(&val, ptr, 8);
                        }
                    } else if (pkt.type_code == halide_type_int) {
                        if (pkt.type_bits == 8) {
                            val = static_cast<int8_t>(*ptr);
                        } else if (pkt.type_bits == 16) {
                            int16_t v;
                            std::memcpy(&v, ptr, 2);
                            val = v;
                        } else if (pkt.type_bits == 32) {
                            int32_t v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            int64_t v;
                            std::memcpy(&v, ptr, 8);
                            val = static_cast<double>(v);
                        }
                    } else if (pkt.type_code == halide_type_uint) {
                        if (pkt.type_bits == 8) {
                            val = *ptr;
                        } else if (pkt.type_bits == 16) {
                            uint16_t v;
                            std::memcpy(&v, ptr, 2);
                            val = v;
                        } else if (pkt.type_bits == 32) {
                            uint32_t v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            uint64_t v;
                            std::memcpy(&v, ptr, 8);
                            val = static_cast<double>(v);
                        }
                    }
                }

                fd.xs.push_back(x);
                fd.ys.push_back(y);
                fd.values.push_back(val);
                if (color_dim >= 0) {
                    fd.cs.push_back(c);
                }
            }
        }

        py::dict result;
        for (auto &[name, fd] : per_func) {
            auto n = static_cast<py::ssize_t>(fd.xs.size());
            py::array_t<int32_t> xs_arr(n), ys_arr(n);
            py::array_t<double> vals_arr(n);
            if (n > 0) {
                std::memcpy(xs_arr.mutable_data(), fd.xs.data(), n * sizeof(int32_t));
                std::memcpy(ys_arr.mutable_data(), fd.ys.data(), n * sizeof(int32_t));
                std::memcpy(vals_arr.mutable_data(), fd.values.data(), n * sizeof(double));
            }
            if (color_dim >= 0) {
                py::array_t<int32_t> cs_arr(n);
                if (n > 0) {
                    std::memcpy(cs_arr.mutable_data(), fd.cs.data(), n * sizeof(int32_t));
                }
                result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, vals_arr, cs_arr);
            } else {
                result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, vals_arr);
            }
        }
        return result;
    }

    // Return {func_name: (xs, ys, values)} for load events in [start, end).
    // Each (x, y) coordinate is included at most once per func (first load wins),
    // so the result is suitable for inferring input buffer contents.
    //
    // When color_dim >= 0 the spatial axes come from the two non-color
    // dimensions, deduplication keys on (x, y, channel) so each channel is kept,
    // and a fourth int32 array `cs` (the color-dim coordinate) is appended to
    // each tuple. With color_dim == -1 (default) the behavior and 3-tuple
    // return are unchanged.
    py::dict collect_load_pixels(size_t start, size_t end, int color_dim = -1) const {
        struct FuncData {
            std::vector<int32_t> xs;
            std::vector<int32_t> ys;
            std::vector<int32_t> cs;
            std::vector<double> values;
            std::unordered_set<int64_t> seen;  // packed (x,y), color_dim < 0
            // Per-channel packed (x,y) sets, so distinct channels at the same
            // (x, y) are each retained when color_dim >= 0.
            std::unordered_map<int32_t, std::unordered_set<int64_t>> seen_ch;
        };
        std::map<std::string, FuncData> per_func;

        if (start >= end) {
            return py::dict();
        }

        end = std::min(end, packets_.size());
        for (size_t idx = start; idx < end; ++idx) {
            const auto &pkt = packets_[idx];
            if (pkt.event != halide_trace_load) continue;

            auto &fd = per_func[pkt.func];

            const int n_lanes = pkt.type_lanes;
            const int n_coords = static_cast<int>(pkt.coordinates.size());
            const int dims = n_lanes > 0 ? n_coords / n_lanes : n_coords;
            const size_t elem_size = (pkt.type_bits + 7) / 8;

            int xd, yd;
            spatial_dims(dims, color_dim, xd, yd);
            const bool has_color = color_dim >= 0 && color_dim < dims;

            for (int lane = 0; lane < n_lanes; ++lane) {
                int32_t x = xd >= 0 ? pkt.coordinates[xd * n_lanes + lane] : 0;
                int32_t y = yd >= 0 ? pkt.coordinates[yd * n_lanes + lane] : 0;
                int32_t c = has_color ? pkt.coordinates[color_dim * n_lanes + lane] : 0;

                // Deduplicate: skip if we've already recorded this (x, y) — or
                // (x, y) for this channel when a color dim is active.
                const int64_t key = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
                if (color_dim >= 0) {
                    if (!fd.seen_ch[c].insert(key).second) continue;
                } else {
                    if (!fd.seen.insert(key).second) continue;
                }

                double val = 0.0;
                if (!pkt.value.empty() && (lane + 1) * elem_size <= pkt.value.size()) {
                    const uint8_t *ptr = pkt.value.data() + lane * elem_size;
                    if (pkt.type_code == halide_type_float) {
                        if (pkt.type_bits == 32) {
                            float v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            std::memcpy(&val, ptr, 8);
                        }
                    } else if (pkt.type_code == halide_type_int) {
                        if (pkt.type_bits == 8) {
                            val = static_cast<int8_t>(*ptr);
                        } else if (pkt.type_bits == 16) {
                            int16_t v;
                            std::memcpy(&v, ptr, 2);
                            val = v;
                        } else if (pkt.type_bits == 32) {
                            int32_t v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            int64_t v;
                            std::memcpy(&v, ptr, 8);
                            val = static_cast<double>(v);
                        }
                    } else if (pkt.type_code == halide_type_uint) {
                        if (pkt.type_bits == 8) {
                            val = *ptr;
                        } else if (pkt.type_bits == 16) {
                            uint16_t v;
                            std::memcpy(&v, ptr, 2);
                            val = v;
                        } else if (pkt.type_bits == 32) {
                            uint32_t v;
                            std::memcpy(&v, ptr, 4);
                            val = v;
                        } else if (pkt.type_bits == 64) {
                            uint64_t v;
                            std::memcpy(&v, ptr, 8);
                            val = static_cast<double>(v);
                        }
                    }
                }

                fd.xs.push_back(x);
                fd.ys.push_back(y);
                fd.values.push_back(val);
                if (color_dim >= 0) {
                    fd.cs.push_back(c);
                }
            }
        }

        py::dict result;
        for (auto &[name, fd] : per_func) {
            auto n = static_cast<py::ssize_t>(fd.xs.size());
            py::array_t<int32_t> xs_arr(n), ys_arr(n);
            py::array_t<double> vals_arr(n);
            if (n > 0) {
                std::memcpy(xs_arr.mutable_data(), fd.xs.data(), n * sizeof(int32_t));
                std::memcpy(ys_arr.mutable_data(), fd.ys.data(), n * sizeof(int32_t));
                std::memcpy(vals_arr.mutable_data(), fd.values.data(), n * sizeof(double));
            }
            if (color_dim >= 0) {
                py::array_t<int32_t> cs_arr(n);
                if (n > 0) {
                    std::memcpy(cs_arr.mutable_data(), fd.cs.data(), n * sizeof(int32_t));
                }
                result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, vals_arr, cs_arr);
            } else {
                result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, vals_arr);
            }
        }
        return result;
    }

    // Return {func_name: (xs, ys, counts)} where each entry is one unique (x, y)
    // touched by a load event in [start, end) and `counts` is how many times that
    // pixel was loaded. Unlike collect_load_pixels (which deduplicates to one entry
    // per pixel), this accumulates a count. Used by the load-count heatmap, which
    // accumulates these per-range counts as the user scrubs forward.
    py::dict collect_load_counts(size_t start, size_t end) const {
        struct FuncData {
            std::unordered_map<int64_t, int32_t> counts;  // packed_xy -> load count
        };
        std::map<std::string, FuncData> per_func;

        if (start >= end) {
            return py::dict();
        }

        end = std::min(end, packets_.size());
        for (size_t idx = start; idx < end; ++idx) {
            const auto &pkt = packets_[idx];
            if (pkt.event != halide_trace_load) continue;

            auto &fd = per_func[pkt.func];

            const int n_lanes = pkt.type_lanes;
            const int n_coords = static_cast<int>(pkt.coordinates.size());
            const int dims = n_lanes > 0 ? n_coords / n_lanes : n_coords;

            for (int lane = 0; lane < n_lanes; ++lane) {
                int32_t x = 0, y = 0;
                if (dims >= 2) {
                    x = pkt.coordinates[lane];
                    y = pkt.coordinates[n_lanes + lane];
                } else if (dims == 1) {
                    x = pkt.coordinates[lane];
                }
                const int64_t key = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);
                fd.counts[key] += 1;
            }
        }

        py::dict result;
        for (auto &[name, fd] : per_func) {
            auto n = static_cast<py::ssize_t>(fd.counts.size());
            py::array_t<int32_t> xs_arr(n), ys_arr(n), counts_arr(n);
            if (n > 0) {
                int32_t *xp = xs_arr.mutable_data();
                int32_t *yp = ys_arr.mutable_data();
                int32_t *cp = counts_arr.mutable_data();
                py::ssize_t i = 0;
                for (auto &[key, c] : fd.counts) {
                    xp[i] = static_cast<int32_t>(key >> 32);
                    yp[i] = static_cast<int32_t>(static_cast<uint32_t>(key));
                    cp[i] = c;
                    ++i;
                }
            }
            result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, counts_arr);
        }
        return result;
    }

    // Return {func_name: (xs, ys, mean_distances)} for the reuse distance of each
    // stored pixel: the number of packets between a store and the first subsequent
    // load of the same (func, x, y). Pixels written multiple times accumulate one
    // distance per (store, first-following-load) pair and report the mean.
    //
    // Pixels that were stored but never read are absent from the result (the caller
    // paints them gray); pixels that were never stored never appear at all.
    py::dict collect_reuse_distances(size_t start, size_t end) const {
        struct Accum {
            double sum_dist = 0.0;
            int64_t count = 0;
        };
        struct FuncData {
            std::unordered_map<int64_t, size_t> last_store;  // packed_xy -> store idx
            std::unordered_map<int64_t, Accum> accum;        // packed_xy -> distances
        };
        std::map<std::string, FuncData> per_func;

        if (start >= end) {
            return py::dict();
        }

        end = std::min(end, packets_.size());
        for (size_t idx = start; idx < end; ++idx) {
            const auto &pkt = packets_[idx];
            const bool is_store = pkt.event == halide_trace_store;
            const bool is_load = pkt.event == halide_trace_load;
            if (!is_store && !is_load) continue;

            auto &fd = per_func[pkt.func];

            const int n_lanes = pkt.type_lanes;
            const int n_coords = static_cast<int>(pkt.coordinates.size());
            const int dims = n_lanes > 0 ? n_coords / n_lanes : n_coords;

            for (int lane = 0; lane < n_lanes; ++lane) {
                int32_t x = 0, y = 0;
                if (dims >= 2) {
                    x = pkt.coordinates[lane];
                    y = pkt.coordinates[n_lanes + lane];
                } else if (dims == 1) {
                    x = pkt.coordinates[lane];
                }
                const int64_t key = (static_cast<int64_t>(x) << 32) | static_cast<uint32_t>(y);

                if (is_store) {
                    // Overwrite: distance is measured from the most recent store,
                    // which is what physically occupies the pixel at load time.
                    fd.last_store[key] = idx;
                } else {
                    auto sit = fd.last_store.find(key);
                    if (sit != fd.last_store.end()) {
                        auto &a = fd.accum[key];
                        a.sum_dist += static_cast<double>(idx - sit->second);
                        a.count += 1;
                        // Erase so we only count store -> first read, not every read.
                        fd.last_store.erase(sit);
                    }
                }
            }
        }

        py::dict result;
        for (auto &[name, fd] : per_func) {
            auto n = static_cast<py::ssize_t>(fd.accum.size());
            py::array_t<int32_t> xs_arr(n), ys_arr(n);
            py::array_t<double> mean_arr(n);
            if (n > 0) {
                int32_t *xp = xs_arr.mutable_data();
                int32_t *yp = ys_arr.mutable_data();
                double *mp = mean_arr.mutable_data();
                py::ssize_t i = 0;
                for (auto &[key, a] : fd.accum) {
                    xp[i] = static_cast<int32_t>(key >> 32);
                    yp[i] = static_cast<int32_t>(static_cast<uint32_t>(key));
                    mp[i] = a.count > 0 ? a.sum_dist / static_cast<double>(a.count) : 0.0;
                    ++i;
                }
            }
            result[py::str(name)] = py::make_tuple(xs_arr, ys_arr, mean_arr);
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
    std::vector<size_t> store_indices_;  // Pre-computed at load time for O(log n) range queries
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
            // Preserve the clean declared shape before min/max_coords get
            // widened by raw load/store coordinates. This is the authoritative
            // source for channel detection (see FuncStats::realize_extent).
            auto &st = funcs[qualified];
            st.realize_min.assign(min_coords.size(), 0);
            st.realize_extent.assign(min_coords.size(), 0);
            for (size_t i = 0; i < min_coords.size(); ++i) {
                st.realize_min[i] = min_coords[i];
                st.realize_extent[i] = max_coords[i] - min_coords[i];
            }
            st.min_coords = std::move(min_coords);
            st.max_coords = std::move(max_coords);
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
        .def_readonly("realize_min", &FuncStats::realize_min)
        .def_readonly("realize_extent", &FuncStats::realize_extent)
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
        .def("store_indices", &Trace::store_indices)
        .def("collect_pixels", &Trace::collect_pixels, py::arg("start"), py::arg("end"), py::arg("color_dim") = -1, "Return {func_name: (xs, ys, values)} numpy arrays for store packets "
                                                                                                                    "with index in [start, end).  Values are float64; coordinates are int32. "
                                                                                                                    "When color_dim >= 0, spatial axes come from the non-color dims and a "
                                                                                                                    "fourth int32 array `cs` (the color-dim coordinate) is appended per func.")
        .def("collect_load_pixels", &Trace::collect_load_pixels, py::arg("start"), py::arg("end"), py::arg("color_dim") = -1, "Return {func_name: (xs, ys, values)} for load events in [start, end), "
                                                                                                                              "deduplicated to one entry per unique (x, y) per func (first load wins). "
                                                                                                                              "Use this to infer the contents of input buffers that have no store events. "
                                                                                                                              "When color_dim >= 0, dedup keys on (x, y, channel) and a fourth int32 "
                                                                                                                              "array `cs` (the color-dim coordinate) is appended per func.")
        .def("collect_load_counts", &Trace::collect_load_counts, py::arg("start"), py::arg("end"), "Return {func_name: (xs, ys, counts)} for load events in [start, end), "
                                                                                                   "one entry per unique (x, y) per func with the number of times it was "
                                                                                                   "loaded (int32). Accumulate across ranges for the load-count heatmap.")
        .def("collect_reuse_distances", &Trace::collect_reuse_distances, py::arg("start"), py::arg("end"), "Return {func_name: (xs, ys, mean_distances)} for the reuse distance of "
                                                                                                           "each stored pixel: packets elapsed between a store and the first "
                                                                                                           "subsequent load of the same (func, x, y), averaged over all such pairs. "
                                                                                                           "Stored-but-never-read pixels are absent.")
        .def("dag_as_dot", &Trace::dag_as_dot);
}

}  // namespace Halide::PythonBindings
