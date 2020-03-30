#ifndef GPU_MEM_INFO_H
#define GPU_MEM_INFO_H

/** \file
 *
 * Data structure that helps track global memory access information. Useful when 
 * computing GPU features
 */

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct GlobalMemInfo {
    double required_accesses() const {
        return total_required_accesses;
    }

    double min_accesses() const {
        return total_min_accesses;
    }

    double access_efficiency() const {
        if (total_required_accesses > 0 && total_min_accesses > 0) {
            return total_min_accesses / total_required_accesses;
        }

        return 1;
    }

    double coalesce_efficiency() const {
        constexpr double max_coalesce_efficiency = 1.0;

        if (num_coalesce_entries == 0) {
            return max_coalesce_efficiency;
        }

        return total_coalesce_efficiency / num_coalesce_entries;
    }

    void add_access_info(double required_accesses, double min_accesses, double stride, int N) {
        for (int i = 0; i < N; ++i) {
            add_access_info(required_accesses, min_accesses, stride);
        }
    }

    void add(const GlobalMemInfo& other) {
        num_coalesce_entries += other.num_coalesce_entries;
        total_coalesce_efficiency += other.total_coalesce_efficiency;
        total_required_accesses += other.total_required_accesses;
        total_min_accesses += other.total_min_accesses;
    }

private:
    void add_access_info(double required_accesses, double min_accesses, double stride) {
        internal_assert(min_accesses <= required_accesses) << "Invalid access values";

        total_required_accesses += required_accesses;
        total_min_accesses += min_accesses;

        constexpr double max_coalesce_efficiency = 1.0;
        if (stride == 0) {
            total_coalesce_efficiency += max_coalesce_efficiency;
        } else {
            total_coalesce_efficiency += max_coalesce_efficiency / std::min(32.0, std::max(1.0, stride));
        }

        ++num_coalesce_entries;
    }

    int num_coalesce_entries = 0;
    double total_coalesce_efficiency = 0;
    double total_required_accesses = 0;
    double total_min_accesses = 0;
};

struct LocalMemInfo {
    void add_access(double num_accesses, double stride) {
        total_accesses += num_accesses;
        add_stride(stride);
    }

    double average_efficiency() const {
        if (total_stride == 0) {
            return 1.0;
        }
        return 1.0 / (total_stride / num_entries);
    }

    double total_accesses = 0;

private:
    void add_stride(double stride) {
        if (stride == 0) {
            return;
        }

        total_stride += std::min(32.0, std::max(1.0, stride));
        ++num_entries;
    }

    int num_entries = 0;
    double total_stride = 0;
};


}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // GPU_MEM_INFO_H
