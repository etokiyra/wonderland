#ifndef WL_SUITES_H
#define WL_SUITES_H

/* One declaration per test suite; test_main.c registers them all. */

void wl_suite_opts(void);
void wl_suite_split(void);
void wl_suite_path(void);
void wl_suite_prompt(void);
void wl_suite_builtins(void);

#endif /* WL_SUITES_H */
