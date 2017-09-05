#ifndef HALIDE_EXPECT_DEATH_H
#define HALIDE_EXPECT_DEATH_H

#include <stdio.h>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>     
#endif

namespace {

inline void halide_expect_death(int argc, char **argv) {
#ifdef _WIN32
    const char *halide_expect_death_flag = "--halide_expect_death_flag";

    if (argc == 2 && !strcmp(argv[1], halide_expect_death_flag)) {
        // I'm the expected-to-fail worker
        return;
    }

    // I'm the master (unless argv/argc are weird)
    if (argc != 1) {
        fprintf(stderr, "Unsupported argc/argv in halide_expect_death().\n");
        exit(1);
    }

    char self_path[_MAX_PATH + 1];
    if (!::GetModuleFileNameA(NULL, self_path, _MAX_PATH)) {
        fprintf(stderr, "GetModuleFileNameA failed.\n");
        exit(1);
    }

    char current_dir[_MAX_PATH];
    if (!::GetCurrentDirectoryA(_MAX_PATH, current_dir)) {
        fprintf(stderr, "GetCurrentDirectoryA failed.\n");
        exit(1);
    }

    std::string command_line = std::string(::GetCommandLineA()) + " " + halide_expect_death_flag;

    STARTUPINFOA startup_info;
    memset(&startup_info, 0, sizeof(STARTUPINFO));
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    startup_info.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process_info;
    BOOL result = ::CreateProcessA(
        self_path,
        const_cast<char*>(command_line.c_str()),
        NULL,
        NULL,
        TRUE,
        0x0,
        NULL,
        current_dir,
        &startup_info,
        &process_info);
    if (!result) {
        fprintf(stderr, "CreateProcessA failed.\n");
        exit(1);
    }
    ::CloseHandle(process_info.hThread);

    if (::WaitForSingleObject(process_info.hProcess, INFINITE) != WAIT_OBJECT_0) {
        fprintf(stderr, "WaitForSingleObject failed.\n");
        exit(1);
    }

    DWORD status_code;
    if (!::GetExitCodeProcess(process_info.hProcess, &status_code)) {
        fprintf(stderr, "GetExitCodeProcess failed.\n");
        exit(1);
    }
    ::CloseHandle(process_info.hProcess);

    if (status_code) {
        printf("Success!\n");
        exit(0);
    } else {
        fprintf(stderr, "Expected Failure, but got Success (%s).\n", argv[0]);
        exit(1);
    }
#else
    int child_pid = fork();
    if (!child_pid) { 
        // I'm the expected-to-fail worker
        return;
    }
    // I'm the master
    int child_status = 0;
    waitpid(child_pid, &child_status, 0);
    if (child_status) {
        printf("Success!\n");
        exit(0);
    } else {
        fprintf(stderr, "Expected Failure, but got Success (%s).\n", argv[0]);
        exit(1);
    }
#endif
}

}  // namespace


#define HALIDE_EXPECT_DEATH(ARGC, ARGV)  \
    do {                       \
        halide_expect_death((ARGC), (ARGV)); \
    } while (0)


#endif  // HALIDE_EXPECT_DEATH_H
