#ifndef WL_SUITES_H
#define WL_SUITES_H

/* One declaration per test suite; test_main.c registers them all. */

void wl_suite_opts(void);
void wl_suite_path(void);
void wl_suite_prompt(void);
void wl_suite_builtins(void);
void wl_suite_ast(void);
void wl_suite_fuzz(void);
void wl_suite_var(void);
void wl_suite_history(void);
void wl_suite_narrative(void);
void wl_suite_pack(void);
void wl_suite_state(void);
void wl_suite_arith(void);

#endif /* WL_SUITES_H */
