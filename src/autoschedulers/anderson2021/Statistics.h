#ifndef STATISTICS_H
#define STATISTICS_H

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "ASLog.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using Clock = std::chrono::high_resolution_clock;

template<typename T>
struct ScopedStatistic {
    const T &value;
    std::string msg;

    ScopedStatistic(const T &value, const std::string &msg)
        : value{value},
          msg{msg} {
    }

    ~ScopedStatistic() {
        aslog(1) << msg << " = " << value << "\n";
    }
};

struct ScopedTimer {
    std::chrono::time_point<Clock> start;
    std::string msg;

    explicit ScopedTimer(const std::string &msg)
        : start{Clock::now()},
          msg{msg} {
        aslog(1) << "Start: " << msg << "\n";
    }

    ~ScopedTimer() {
        auto duration = Clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        aslog(1) << "Duration (ms): " << msg << " = " << ms << "\n";
    }
};

struct Timer {
    std::chrono::time_point<Clock> start;

    Timer()
        : start{Clock::now()} {
    }

    void restart() {
        start = Clock::now();
    }

    std::chrono::duration<double> elapsed() const {
        return Clock::now() - start;
    }
};

struct Statistics {
    int num_featurizations{0};
    int num_states_added{0};
    int num_block_memoization_hits{0};
    int num_block_memoization_misses{0};
    int num_memoized_featurizations{0};
    int num_memoization_hits{0};
    int num_memoization_misses{0};
    int num_tilings_accepted{0};
    int num_tilings_generated{0};
    std::chrono::duration<double> generate_children_time{0};
    std::chrono::duration<double> calculate_cost_time{0};
    std::chrono::duration<double> enqueue_time{0};
    std::chrono::duration<double> compute_in_tiles_time{0};
    std::chrono::duration<double> filter_thread_tiles_time{0};
    std::chrono::duration<double> filter_parallel_tiles_time{0};
    std::chrono::duration<double> feature_write_time{0};
    std::chrono::duration<double> featurization_time{0};
    int num_schedules_enqueued{0};
    std::chrono::duration<double> cost_model_evaluation_time{0};

    double total_generate_children_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(generate_children_time).count();
    }

    double total_compute_in_tiles_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(compute_in_tiles_time).count();
    }

    double total_filter_thread_tiles_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(filter_thread_tiles_time).count();
    }

    double total_filter_parallel_tiles_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(filter_parallel_tiles_time).count();
    }

    double total_feature_write_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(feature_write_time).count();
    }

    double total_calculate_cost_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(calculate_cost_time).count();
    }

    double total_featurization_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(featurization_time).count();
    }

    double average_featurization_time() const {
        return total_featurization_time() / (double)num_featurizations;
    }

    double total_enqueue_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(enqueue_time).count();
    }

    double total_cost_model_evaluation_time() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(enqueue_time + cost_model_evaluation_time).count();
    }

    double average_cost_model_evaluation_time() const {
        return total_cost_model_evaluation_time() / (double)num_schedules_enqueued;
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // STATISTICS_H
