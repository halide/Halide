#include "Util.h"
#include "Introspection.h"
#include "Debug.h"
#include "Error.h"
#include "Generator.h"
#include <sstream>
#include <map>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::ostringstream;
using std::map;

string unique_name(char prefix) {
    // arrays with static storage duration should be initialized to zero automatically
    static int instances[256];
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

/** Convert an integer to a string. */
string int_to_string(int x) {
    // Most calls to this function are during lowering, and correspond
    // to the dimensions of some buffer. So this gets called with 0,
    // 1, 2, and 3 a lot, and it's worth optimizing those cases.
    static const string small_ints[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
    if (x < 8) return small_ints[x];
    ostringstream ss;
    ss << x;
    return ss.str();
}

string unique_name(const string &name, bool user) {
    static map<string, int> known_names;

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

    int &count = known_names[name];
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

string base_name(const string &name, char delim) {
    size_t off = name.rfind(delim);
    if (off == string::npos) {
        return name;
    }
    return name.substr(off+1);
}

string make_entity_name(void *stack_ptr, const string &type, char prefix) {
    string name = get_variable_name(stack_ptr, type);
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

std::vector<std::string> split_string(const std::string& source, const std::string& delim) {
  std::vector<std::string> elements;
  size_t start = 0;
  size_t found = 0;
  while ((found = source.find(delim, start)) != string::npos) {
    elements.push_back(source.substr(start, found - start));
    start = found + delim.size();
  }

  // If start is exactly source.size(), the last thing in source is a
  // delimiter, in which case we want to add an empty string to elements.
  if (start <= source.size()) {
    elements.push_back(source.substr(start, string::npos));
  }
  return elements;
}

#if __cplusplus > 199711L
int generate_filter_main(int argc, char **argv, std::ostream& cerr) {
    const char kUsage[] = "gengen [-g GENERATOR_NAME] [-f FUNCTION_NAME] [-o OUTPUT_DIR]  "
        "target=target-string [generator_arg=value [...]]\n";

    std::map<std::string, std::string> flags_info = {{"-f", ""}, {"-g", ""}, {"-o", ""}};
    std::map<std::string, std::string> generator_args;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            std::vector<std::string> v = split_string(argv[i], "=");
            if (v.size() != 2 || v[0].empty() || v[1].empty()) {
              cerr << kUsage;
              return 1;
            }
            generator_args[v[0]] = v[1];
            continue;
        }
        auto it = flags_info.find(argv[i]);
        if (it != flags_info.end()) {
            if (i+1 >= argc) {
              cerr << kUsage;
              return 1;
            }
            it->second = argv[i+1];
            ++i;
            continue;
        }
        cerr << "Unknown flag: " << argv[i] << "\n";
        cerr << kUsage;
        return 1;
    }

    std::vector<std::string> generator_names = Internal::GeneratorRegistry::enumerate();

    std::string generator_name = flags_info["-g"];
    if (generator_name.empty()) {
        // If -g isn't specified, but there's only one generator registered, just use that one.
        if (generator_names.size() != 1) {
            cerr << "-g must be specified if multiple generators are registered\n";
            cerr << kUsage;
            return 1;
        }
        generator_name = generator_names[0];
    }
    std::string function_name = flags_info["-f"];
    if (function_name.empty()) {
        // If -f isn't specified, but there's only one generator registered,
        // just assume function name = generator name.
        if (generator_names.size() != 1) {
            cerr << "-f must be specified if multiple generators are registered\n";
            cerr << kUsage;
            return 1;
        }
        function_name = generator_names[0];
    }
    std::string output_dir = flags_info["-o"];
    if (output_dir.empty()) {
        cerr << "-o must always be specified.\n";
        cerr << kUsage;
        return 1;
    }
    if (generator_args.find("target") == generator_args.end()) {
        cerr << "Target missing\n";
        cerr << kUsage;
        return 1;
    }

    std::unique_ptr<GeneratorBase> gen = Internal::GeneratorRegistry::create(generator_name, generator_args);
    if (gen == nullptr) {
        cerr << "Unknown generator: " << generator_name << "\n";
        cerr << kUsage;
        return 1;
    }
    gen->emit_filter(output_dir, function_name);
    return 0;
}
#endif  // __cplusplus > 199711L

}
}
