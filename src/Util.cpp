#include "Util.h"
#include "Introspection.h"
#include "Debug.h"
#include "Error.h"
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::ostringstream;
using std::map;

std::string get_env_variable(char const *env_var_name, size_t &read) {
    if (!env_var_name) {
        return "";
    }
    read = 0;

    #ifdef _MSC_VER
    char lvl[32];
    getenv_s(&read, lvl, env_var_name);
    #else
    char *lvl = getenv(env_var_name);
    read = (lvl)?1:0;
    #endif

    if (read) {
        return std::string(lvl);
    }
    else {
        return "";
    }
}

string running_program_name() {
    // linux specific currently.
    #ifndef __linux__
    return "";
    #else
    string program_name;
    char path[PATH_MAX];
    ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path)-1);
    if (len != -1) {
        path[len] = '\0';
        string tmp = std::string(path);
        program_name = tmp.substr(tmp.find_last_of("/")+1);
    }
    else {
        return "";
    }
    return program_name;
    #endif
}

// TODO: Rationalize the two different versions of unique_name,
// possibly changing the name of one of them as they are used
// for different things, and in fact the two versions can end
// up returning the same name, thus they are not collectively
// unique.
string unique_name(char prefix) {
    // arrays with static storage duration should be initialized to zero automatically
    static std::atomic<int> instances[256];
    ostringstream str;
    str << prefix << instances[(unsigned char)prefix]++;
    return str.str();
}

bool starts_with(const string &str, const string &prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (str[i] != prefix[i]) return false;
    }
    return true;
}

bool ends_with(const string &str, const string &suffix) {
    if (str.size() < suffix.size()) return false;
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off+i] != suffix[i]) return false;
    }
    return true;
}

string replace_all(string &str, const string &find, const string &replace) {
    size_t pos = 0;
    while ((pos = str.find(find, pos)) != string::npos) {
        str.replace(pos, find.length(), replace);
        pos += replace.length();
    }
    return str;
}

string unique_name(const string &name, bool user) {
    static std::mutex known_names_lock;
    static map<string, int> *known_names = new map<string, int>();
    {
        std::lock_guard<std::mutex> lock(known_names_lock);

        // An empty string really does not make sense, but use 'z' as prefix.
        if (name.length() == 0) {
            return unique_name('z');
        }

        // Check the '$' character doesn't appear in the prefix. This lets
        // us separate the name from the number using '$' as a delimiter,
        // which guarantees uniqueness of the generated name, without
        // having to track all names generated so far.
        if (user) {
            for (size_t i = 0; i < name.length(); i++) {
                user_assert(name[i] != '$')
                    << "Name \"" << name << "\" is invalid. "
                    << "Halide names may not contain the character '$'\n";
            }
        }

        int &count = (*known_names)[name];
        count++;
        if (count == 1) {
            // The very first unique name is the original function name itself.
            return name;
        } else {
            // Use the programmer-specified name but append a number to make it unique.
            ostringstream oss;
            oss << name << '$' << count;
            return oss.str();
        }
    }
}

string base_name(const string &name, char delim) {
    size_t off = name.rfind(delim);
    if (off == string::npos) {
        return name;
    }
    return name.substr(off+1);
}

string make_entity_name(void *stack_ptr, const string &type, char prefix) {
    string name = Introspection::get_variable_name(stack_ptr, type);

    if (name.empty()) {
        return unique_name(prefix);
    } else {
        // Halide names may not contain '.'
        for (size_t i = 0; i < name.size(); i++) {
            if (name[i] == '.') {
                name[i] = ':';
            }
        }
        return unique_name(name);
    }
}

std::vector<std::string> split_string(const std::string &source, const std::string &delim) {
    std::vector<std::string> elements;
    size_t start = 0;
    size_t found = 0;
    while ((found = source.find(delim, start)) != std::string::npos) {
        elements.push_back(source.substr(start, found - start));
        start = found + delim.size();
    }

    // If start is exactly source.size(), the last thing in source is a
    // delimiter, in which case we want to add an empty string to elements.
    if (start <= source.size()) {
        elements.push_back(source.substr(start, std::string::npos));
    }
    return elements;
}

std::string extract_namespaces(const std::string &name, std::vector<std::string> &namespaces) {
    namespaces = split_string(name, "::");
    std::string result = namespaces.back();
    namespaces.pop_back();
    return result;
}

}
}
