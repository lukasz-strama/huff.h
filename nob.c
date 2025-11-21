#define NOB_IMPLEMENTATION

#include "nob.h"

#define BUILD_FOLDER "build/"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (!nob_mkdir_if_not_exists(BUILD_FOLDER)) return 1;
    Nob_Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "-O3", "-march=native", "-pthread");
    nob_cc_output(&cmd, BUILD_FOLDER "huff");
    nob_cc_inputs(&cmd, "main.c");
    nob_cmd_append(&cmd, "-lm");
    if (!nob_cmd_run_sync(cmd)) return 1;

    // Build test runner
    cmd.count = 0;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-O3", "-march=native", "-pthread");
    nob_cmd_append(&cmd, "-o", "build/test", "test.c", "-lm");
    if (!nob_cmd_run_sync(cmd)) return 1;

    return 0;
}
