#ifndef BENCHMARKING_UTILS_H_
#define BENCHMARKING_UTILS_H_

class CacheEvictor {
public:
    CacheEvictor()
        : buffer_(0, 32 * 1024 * 1024) {
    }

    void flush_caches() {
        volatile int *d = buffer_.data();
        for (int i = 0; i < buffer_.size(); ++i) {
            d[i] += 1;
        }
    }

private:
    std::vector<int> buffer_;
};

#endif
