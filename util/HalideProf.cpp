#include <algorithm>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>

namespace {

  const char kToplevel[] = "$total$";
  const char kOverhead[] = "$overhead$";
  const char kIgnore[] = "$ignore$";

  struct OpInfo {
    std::string op_type;
    std::string op_name;
    std::string parent_type;
    std::string parent_name;
    // number of times called
    int64_t count;
    // ticks used (processor specific, no fixed time interval)
    int64_t ticks;
    // nsec is actually only measured for $total$;
    // it's (approximated) calculated for all others
    double nsec;
    // percentage of total ticks, [0.0..1.0]
    double percent;
    // ticks/nsec/percent used by this op alone (not including callees)
    int64_t ticks_only;
    double nsec_only;
    double percent_only;

    OpInfo()
      : count(0),
        ticks(0),
        nsec(0.0),
        percent(0.0),
        ticks_only(0),
        nsec_only(0.0),
        percent_only(0.0) {}
  };

  // Outer map is keyed by function name,
  // inner map is keyed by op_type + op_name
  typedef std::map<std::string, OpInfo> OpInfoMap;
  typedef std::map<std::string, OpInfoMap> FuncInfoMap;

  std::string qualified_name(const std::string& op_type, const std::string& op_name) {
    // Arbitrary, just join type + name
    return op_type + ":" + op_name;
  }

  // How is it posible that there is no string-split function in the C++
  // standard library?
  std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> v;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        v.push_back(item);
    }
    return v;
  }

  bool HasOpt(char** begin, char** end, const std::string& opt) {
    return std::find(begin, end, opt) != end;
  }

  // look for string "opt"; if found, return subsequent string;
  // if not found, return empty string.
  std::string GetOpt(char** begin, char** end, const std::string& opt) {
    char** it = std::find(begin, end, opt);
    if (it != end && ++it != end) {
        return std::string(*it);
    }
    return std::string();
  }

  void ProcessLine(const std::string& s, FuncInfoMap& info, bool accumulate_runs) {
    std::vector<std::string> v = Split(s, ' ');
    if (v.size() < 8) {
      return;
    }
    // Some environments (e.g. Android logging) will emit a prefix
    // for each line; skip the first few words we see before
    // deciding to ignore the line.
    int first = -1;
    for (int i = 0; i < 4; ++i) {
      if (v[i] == "halide_profiler") {
        first = i;
        break;
      }
    }
    if (first < 0) {
      return;
    }
    const std::string& metric = v[first + 1];
    const std::string& func_name = v[first + 2];
    const std::string& op_type = v[first + 3];
    const std::string& op_name = v[first + 4];
    const std::string& parent_type = v[first + 5];
    const std::string& parent_name = v[first + 6];
    if (op_type == kIgnore || op_name == kIgnore) {
      return;
    }
    std::istringstream value_stream(v[first + 7]);
    int64_t value;
    value_stream >> value;
    OpInfoMap& op_info_map = info[func_name];
    OpInfo& op_info = op_info_map[qualified_name(op_type, op_name)];
    op_info.op_type = op_type;
    op_info.op_name = op_name;
    op_info.parent_type = parent_type;
    op_info.parent_name = parent_name;
    if (metric == "count") {
      op_info.count = (accumulate_runs ? op_info.count : 0) + value;
    } else if (metric == "ticks") {
      op_info.ticks = (accumulate_runs ? op_info.ticks : 0) + value;
    } else if (metric == "nsec") {
      op_info.nsec = (accumulate_runs ? op_info.nsec : 0) + value;
    }
  }

  typedef std::map<std::string, std::vector<OpInfo*> > ChildMap;

  int64_t AdjustOverhead(OpInfo& op_info, ChildMap& child_map, double overhead_ticks_avg) {
    int64_t overhead_ticks = op_info.count * overhead_ticks_avg;

    std::string qual_name = qualified_name(op_info.op_type, op_info.op_name);
    const std::vector<OpInfo*>& children = child_map[qual_name];
    for (std::vector<OpInfo*>::const_iterator it = children.begin(); it != children.end(); ++it) {
      OpInfo* c = *it;
      int64_t child_overhead = AdjustOverhead(*c, child_map, overhead_ticks_avg);
      overhead_ticks += child_overhead;
    }

    op_info.ticks -= overhead_ticks;

    return overhead_ticks;
  }

  void FinishOpInfo(OpInfoMap& op_info_map, bool adjust_for_overhead) {

    std::string toplevel_qual_name = qualified_name(kToplevel, kToplevel);
    OpInfo& total = op_info_map[toplevel_qual_name];
    total.percent = 1.0;

    double ticks_per_nsec = (double)total.ticks / (double)total.nsec;

    // Note that overhead (if present) is measured outside the rest
    // of the "total", so it should not be included (or subtracted from)
    // the total.
    double overhead_ticks_avg = 0;
    std::string overhead_qual_name = qualified_name(kOverhead, kOverhead);
    OpInfoMap::iterator it = op_info_map.find(overhead_qual_name);
    if (it != op_info_map.end()) {
      OpInfo overhead = it->second;
      overhead_ticks_avg = (double)overhead.ticks / ((double)overhead.count * 2.0);
      op_info_map.erase(it);
    }

    ChildMap child_map;
    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      OpInfo& op_info = o->second;
      std::string parent_qual_name = qualified_name(op_info.parent_type, op_info.parent_name);
      child_map[parent_qual_name].push_back(&op_info);
    }

    if (adjust_for_overhead) {
      // Adjust values to account for profiling overhead
      AdjustOverhead(total, child_map, overhead_ticks_avg);
    }

    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      OpInfo& op_info = o->second;
      op_info.ticks_only = op_info.ticks;
      const std::vector<OpInfo*>& children = child_map[o->first];
      for (std::vector<OpInfo*>::const_iterator it = children.begin(); it != children.end(); ++it) {
        OpInfo* c = *it;
        op_info.ticks_only -= c->ticks;
      }
    }

    // Calc the derived fields
    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      OpInfo& op_info = o->second;
      op_info.nsec = op_info.ticks / ticks_per_nsec;
      op_info.nsec_only = op_info.ticks_only / ticks_per_nsec;
      op_info.percent = (double)op_info.ticks / (double)total.ticks;
      op_info.percent_only = (double)op_info.ticks_only / (double)total.ticks;
    }
  }

  template <typename CmpFunc>
  std::vector<OpInfo> SortOpInfo(const OpInfoMap& op_info_map, CmpFunc cmp) {
    std::vector<OpInfo> v;
    for (OpInfoMap::const_iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      v.push_back(o->second);
    }
    // Note: using reverse iterators to get a descending sort
    std::sort(v.rbegin(), v.rend(), cmp);
    return v;
  }

  bool by_count(const OpInfo& a, const OpInfo& b) { return a.count < b.count; }
  bool by_ticks(const OpInfo& a, const OpInfo& b) { return a.ticks < b.ticks; }
  bool by_ticks_only(const OpInfo& a, const OpInfo& b) { return a.ticks_only < b.ticks_only; }

}  // namespace

int main(int argc, char** argv) {

  if (HasOpt(argv, argv + argc, "-h")) {
    printf("HalideProf [-f funcname] [-sort c|t|to] [-top N] [-overhead=0|1] [-accumulate=[0|1]] < profiledata\n");
    return 0;
  }

  std::string func_name_filter = GetOpt(argv, argv + argc, "-f");

  bool (*sort_by_func)(const OpInfo& a, const OpInfo& b);
  std::string sort_by = GetOpt(argv, argv + argc, "-sort");
  if (sort_by.empty() || sort_by == "to") {
    sort_by_func = by_ticks_only;
  } else if (sort_by == "t") {
    sort_by_func = by_ticks;
  } else if (sort_by == "c") {
    sort_by_func = by_count;
  } else {
    std::cerr << "Unknown value for -sort: " << sort_by << "\n";
    exit(-1);
  }

  int32_t top_n = 10;
  std::string top_n_str = GetOpt(argv, argv + argc, "-top");
  if (!top_n_str.empty()) {
    std::istringstream(top_n_str) >> top_n;
  }

  // It's rare that you wouldn't want to try to adjust the times
  // to minimize the effect profiling overhead, but just in case,
  // allow -overhead 0
  int32_t adjust_for_overhead = 1;
  std::string adjust_for_overhead_str = GetOpt(argv, argv + argc, "-overhead");
  if (!adjust_for_overhead_str.empty()) {
    std::istringstream(adjust_for_overhead_str) >> adjust_for_overhead;
  }

  int32_t accumulate_runs = 0;
  std::string accumulate_runs_str = GetOpt(argv, argv + argc, "-accumulate");
  if (!accumulate_runs_str.empty()) {
    std::istringstream(accumulate_runs_str) >> accumulate_runs;
  }

  FuncInfoMap func_info_map;
  std::string line;
  while (std::getline(std::cin, line)) {
    ProcessLine(line, func_info_map, accumulate_runs);
  }

  for (FuncInfoMap::iterator f = func_info_map.begin(); f != func_info_map.end(); ++f) {
    FinishOpInfo(f->second, adjust_for_overhead != 0);
  }

  for (FuncInfoMap::iterator f = func_info_map.begin(); f != func_info_map.end(); ++f) {
    const std::string& func_name = f->first;
    if (!func_name_filter.empty() && func_name_filter != func_name) {
      continue;
    }
    std::cout << "Func: " << func_name << "\n";
    std::cout << "--------------------------\n";
    std::cout
      << std::setw(10) << std::left << "op_type"
      << std::setw(40) << std::left << "op_name"
      << std::setw(16) << std::right << "count"
      << std::setw(16) << "ticks-cum"
      << std::setw(12) << "msec-cum"
      << std::setw(8) << std::fixed << "%-cum"
      << std::setw(16) << "ticks-only"
      << std::setw(12) << "msec-only"
      << std::setw(8) << std::fixed << "%-only"
      << "\n";
    std::vector<OpInfo> op_info = SortOpInfo(f->second, sort_by_func);
    for (std::vector<OpInfo>::const_iterator o = op_info.begin(); o != op_info.end(); ++o) {
      const OpInfo& op_info = *o;
      std::cout
        << std::setw(10) << std::left << op_info.op_type
        << std::setw(40) << std::left << op_info.op_name
        << std::setw(16) << std::right << op_info.count
        << std::setw(16) << op_info.ticks
        << std::setw(12) << std::setprecision(2) << std::fixed << (op_info.nsec / 1000000.0)
        << std::setw(8) << std::setprecision(2) << std::fixed << (op_info.percent * 100.0)
        << std::setw(16) << op_info.ticks_only
        << std::setw(12) << std::setprecision(2) << std::fixed << (op_info.nsec_only / 1000000.0)
        << std::setw(8) << std::setprecision(2) << std::fixed << (op_info.percent_only * 100.0)
        << "\n";
      if (--top_n <= 0) {
        break;
      }
    }
  }
}
