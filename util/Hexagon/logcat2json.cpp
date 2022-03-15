#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <cctype>
#include <ctime>
#include <cxxopts.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <variant>

auto strip_inplace(std::string &s) -> std::string & {
  auto isgraph = [](unsigned char c) { return std::isgraph(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), isgraph));
  s.resize(
      s.size() -
      std::distance(s.rbegin(), std::find_if(s.rbegin(), s.rend(), isgraph)));
  return s;
}

template <class... Fs> struct match : Fs... { using Fs::operator()...; };
template <class... Fs> match(Fs...) -> match<Fs...>;

#define NOIMPL                                                                 \
  { throw std::runtime_error(std::string(__FUNCTION__) + " unimplemented"); }

struct Thread {
  using Id = uint32_t;
  Id id;
  std::string name;
};
struct Node {
  using Backlink = std::optional<std::reference_wrapper<Node>>;
  using Links =
      std::map<std::pair<std::string, Thread::Id>, std::unique_ptr<Node>>;

  Node(const std::string &name, Thread::Id thread_id, uint64_t total_time_ns,
       uint64_t overhead_time_ns, uint64_t times_called)
      : _name(name), _thread_id(thread_id), _total_time_ns(total_time_ns),
        _overhead_time_ns(overhead_time_ns), _times_called(times_called) {}
  Node(const Node &) = delete;
  Node(Node &&) = delete;

  auto name() const -> std::string { return _name; }
  auto thread_id() const -> Thread::Id { return _thread_id; }

  auto total_time_ns() const -> uint64_t { return _total_time_ns; }
  auto overhead_time_ns() const -> uint64_t { return _overhead_time_ns; }
  auto times_called() const -> uint64_t { return _times_called; }
  auto self_time_ns() const -> uint64_t {
    auto child_time_ns = uint64_t{0};
    for (const auto &[_, child] : children()) {
      if (this->is_parallel()) {
        child_time_ns = std::max(child_time_ns, child->total_time_ns());
      } else {
        child_time_ns += child->total_time_ns();
      }
    }
    return total_time_ns() - child_time_ns;
  }
  auto mean_total_time_ns() const -> uint64_t {
    return times_called() == 0 ? 0 : total_time_ns() / times_called();
  }
  auto mean_self_time_ns() const -> uint64_t {
    return times_called() == 0 ? 0 : self_time_ns() / times_called();
  }

  auto root_relatime() const -> double {
    return double(total_time_ns()) / root().total_time_ns();
  }
  auto parent_relatime() const -> std::optional<double> {
    if (parent()) {
      return double(total_time_ns()) / parent().value().get().total_time_ns();
    } else {
      return {};
    }
  }
  auto self_relatime() const -> double {
    return double(self_time_ns()) / total_time_ns();
  }
  auto self_root_relatime() const -> double {
    return double(self_time_ns()) / root().total_time_ns();
  }

  auto root() -> Node & {
    auto root = this;
    for (auto parent = this->parent(); parent;
         parent = parent.value().get().parent()) {
      root = std::addressof(parent.value().get());
    }
    return *root;
  }
  auto root() const -> const Node & { return const_cast<Node &>(*this).root(); }
  auto parent() -> Backlink & { return _parent; }
  auto parent() const -> const Backlink & {
    return const_cast<Node &>(*this).parent();
  }
  auto children() -> Links & { return _children; }
  auto children() const -> const Links & {
    return const_cast<Node &>(*this).children();
  }

  auto insert(std::unique_ptr<Node> child) -> Node & {
    child->_parent = *this;
    const auto name = child->name();
    const auto tid = child->thread_id();
    return *std::get<1>(*std::get<0>(
        _children.emplace(std::make_pair(name, tid), std::move(child))));
  }

  auto is_parallel() const -> bool {
    for (const auto &[_, child] : _children) {
      if (child->thread_id() != this->thread_id()) {
        return true;
      }
    }
    return false;
  }

private:
  std::string _name;
  Thread::Id _thread_id;
  uint64_t _total_time_ns;
  uint64_t _overhead_time_ns;
  uint64_t _times_called;
  Backlink _parent;
  Links _children;
};

struct split_by_thread_fn {
  using result_t = std::map<Thread::Id, std::unique_ptr<Node>>;
  auto operator()(const Node &root) const -> result_t {
    auto threads = result_t{};
    threads.emplace(root.thread_id(), op(root, threads));
    return threads;
  }

private:
  static auto op(const Node &node, result_t &forest) -> std::unique_ptr<Node> {
    auto new_node = std::make_unique<Node>(
        node.name(), node.thread_id(), node.total_time_ns(),
        node.overhead_time_ns(), node.times_called());
    for (const auto &[link, child] : node.children()) {
      const auto &[name, tid] = link;
      if (node.thread_id() != tid) {
        forest.emplace(tid, op(*child, forest));
      } else {
        new_node->insert(op(*child, forest));
      }
    }
    return std::move(new_node);
  }
};
const auto split_by_thread = split_by_thread_fn{};

struct Profile {
  struct Parameter {
    std::string name;
    std::string type;
    std::string value;
    bool is_output;
  };

  std::string function_name;
  std::vector<Parameter> inputs;
  std::vector<Parameter> outputs;
  std::string schedule;
  std::map<Thread::Id, Thread> thread_table;
  std::unique_ptr<Node> root;
};
auto parse_profile(std::function<std::optional<std::string>()> get_next_line)
    -> Profile {
  using Depth = std::size_t;

  struct Tokenizer {
    auto operator()(const std::string &input) const
        -> std::vector<std::string> {
      auto result = std::vector<std::string>{};
      result.emplace_back();

      for (auto it = input.begin(); it != input.end(); ++it) {
        if (auto c = *it; std::isgraph(c)) {
          result.back().push_back(c);
        } else {
          result.emplace_back();
        }
      }
      return result;
    }
  };
  struct CallTreeParser {
    using SignatureItem = std::variant<std::string, Profile::Parameter>;
    using ScheduleItem = std::tuple<std::string>;
    using ThreadTableItem = std::tuple<Thread>;
    using ProfileItem = std::tuple<std::unique_ptr<Node>, Depth>;
    using ParseResult =
        std::optional<std::variant<SignatureItem, ScheduleItem, ThreadTableItem,
                                   ProfileItem>>;

    auto operator()(std::string line) -> ParseResult {
      const auto &s = strip_inplace(line);

      if (std::regex_search(s, report_delimeter)) {
        advance_stage();
      } else if (std::regex_search(s, report_pattern)) {
        auto t = std::regex_replace(s, report_pattern, "");
        switch (_stage) {
          break;
        case Stage::metadata:
          return parse_signature_item(t);
          break;
        case Stage::schedule:
          return parse_schedule_item(t);
          break;
        case Stage::thread_table:
          return parse_thread_table_item(t);
          break;
        case Stage::profile:
          return parse_profile_item(t);
          break;
        default:
          throw std::runtime_error(
              std::string{"encountered out-of-context data: "} + s);
        }
      }

      return {};
    }

    auto has_finished() const -> bool { return _stage == Stage::finished; }

  private:
    enum class Stage : uint8_t {
      none,
      metadata,
      schedule,
      thread_table,
      profile,
      finished,
      N_STAGES
    };

    void advance_stage() {
      auto i = static_cast<uint8_t>(_stage);
      auto n = static_cast<uint8_t>(Stage::N_STAGES);
      _stage = static_cast<Stage>(++i % n);
    }

    auto parse_signature_item(const std::string &s) const -> SignatureItem {
      if (s.find('=') != std::string::npos) {
        Profile::Parameter p;
        std::smatch m;

        std::regex name_pattern("^[><]\\w+(?=\\s*=)");
        std::regex value_pattern("\\S+(?=\\s*:)");
        std::regex type_pattern("\\S+(?=\\s*$)");

        if (s.front() == '>') {
          p.is_output = false;
        } else if (s.front() == '<') {
          p.is_output = true;
        } else {
          throw std::runtime_error(
              std::string{"failed to locate input/output indicator in "} + s);
        }

        std::regex_search(s, m, name_pattern);
        if (m.empty()) {
          throw std::runtime_error(std::string{"failed to locate name in "} +
                                   s);
        } else {
          auto mstr = m.str(0);
          std::copy(mstr.begin() + 1, mstr.end(), std::back_inserter(p.name));
        }

        std::regex_search(s, m, value_pattern);
        if (m.empty()) {
          throw std::runtime_error(std::string{"failed to locate value in "} +
                                   s);
        } else {
          p.value = m.str(0);
        }

        std::regex_search(s, m, type_pattern);
        if (m.empty()) {
          throw std::runtime_error(std::string{"failed to locate type in "} +
                                   s);
        } else {
          p.type = m.str(0);
        }

        return p;
      } else {
        return s;
      }
    }
    auto parse_schedule_item(const std::string &s) const -> ScheduleItem {
      return s;
    }
    auto parse_thread_table_item(const std::string &s) const
        -> ThreadTableItem {
      const auto columns = Tokenizer{}(s);
      if (columns.size() != 2) {
        throw std::runtime_error(std::string{"expected 2 columns per row: "} +
                                 s);
      }

      Thread::Id thread_id;
      std::string thread_name;

      std::stringstream ss;
      ss << columns[0];
      ss >> thread_id;
      ss = {};
      ss << columns[1];
      ss >> thread_name;
      ss = {};

      return {{thread_id, thread_name}};
    }
    auto parse_profile_item(const std::string &s) const -> ProfileItem {
      const auto [row, depth] = [&] {
        auto row = s;
        const auto depth = std::size_t(std::count(row.begin(), row.end(), '>'));
        row.erase(0, depth);
        strip_inplace(row);

        const auto columns = Tokenizer{}(row);
        if (columns.size() != 5) {
          throw std::runtime_error(std::string{"expected 5 columns per row: "} +
                                   row);
        }
        return std::tuple{columns, depth};
      }();
      const auto [func_name, thread_id, total_time_ns, overhead_time_ns,
                  times_called] = [&] {
        const auto func_name = row[0];
        Thread::Id thread_id;
        uint64_t total_time_us;
        uint64_t overhead_time_us;
        uint64_t times_called;

        std::stringstream ss;
        ss << row[1];
        ss >> thread_id;
        ss = {};
        ss << row[2];
        ss >> total_time_us;
        ss = {};
        ss << row[3];
        ss >> times_called;
        ss = {};
        ss << row[4];
        ss >> overhead_time_us;
        ss = {};

        return std::tuple{func_name, thread_id, 1000 * total_time_us,
                          1000 * overhead_time_us, times_called};
      }();

      return {std::make_unique<Node>(func_name, thread_id, total_time_ns,
                                     overhead_time_ns, times_called),
              depth};
    }

    Stage _stage = Stage::none;

    const std::string report_preamble =
        "..-.. ..:..:...... .* adsprpc : profiler:[0-9]+:0x[a-f0-9]+:[0-9]+: ";
    const std::regex report_pattern = std::regex(report_preamble);
    const std::regex report_delimeter =
        std::regex(report_preamble + "-----------");
  };
  struct CallTreeBuilder {
    void operator()(std::unique_ptr<Node> node, Depth depth) {
      if (call_tree == nullptr) {
        if (depth != 0) {
          throw std::runtime_error{"parsed tree started below depth 0 - is the "
                                   "logcat report incomplete?"};
        }
        call_tree = std::move(node);
        call_stack.emplace_back(*call_tree);
      } else {
        while (depth < call_stack.size()) {
          call_stack.pop_back();
        }
        call_stack.emplace_back(
            insert(std::move(node), *call_tree, std::next(call_stack.begin())));
      }
    }

    auto operator()() -> std::unique_ptr<Node> {
      call_stack.clear();
      return std::move(call_tree);
    }

  private:
    std::unique_ptr<Node> call_tree;
    std::vector<std::reference_wrapper<Node>> call_stack;

    auto insert(std::unique_ptr<Node> node, Node &focus,
                decltype(call_stack)::iterator substack) -> Node & {
      if (substack == call_stack.end()) {
        return focus.insert(std::move(node));
      } else {
        const auto &top = (*substack).get();
        return insert(
            std::move(node),
            *focus.children().at(std::make_pair(top.name(), top.thread_id())),
            std::next(substack));
      }
    }
  };
  struct MetadataBuilder {
    void operator()(const std::string &name) { _name = name; }
    void operator()(const Profile::Parameter &param) {
      if (param.is_output) {
        _outputs.push_back(param);
      } else {
        _inputs.push_back(param);
      }
    }
    void operator()(CallTreeParser::ScheduleItem &line) {
      if (not _schedule.empty()) {
        _schedule.push_back('\n');
      }
      _schedule += std::get<std::string>(line);
    }
    void operator()(CallTreeParser::ThreadTableItem &entry) {
      auto &[item] = entry;
      _thread_table.emplace(item.id, item);
    }

    auto function_name() const -> const std::string & { return _name; }
    auto inputs() const -> const std::vector<Profile::Parameter> & {
      return _inputs;
    }
    auto outputs() const -> const std::vector<Profile::Parameter> & {
      return _outputs;
    }
    auto schedule() const -> const std::string & { return _schedule; }
    auto thread_table() const -> const std::map<Thread::Id, Thread> & {
      return _thread_table;
    }

  private:
    std::string _name;
    std::vector<Profile::Parameter> _inputs;
    std::vector<Profile::Parameter> _outputs;
    std::string _schedule;
    std::map<Thread::Id, Thread> _thread_table;
  };

  auto build = CallTreeBuilder{};
  auto parse = CallTreeParser{};
  auto describe = MetadataBuilder{};

  for (std::optional<std::string> line;
       not parse.has_finished() and (line = get_next_line()).has_value();) {
    if (auto parsed = parse(line.value())) {
      std::visit(match{[&](CallTreeParser::SignatureItem &x) {
                         std::visit(describe, x);
                       },
                       [&](CallTreeParser::ScheduleItem &x) { describe(x); },
                       [&](CallTreeParser::ThreadTableItem &x) { describe(x); },
                       [&](CallTreeParser::ProfileItem &x) {
                         auto [node, depth] = std::move(x);
                         build(std::move(node), depth);
                       }},
                 *parsed);
    }
  }

  return {describe.function_name(), describe.inputs(),       describe.outputs(),
          describe.schedule(),      describe.thread_table(), build()};
}

struct NodePrinter {
  const std::map<Thread::Id, Thread> &thread_table;
  const Node &node;

private:
  friend void to_json(json &j, const NodePrinter &x) {
    const auto &root = x.node;
    const auto parallel = root.is_parallel();

    j = json{{"name", root.name()},
             {"thread_id", root.thread_id()},
             {"total_time_ns", root.total_time_ns()},
             {"overhead_time_ns", root.overhead_time_ns()},
             {"times_called", root.times_called()},
             {"self_time_ns", root.self_time_ns()},
             {"mean_total_time_ns", root.mean_total_time_ns()},
             {"mean_self_time_ns", root.mean_self_time_ns()},
             {"root_relatime", root.root_relatime()},
             {"self_relatime", root.self_relatime()},
             {"self_root_relatime", root.self_root_relatime()},
             {"parent_relatime", root.parent_relatime()
                                     ? json(root.parent_relatime().value())
                                     : json(nullptr)},
             {parallel ? "forks" : "loops", [&] {
                std::map<std::string, json> j;
                for (const auto &[link, child] : root.children()) {
                  const auto name = [&] {
                    const auto &[name, tid] = link;
                    if (parallel) {
                      return x.thread_table.at(tid).name;
                    } else {
                      return name;
                    }
                  }();
                  j.emplace(name, NodePrinter{x.thread_table, *child});
                }
                return j;
              }()}};
  }
};
struct ThreadPrinter {
  const Thread &thread;

private:
  friend void to_json(json &j, const ThreadPrinter &x) {
    const auto &thread = x.thread;

    j = json{{"name", thread.name}, {"thread_id", thread.id}};
  }
};
struct ProfilePrinter {
  const Profile &profile;

private:
  friend void to_json(json &j, const ProfilePrinter &x) {
    const auto &prof = x.profile;
    j = json{
        {"signature",
         json{{"name", prof.function_name},
              {"inputs",
               [&] {
                 json j;
                 for (const auto &p : prof.inputs) {
                   j.emplace_back(json{
                       {"name", p.name}, {"type", p.type}, {"value", p.value}});
                 }
                 return j;
               }()},
              {"outputs",
               [&] {
                 json j;
                 for (const auto &p : prof.outputs) {
                   j.emplace_back(json{
                       {"name", p.name}, {"type", p.type}, {"value", p.value}});
                 }
                 return j;
               }()}}},
        {"schedule", prof.schedule},
        {"thread_table",
         [&] {
           std::map<std::string, json> threads;
           for (const auto &[_, thread] : prof.thread_table) {
             threads.emplace(std::to_string(thread.id),
                             json(ThreadPrinter{thread}));
           }
           return threads;
         }()},
        {"call_tree", json(NodePrinter{prof.thread_table, *prof.root})},
        {"call_trees_by_thread", json([&] {
           std::map<std::string, json> threads;
           for (const auto &[tid, thread] : split_by_thread(*prof.root)) {
             threads.emplace(x.profile.thread_table.at(tid).name,
                             json(NodePrinter{prof.thread_table, *thread}));
           }
           return threads;
         }())},
    };
  }
};

int main(int argc, const char **argv) {
  cxxopts::Options options(
      argv[0], "Read Halide-Hexagon profiling output on adb logcat from stdin,"
               " and output a structured JSON object.");

  options.add_options()("h,help", "Display help", cxxopts::value<bool>())(
      "o,output",
      "Output file basename. Files will be named "
      "`{BASENAME}-{TIMESTAMP}.json`. If not provided, outputs to stdout.",
      cxxopts::value<std::string>());

  auto args = options.parse(argc, argv);

  if(args.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
  }

  auto read_stdin = [] {
    std::string line;
    std::getline(std::cin, line);
    return line;
  };
  auto emit = [&](const Profile &profile) -> void {
    if (args.count("output")) {
      static std::size_t n_emitted = 0;
      std::stringstream filename;
      std::stringstream time_format;

      const auto now = std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now());

      time_format << "-%FT%T%z-" << (n_emitted++);

      filename << args["output"].as<std::string>()
               << std::put_time(std::localtime(&now), time_format.str().c_str()) << ".json";

      std::ofstream output(filename.str());
      output << json(ProfilePrinter{profile});
    } else {
      std::cout << json(ProfilePrinter{profile}) << std::endl;
    }
  };

  for (;;) {
    try {
      emit(parse_profile(read_stdin));
    } catch (std::exception &e) {
      std::cerr << "profile parse failure: " << e.what() << std::endl;
    }
  }
}
