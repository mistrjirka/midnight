/* SPDX-License-Identifier: GPL-2.0-only */
/* One-shot solves started from the menu, and the last result for the menu readout. */
#ifndef ASTRO_SOLVE_ACTIONS_H
#define ASTRO_SOLVE_ACTIONS_H

#include "solve.h"

/* Worker: Live View (after a 3 s settling delay) or the selected test PGM. */
void astro_run_solve(int from_test_file);

/* The most recent successful solution (valid == 0 before the first one). */
const astro_solve_result *astro_last_solution(void);
void astro_remember_solution(const astro_solve_result *result);

#endif
