#include "util/file_util.h"

#include <fstream>
#include <memory>

#ifdef _WIN32
#error "TODO: support Windows here via MapViewOfFile()"
#ifdef _MSC_VER
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "util/error_util.h"

namespace hannk {

std::vector<char> read_entire_file(const std::string &filename) {
    std::ifstream f(filename, std::ios::in | std::ios::binary);
    HCHECK(f.is_open()) << "Unable to open file: " << filename;

    std::vector<char> result;

    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result.resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result.data(), result.size());
    HCHECK(f.good()) << "Unable to read file: " << filename;
    f.close();
    return result;
}

MemoryMappedFile::MemoryMappedFile(const std::string &filename, bool read_only) {
#ifdef _WIN32
    HLOG(FATAL) << "Unimplemented";
#else
    struct stat st;
    int result = stat(filename.c_str(), &st);
    if (result != 0) {
        HLOG(WARNING) << "Unable to stat file: " << filename << " error: " << strerror(errno);
        data_ = nullptr;
        size_ = 0;
        return;
    }
    size_ = st.st_size;

    int fd = open(filename.c_str(), read_only ? O_RDONLY : O_RDWR);
    if (fd == -1) {
        HLOG(WARNING) << "Unable to open file: " << filename << " error: " << strerror(errno);
        data_ = nullptr;
        size_ = 0;
        return;
    }
    data_ = mmap(nullptr, size_, read_only ? PROT_READ : (PROT_READ | PROT_WRITE), MAP_SHARED, fd, /*offset*/ 0);
    if (!data_) {
        HLOG(WARNING) << "Unable to open file: " << filename << " error: " << strerror(errno);
        size_ = 0;
        return;
    }
    result = close(fd);
    if (result != 0) {
        HLOG(WARNING) << "Unable to close file: " << filename << " error: " << strerror(errno);
        data_ = nullptr;
        size_ = 0;
        return;
    }
#endif
}

MemoryMappedFile::~MemoryMappedFile() {
#ifdef _WIN32
    HLOG(FATAL) << "Unimplemented";
#else
    if (data_) {
        int result = munmap(data_, size_);
        // TODO: should this be a failure, or just a warning?
        HCHECK(result == 0) << "munmap() failed: " << strerror(errno);
    }
#endif
}

ReadOnlyFileView::ReadOnlyFileView(const std::string &filename, bool use_mmap) {
    if (use_mmap) {
        mmap_ = std::make_unique<MemoryMappedFile>(filename, true);
    } else {
        buffer_ = read_entire_file(filename);
    }
}

ReadOnlyFileView::~ReadOnlyFileView() {
    // Nothing
}

const void *ReadOnlyFileView::data() const {
    return mmap_ ? mmap_->data() : buffer_.data();
}

size_t ReadOnlyFileView::size() const {
    return mmap_ ? mmap_->size() : buffer_.size();
}

}  // namespace hannk
