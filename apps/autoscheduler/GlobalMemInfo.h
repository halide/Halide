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

    void add_access_info(double num_requests, double num_transactions_per_request, double num_words_used_per_request, int N, double amortization) {
        internal_assert(num_words_used_per_request > 0);

        num_requests /= amortization;

        double total_transactions = num_requests * num_transactions_per_request;
        double total_words = total_transactions * 8.0;
        double total_words_used = num_requests * num_words_used_per_request;

        internal_assert(total_words_used <= total_words);

        for (int i = 0; i < N; ++i) {
            add_access_info(num_requests, total_transactions, total_words_used, total_words);
        }
    }

    void add(const GlobalMemInfo& other) {
        total_num_requests += other.total_num_requests;
        total_num_transactions += other.total_num_transactions;
        total_num_words_used += other.total_num_words_used;
        total_num_words += other.total_num_words;
    }

    double efficiency() const {
        if (total_num_words == 0) {
            return 1;
        }

        double result = total_num_words_used / total_num_words;
        internal_assert(result <= 1);
        return result;
    }

private:
    void add_access_info(double num_requests, double num_transactions, double num_words_used, double num_words) {
        total_num_requests += num_requests;
        total_num_transactions += num_transactions;
        total_num_words_used += num_words_used;
        total_num_words += num_words;
    }

    double total_num_requests = 0;
    double total_num_transactions = 0;
    double total_num_words_used = 0;
    double total_num_words = 0;
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
