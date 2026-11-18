#ifndef WL_CORE_EXIT_CODE_H
#define WL_CORE_EXIT_CODE_H

/*
 * Process exit codes used by the bootstrap binary.
 *
 * 0 and 2 follow the shell convention (success, usage error). 3 is temporary:
 * it means "this build knows the request but does not implement it yet", so a
 * wrapper script cannot mistake a stub for a working shell
 * (docs/DECISIONS.md D-0026).
 */
enum wl_exit_code {
    WL_EXIT_OK = 0,
    WL_EXIT_FAILURE = 1,
    WL_EXIT_USAGE = 2,
    WL_EXIT_UNIMPLEMENTED = 3
};

#endif /* WL_CORE_EXIT_CODE_H */
