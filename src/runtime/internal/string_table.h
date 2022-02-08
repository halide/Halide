#ifndef HALIDE_RUNTIME_STRING_TABLE_H
#define HALIDE_RUNTIME_STRING_TABLE_H

#include "block_storage.h"
#include "linked_list.h"
#include "string_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Storage class for an array of strings (based on block storage)
// -- Intended for building and maintaining tables of strings
class StringTable {
public:
    // Disable copy constructors
    StringTable(const StringTable &) = delete;
    StringTable &operator=(const StringTable &) = delete;

    StringTable(const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    StringTable(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    StringTable(void *user_context, const char **array, size_t count, const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    ~StringTable();

    void reserve(void *user_context, size_t capacity);
    void clear(void *user_context);

    // fills the contents of the table (copies strings from given array)
    void fill(void *user_context, const char **array, size_t coun);

    // assign the entry at given index the given string
    void assign(void *user_context, size_t index, const char *str, size_t length = 0);  // if length is zero, strlen is used

    // parses the given c-string based on given delimiter, stores each substring in the resulting table
    size_t parse(void *user_context, const char *str, const char *delim);

    // index-based access operator
    const char *operator[](size_t index) const;

    // scans the table for existance of the given string within any entry (linear scan w/string compare!)
    bool contains(const char *str) const;

    // raw ptr access
    const char **data() const {
        return const_cast<const char **>(pointers.data());
    }

    size_t size() const {
        return pointers.size();
    }

private:
    LinkedList<StringStorage> contents;   //< owns string data
    BlockStorage<const char *> pointers;  //< points to contents
};

// --

StringTable::StringTable(const SystemMemoryAllocatorFns &sma)
    : contents(sma),
      pointers(sma) {
    // EMPTY!
}

StringTable::StringTable(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &sma)
    : contents(sma),
      pointers(sma) {

    reserve(user_context, capacity);
}

StringTable::StringTable(void *user_context, const char **array, size_t count, const SystemMemoryAllocatorFns &sma)
    : contents(sma),
      pointers(sma) {
    fill(user_context, array, count);
}

StringTable::~StringTable() {
    clear(nullptr);
}

void StringTable::reserve(void *user_context, size_t capacity) {

    for (size_t n = contents.size(); n < capacity; ++n) {
        LinkedList<StringStorage>::EntryType *entry = contents.append(user_context);
        entry->value.initialize(user_context, contents.current_allocator());
    }
    pointers.reserve(user_context, capacity);
}

void StringTable::clear(void *user_context) {
    for (size_t n = 0; n < contents.size(); ++n) {
        LinkedList<StringStorage>::EntryType *entry = contents.front();
        entry->value.clear(user_context);
        contents.pop_front(user_context);
    }
    contents.clear(user_context);
    pointers.clear(user_context);
}

const char *StringTable::operator[](size_t index) const {
    return pointers[index];
}

void StringTable::fill(void *user_context, const char **array, size_t count) {
    reserve(user_context, count);
    pointers.resize(user_context, count);
    LinkedList<StringStorage>::EntryType *entry = contents.front();
    for (size_t n = 0; n < count && n < contents.size() && entry != nullptr; ++n) {
        entry->value.assign(user_context, array[n]);
        pointers[n] = entry->value.data();
        entry = entry->next_ptr;
    }
}

void StringTable::assign(void *user_context, size_t index, const char *str, size_t length) {
    if (length == 0) { length = strlen(str); }
    LinkedList<StringStorage>::EntryType *entry = contents.front();
    for (size_t n = 0; n < contents.size() && entry != nullptr; ++n) {
        if (n == index) {
            entry->value.assign(user_context, str, length);
            pointers[n] = entry->value.data();
            break;
        }
        entry = entry->next_ptr;
    }
}

size_t StringTable::parse(void *user_context, const char *str, const char *delim) {
    if (StringUtils::is_empty(str)) { return 0; }

    size_t delim_length = strlen(delim);
    size_t total_length = strlen(str);
    size_t entry_count = StringUtils::count_tokens(str, delim);
    if (entry_count < 1) { return 0; }

    reserve(user_context, entry_count);
    pointers.resize(user_context, entry_count);

    // save each entry into the table
    size_t index = 0;
    const char *ptr = str;
    LinkedList<StringStorage>::EntryType *entry = contents.front();
    while (!StringUtils::is_empty(ptr) && (index < entry_count)) {
        size_t ptr_offset = ptr - str;
        const char *next_delim = strstr(ptr, delim);
        size_t token_length = (next_delim == nullptr) ? (total_length - ptr_offset) : (next_delim - ptr);
        if (token_length > 0 && entry != nullptr) {
            entry->value.assign(user_context, ptr, token_length);
            pointers[index] = entry->value.data();
            entry = entry->next_ptr;
            ++index;
        }
        ptr = (next_delim != nullptr) ? (next_delim + delim_length) : nullptr;
    }
    return entry_count;
}

bool StringTable::contains(const char *str) const {
    if (StringUtils::is_empty(str)) { return false; }

    const LinkedList<StringStorage>::EntryType *entry = contents.front();
    for (size_t n = 0; n < contents.size() && entry != nullptr; ++n) {
        if (entry->value.contains(str)) {
            return true;
        }
        entry = entry->next_ptr;
    }
    return false;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_STRING_STORAGE_H