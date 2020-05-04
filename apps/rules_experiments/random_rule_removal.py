import subprocess

def main(nprocs, output_dir):
    reset_excluded_ruleset()
    excluded = pick_rules_to_exclude(50)
    output_excluded_ruleset(excluded)
    run_tests(nprocs, output_dir)
    output_candiate_expressions(output_dir)

def gather_expressions(directory):
    """
    Gathers expressions from output files to feed into super_simplify
    """
    import glob
    import re
    exprs = []
    files = glob.glob(directory + "/output.tmp_stderr_*")
    for fname in files:
        with open(fname, "r") as fr:
            lines = fr.readlines()
            for line_num in range(len(lines)):
                if re.search("Failed to prove", lines[line_num]):
                    #print("Failed to prove: ", lines[line_num+1])
                    exprs.append(lines[line_num+1])
                if re.search("expression is non-monotonic", lines[line_num]):
                    exprs.append(lines[line_num].split(":")[-1])
                    #print("non-monotonic: ", exprs[-1])
    return list(set(exprs))

def output_candiate_expressions(directory):
    with open(directory + "candidates.txt", "w") as outfile:
        outfile.writelines(gather_expressions(directory))

def parse_rule_match_csv():
    """
    Loads a master file with which rules matched
    in which test set, returning a table
    """
    import pandas
    table = pandas.read_csv("rules_matched.csv", delim_whitespace=False, sep=",")
    return table

def pick_rules_to_exclude(num, test_set='test_correctness', thresh=0):
    """
    Return a list of random rules to exclude.
    Rules will be selected by filtering for at least thresh triggerings
    in test_set.
    """
    matched_table = parse_rule_match_csv()
    potential_rules = matched_table.loc[lambda df: df[test_set] > thresh]
    import random
    excluded_rules = (potential_rules.iloc[random.sample(range(potential_rules.shape[0]), num), :])
    print("Excluding rules: ")
    print(excluded_rules.loc[:,['rule', test_set]])
    return list(excluded_rules.loc[:,'rule'])

def reset_excluded_ruleset(loc='../../src/Simplify_*.cpp'):
    import glob
    import shutil

    all_files = glob.glob(loc)
    all_files.append("../../src/Interval.cpp")
    all_files.append("../../src/SimplifyCorrelatedDifferences.cpp")
    print("Resetting excluded ruleset...")
    for f in all_files:
        print("  Copying %s to %s" % (f+".orig", f))
        shutil.copyfile(f+".orig", f)


def output_excluded_ruleset(rules, loc='../../src/Simplify_*.cpp'):
    import glob
    import re

    all_files = glob.glob(loc)
    all_files.append("../../src/Interval.cpp")
    all_files.append("../../src/SimplifyCorrelatedDifferences.cpp")
    for rule in rules:
        #print ("Searching for rule ", rule)
        found = False
        for fname in all_files:
            #print("  searching ", fname)
            with open(fname+".orig", 'r') as fr:
                with open(fname, 'w') as fw:
                    for line in fr.readlines():
                        if re.search("\"" + rule + "\"", line):
                            fw.write(re.sub(rule, '*'+rule, line))
                            found = True
                        else:
                            fw.write(line)
        if (not found):
            print("ERROR: could not find rule %s" % rule)

def determine_test_suite():
    """
    For now, just list out all tests in test/correctness.
    """
    import os
    all_files = os.listdir("../../test/correctness")
    all_cpp_files = filter(lambda x: ".cpp" in x, all_files)
    # x[:-4] gets rid of .cpp
    all_test_cases = ["correctness_%s" % (x[:-4]) for x in all_cpp_files]
    return all_test_cases

def run_single_test(test_name, directory):
    timeout = 0
    compile_failure = 0
    env = {"HL_EXCLUDE_MISORDERED_RULES":"0",
           "HL_USE_SYNTHESIZED_RULES":"0",
           "HL_DEBUG_CODEGEN":"0",
           "CLANG":"/Users/kamil/Documents/work/adobe/halide-builder/builds/halide-master_llvm-90/llvm/make/MacOS/Release/bin/clang",
           "LLVM_CONFIG":"/Users/kamil/Documents/work/adobe/halide-builder/builds/halide-master_llvm-90/llvm/make/MacOS/Release/bin/llvm-config"}

    print("Running test: " + test_name)
    #with open("outputs/output.tmp_stdout_"+test_name, "w") as out:
    #    with open("outputs/output.tmp_stderr_"+test_name, "w", 1) as err:
    root_dir = "../.."
    subprocess.Popen("make bin/" + test_name + " &>/dev/null", cwd=root_dir, env=env,
                            shell=True).wait()
    p = subprocess.Popen("bin/" + test_name + " 2>apps/rules_experiments/" + directory + "/output.tmp_stderr_"+test_name + " >apps/rules_experiments/" + directory + "/output.tmp_stdout_"+test_name, cwd=root_dir, env=env,
                            shell=True)
    try:
        p.wait(timeout=120)
    except subprocess.TimeoutExpired:
        timeout = 1
        print("--> Timeout for " + test_name)
    if not timeout and p.returncode != 0:
        compile_failure = 1
        print("--> Compile failure for " + test_name)
    return (timeout, compile_failure)

# Python multiprocessing requires a picklable object,
# so we introduce this callable class to specialize
# the output directory
class SingleTestRunner(object):
    def __init__(self, directory):
        self.directory = directory
    def __call__(self, test_name):
        return run_single_test(test_name, self.directory)

def run_tests(nprocs, directory):
    print("Compiling...")
    # first, just build
    env = {"HL_EXCLUDE_MISORDERED_RULES":"0",
           "HL_USE_SYNTHESIZED_RULES":"1",
           "CLANG":"/Users/kamil/Documents/work/adobe/halide-builder/builds/halide-master_llvm-90/llvm/make/MacOS/Release/bin/clang",
           "LLVM_CONFIG":"/Users/kamil/Documents/work/adobe/halide-builder/builds/halide-master_llvm-90/llvm/make/MacOS/Release/bin/llvm-config"}
    import os
    from pathlib import Path
    Path(directory).mkdir(exist_ok=True)
    with open(directory + "output.tmp", "w") as out:
        root_dir = os.path.abspath("../..")
        subprocess.Popen(["make", "-j"], cwd=root_dir, env=env, stdout=out, stderr=subprocess.STDOUT).wait()

    # now clear out old data and run
    import shutil
    shutil.rmtree("../../bin/build")

    # run experiment
    timeouts = 0
    compile_failures = 0
    non_monotonic_warnings = 0
    failed_to_simplifies = 0

    experiments = determine_test_suite()

    import multiprocessing
    pool = multiprocessing.Pool(nprocs)

    run_results = pool.imap_unordered(SingleTestRunner(directory), experiments)
    pool.close()
    pool.join()

    from functools import reduce
    timeouts, compile_failures = reduce(lambda s, x: (s[0]+x[0], s[1]+x[1]), run_results)

    for experiment in experiments:
        # count non_monotonic_warnings
        # and failed_to_simplifies
        import re
        with open(directory + "/output.tmp_stderr_"+experiment) as out:
            for line in out:
                if re.search("monotonic", line, flags=re.I):
                    non_monotonic_warnings += 1
                if re.search("Failed to prove", line, flags=re.I):
                    failed_to_simplifies += 1

    print("Timeouts: %d" % timeouts)
    print("Compile failures: %d" % compile_failures)
    print("Non-monotonic warnings: %d" % non_monotonic_warnings)
    print("Failed to proves: %d" % failed_to_simplifies)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser("Run random rule removal experiment.")
    parser.add_argument('--reset-only', dest='reset_only', action='store_true')
    parser.add_argument('--baseline', dest='baseline', action='store_true')
    parser.add_argument('--nprocs', dest='nprocs', action='store', type=int, default=8)
    parser.add_argument('--output-dir', dest='output_dir', action='store', required=True)
    args = parser.parse_args()

    if args.reset_only:
        reset_excluded_ruleset()
        exit(0)
    if args.baseline:
        reset_excluded_ruleset()
        run_tests(args.nprocs, args.output_dir)
        exit(0)

    main(args.nprocs, args.output_dir)
