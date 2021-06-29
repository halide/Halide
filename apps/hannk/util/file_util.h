#ifndef HANNK_FILE_UTIL_H
#define HANNK_FILE_UTIL_H

#include <memory>
#include <vector>

#include "util/error_util.h"

namespace hannk {

// Slurp entire file into memory.
// Most code should probably use ReadOnlyFileView instead.
std::vector<char> read_entire_file(const std::string &filename);

// Abstraction for using mmap() for a view of an existing file.
// File is kept open for the lifetime of the object.
// Error conditions are currently handled by returning a nullptr from data().
// Most code should probably use ReadOnlyFileView instead.
// TODO: add Windows-specific support via MapViewOfFile().
class MemoryMappedFile {
public:
    MemoryMappedFile(const std::string &filename, bool read_only = true);
    ~MemoryMappedFile();

    void *data() {
        return data_;
    }
    const void *data() const {
        return data_;
    }

    size_t size() const {
        return size_;
    }

    // Movable but not copyable.
    MemoryMappedFile() = delete;
    MemoryMappedFile(const MemoryMappedFile &) = delete;
    MemoryMappedFile &operator=(const MemoryMappedFile &) = delete;
    MemoryMappedFile(MemoryMappedFile &&) = default;
    MemoryMappedFile &operator=(MemoryMappedFile &&) = default;

private:
    size_t size_ = 0;
    void *data_ = nullptr;
};

// Abstraction for opening a read-only file, either via slurping into memory or mmap().
class ReadOnlyFileView {
public:
    ReadOnlyFileView(const std::string &filename, bool use_mmap = true);
    ~ReadOnlyFileView();

    const void *data() const;
    size_t size() const;

    // Movable but not copyable.
    ReadOnlyFileView() = delete;
    ReadOnlyFileView(const ReadOnlyFileView &) = delete;
    ReadOnlyFileView &operator=(const ReadOnlyFileView &) = delete;
    ReadOnlyFileView(ReadOnlyFileView &&) = default;
    ReadOnlyFileView &operator=(ReadOnlyFileView &&) = default;

private:
    std::vector<char> buffer_;
    std::unique_ptr<MemoryMappedFile> mmap_;
};

}  // namespace hannk

#endif  // HANNK_FILE_UTIL_H
