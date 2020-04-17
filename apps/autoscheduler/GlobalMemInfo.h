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
    double num_transactions() const {
        return total_num_transactions;
    }

    void add_access_info(double num_requests, double num_transactions_per_request, double num_bytes_used_per_request, int N, double amortization) {
        internal_assert(num_bytes_used_per_request > 0);

        num_requests /= amortization;

        double total_transactions = num_requests * num_transactions_per_request;
        double total_bytes = total_transactions * 32.0;
        double total_bytes_used = num_requests * num_bytes_used_per_request;

        internal_assert(total_bytes_used <= total_bytes);

        for (int i = 0; i < N; ++i) {
            add_access_info(num_requests, total_transactions, total_bytes_used, total_bytes);
        }
    }

    void add(const GlobalMemInfo& other) {
        total_num_requests += other.total_num_requests;
        total_num_transactions += other.total_num_transactions;
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
    void add_access_info(double num_requests, double num_transactions, double num_bytes_used, double num_bytes) {
        total_num_requests += num_requests;
        total_num_transactions += num_transactions;
        total_num_bytes_used += num_bytes_used;
        total_num_bytes += num_bytes;
    }

    double total_num_requests = 0;
    double total_num_transactions = 0;
    double total_num_bytes_used = 0;
    double total_num_bytes = 0;
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
