#include "StaticLibrary.h"

#include <stdio.h>
#ifndef _MSC_VER
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "Error.h"

namespace Halide {
namespace Internal {

namespace {

class File {
public:
    File(const std::string &name, const char *mode) : name(name), f(fopen(name.c_str(), mode)) {
        internal_assert(f != nullptr) << "Could not open file " << f << " in mode " << mode << "\n";
    }
    ~File() {
        if (f != nullptr) {
            fclose(f);
        }
    }
    void write(const void *data, size_t size) {
        if (!fwrite(data, size, 1, f)) {
            user_error << "Could not write to " << name << "\n";
        }
    }

    void write(const std::string &str) {
        write(str.c_str(), str.size());
    }

    void write(const std::vector<uint8_t> &data) {
        write(&data[0], data.size());
    }

    std::vector<uint8_t> read(size_t size) {
        std::vector<uint8_t> result(size);
        if (!fread(&result[0], size, 1, f)) {
            user_error << "Could not read from " << name << "\n";
        }
        return result;
    }

    size_t tell() {
        long offset = ftell(f);
        if (offset < 0) {
            user_error << "Could not ftell: " << name << "\n";
        }
        return (size_t) offset;
    }

    static bool exists(const std::string &name) {
        #ifdef _MSC_VER
        return _access(name.c_str(), 0) == 0;
        #else
        return ::access(name.c_str(), F_OK) == 0;
        #endif
    }

    static void unlink(const std::string &name) {
        #ifdef _MSC_VER
        _unlink(name.c_str());
        #else
        ::unlink(name.c_str());
        #endif
    }

    struct Stat {
        off_t file_size;
        time_t mod_time;
        uid_t uid;
        gid_t gid;
        mode_t mode;
    };

    static Stat stat(const std::string &name) {
        #ifdef _MSC_VER
        struct _stat a;
        if (_stat(name.c_str(), &a) != 0) {
            user_error << "Could not stat " << name << "\n";
        }
        #else
        struct stat a;
        if (::stat(name.c_str(), &a) != 0) {
            user_error << "Could not stat " << name << "\n";
        }
        #endif
        return { a.st_size, a.st_mtime, a.st_uid, a.st_gid, a.st_mode };
    }

private:
    const std::string name;
    FILE *f;
};

std::string pad_right(std::string s, size_t max) {
    internal_assert(s.size() <= max) << s.size() << " "<<s;
    while (s.size() < max) {
        s += " ";
    }
    return s;
}

std::string decimal_string(int value, size_t pad) {
    return pad_right(std::to_string(value), pad);
}

std::string octal_string(int value, size_t pad) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%o", value);
    return pad_right(buf, pad);
}

}  // namespace

void create_ar_file(const std::vector<std::string> &src_files, const std::string &dst_file) {
    File ar(dst_file, "wb");
    ar.write("!<arch>\x0A", 8);
    for (const std::string &src_file : src_files) {
        File::Stat stat = File::stat(src_file);

        size_t filesize = stat.file_size;
        if (src_file.size() > 16) {
            ar.write("#1/" + decimal_string(src_file.size(), 13));
            filesize += src_file.size();
        } else {
            ar.write(pad_right(src_file, 16));
        }
        ar.write(decimal_string(stat.mod_time, 12));  // mod time
        ar.write(decimal_string(stat.uid, 6));  // user id
        ar.write(decimal_string(stat.gid, 6));  // group id
        ar.write(octal_string(stat.mode, 8));  // mode
        ar.write(decimal_string(filesize, 10));  // filesize
        ar.write("\x60\x0A");  // magic
        if (src_file.size() > 16) {
            ar.write(src_file);
        }
        {
            File src(src_file, "rb");
            // Just slurp everything into memory and write it back out.
            ar.write(src.read(stat.file_size));  // slurp it all over
        }
        if (ar.tell() & 1) {
            ar.write("\x0A");
        }
    }
}

void static_library_test() {
    {
        File a("a.tmp", "w");
        a.write("a123b");

        File b("b_long_name_is_long.tmp", "w");
        b.write("c456d");
    }

    create_ar_file({"a.tmp", "b_long_name_is_long.tmp"}, "arfile.a");

    File::Stat stat = File::stat("arfile.a");
    internal_assert(stat.file_size == 162) << "Filesize was " << stat.file_size;

    {
        File ar("arfile.a", "r");
        auto slurp = ar.read(162);
        // TODO(srj): verify contents are correct, but ignore mod/uid/gid/mode
    }
    File::unlink("a.tmp");
    File::unlink("b_long_name_is_long.tmp");
    File::unlink("arfile.a");

    debug(0) << "static_library_test passed\n";
}

}  // namespace Internal
}  // namespace Halide
