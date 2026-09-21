// Copyright 2026 Jamison A. Drapeau
#ifndef __TOOL_PTY__H
#define __TOOL_PTY__H

#include "data.h"

// @@ Interactive PTY bridge for registered external tools
int tool_pty_run(
        const char * tool_name,
        const char * executable_path,
        char * const child_argv[],
        const char * working_directory,
        int * wait_status
);

#endif
