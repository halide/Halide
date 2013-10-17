import os

stub = open("template.vcxproj").read()

tests = os.listdir('../../test/correctness')

tests = [x.split('.')[0] for x in tests if x.endswith('.cpp')]

exclude = ["simd_op_check"]

runner = open("run_all_tests.bat", "w")
for t in tests:
    print t
    if t in exclude: continue
    open("test_" + t + ".vcxproj", "w").write(stub.replace('XXXXX', t))
    runner.write("echo Running test " + t + "\n"
                 "test_" + t + ".exe\n"
                 "if not errorlevel 0 exit 1\n")
