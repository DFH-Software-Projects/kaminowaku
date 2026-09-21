// Copyright 2026 Jamison A. Drapeau
#include "targets.h"

#undef targets_print_usage
#undef targets_display_from_project

void targets_print_usage_guard(void) {
        targets_print_usage();
}

void targets_display_from_project_guard(
        _carry_forward * _prog_data
) {
        targets_display_from_project(_prog_data);
}
