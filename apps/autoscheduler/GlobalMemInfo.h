#ifndef GPU_MEM_INFO_H
#define GPU_MEM_INFO_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ASLog.h"

/** \file
 *
 * Data structure that helps track global memory access information. Useful when 
 * computing GPU features
 */

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct GlobalMemInfo {
    double num_transactions() const {
        return total_num_transactions;
    }

    double num_transactions_with_pure_licm() const {
        return total_num_transactions_with_pure_licm;
    }

    void add_access_info(double num_requests, double num_requests_with_pure_licm, double num_transactions_per_request, double num_bytes_used_per_request, int N, double amortization) {
        internal_assert(num_bytes_used_per_request > 0);

        num_requests /= amortization;
        num_requests_with_pure_licm /= amortization;

        double total_transactions = num_requests * num_transactions_per_request;
        double total_transactions_with_pure_licm = num_requests_with_pure_licm * num_transactions_per_request;
        double total_bytes = total_transactions * 32.0;
        double total_bytes_used = num_requests * num_bytes_used_per_request;

        internal_assert(total_bytes_used <= total_bytes);

        for (int i = 0; i < N; ++i) {
            add_access_info(total_transactions, total_transactions_with_pure_licm, total_bytes_used, total_bytes);
        }
    }

    void add(const GlobalMemInfo& other) {
        total_num_transactions += other.total_num_transactions;
        total_num_transactions_with_pure_licm += other.total_num_transactions_with_pure_licm;
        total_num_bytes_used += other.total_num_bytes_used;
        total_num_bytes += other.total_num_bytes;
    }

    double efficiency() const {
        if (total_num_bytes == 0) {
            return 1;
        }

        double result = total_num_bytes_used / total_num_bytes;
        internal_assert(result <= 1);
        return result;
    }

private:
    void add_access_info(double num_transactions, double num_transactions_with_pure_licm, double num_bytes_used, double num_bytes) {
        total_num_transactions += num_transactions;
        total_num_transactions_with_pure_licm += num_transactions_with_pure_licm;
        total_num_bytes_used += num_bytes_used;
        total_num_bytes += num_bytes;
    }

    double total_num_transactions = 0;
    double total_num_transactions_with_pure_licm = 0;
    double total_num_bytes_used = 0;
    double total_num_bytes = 0;
};

struct StorageStrides {
public:
    void add_valid(double stride) {
        add(stride, true);
    }

    void add_invalid() {
        add(0, false);
    }

    void multiply_by_scalar(double scalar) {
        for (double& s : values) {
            s *= scalar;
        }
    }

    bool valid(size_t i) const {
        return is_valid[i];
    }

    double operator[](size_t i) const {
        return values[i];
    }
private:
    void add(double stride, bool e) {
        values.push_back(stride);
        is_valid.push_back(e);
    }
    std::vector<double> values;
    std::vector<bool> is_valid;
};

struct GlobalAccessAccumulator {
    GlobalAccessAccumulator(int bytes_per_access, size_t dimensions, const StorageStrides& strides, bool verbose)
        : bytes_per_access{bytes_per_access}
        , dimensions{dimensions}
        , strides{strides}
        , verbose{verbose}
    {}

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        if (verbose) {
            aslog(0) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n"; 
        }

        int thread_ids[3] = {x, y, z};
        int64_t byte = 0;
        for (size_t i = 0; i < dimensions; ++i) {
            if (!strides.valid(i)) {
                ++unknown_sectors;
                return;
            }
            byte += bytes_per_access * (int)(thread_ids[i] * strides[i]);
        }

        if (verbose) {
            aslog(0) << "byte accessed: " << byte << "\n";
        }

        int64_t sector = byte / 32;
        for (int i = 0; i < bytes_per_access; ++i) {
            if (verbose) {
                aslog(0) << "sector accessed: " << sector << "\n";
            }
            sectors_accessed[sector].insert(byte + i);
        }
    }

    void add_access_info(int num_requests, int num_requests_with_pure_licm, double access_count, double amortization, GlobalMemInfo& global_mem_info) const {
        int num_transactions_per_request = sectors_accessed.size() + unknown_sectors;

        if (verbose) {
            aslog(0) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        int num_bytes_used_per_request = 0;
        for (const auto& sector : sectors_accessed) {
            num_bytes_used_per_request += sector.second.size();
        }

        num_bytes_used_per_request += unknown_sectors * bytes_per_access;

        if (verbose) {
            aslog(0) << "num_requests = " << num_requests << "\n";
        }

        global_mem_info.add_access_info(
            num_requests,
            num_requests_with_pure_licm,
            num_transactions_per_request,
            num_bytes_used_per_request,
            access_count,
            amortization
        );
    }

private:
    int bytes_per_access;
    size_t dimensions;
    StorageStrides strides;
    bool verbose;
    int unknown_sectors = 0;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> sectors_accessed;
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
