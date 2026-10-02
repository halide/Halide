#ifndef HALIDE_APPS_HALLMARK_MEMORY_MAPPED_FILE_H_
#define HALIDE_APPS_HALLMARK_MEMORY_MAPPED_FILE_H_

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "absl/strings/string_view.h"

namespace hallmark {

class MemoryMappedFile final {
public:
    explicit MemoryMappedFile(absl::string_view path) {
        fd_ = open(path.data(), O_RDONLY);
        if (fd_ >= 0) {
            length_ = lseek(fd_, 0, SEEK_END);
            data_ = mmap(nullptr, length_, PROT_READ, MAP_SHARED, fd_, 0);
        } else {
            length_ = 0;
            data_ = nullptr;
        }
    }

    virtual ~MemoryMappedFile() {
        if (data_) {
            munmap(data_, length_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    uint64_t length() const {
        return length_;
    }
    void *data() const {
        return data_;
    }
    bool valid() const {
        return data_ != nullptr;
    }

private:
    int fd_;
    uint64_t length_;
    void *data_;
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_MEMORY_MAPPED_FILE_H_
