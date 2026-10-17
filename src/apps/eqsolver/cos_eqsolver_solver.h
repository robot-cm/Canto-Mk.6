/**
 * @file cos_eqsolver_solver.h
 * @brief Equation system solver (linear via Gaussian elimination, non-linear
 *        via multi-start damped Newton / Gauss-Newton).
 */

#ifndef COS_EQSOLVER_SOLVER_H
#define COS_EQSOLVER_SOLVER_H

#include <stddef.h>

/**
 * @brief Solve a system of equations and write an English result string.
 *
 * @param eqs      Array of equation strings (each may contain at most one '=').
 * @param n_eqs    Number of equations (<= EQSOLVER_MAX_EQS).
 * @param out      Output buffer for the human-readable result.
 * @param out_size Size of out.
 * @return int 0 if a result string was produced (even if "No solution"),
 *             non-zero only on a fatal internal error.
 */
int cos_eqsolve(const char *eqs[], int n_eqs, char *out, size_t out_size);

/**
 * @brief Solve a system *with respect to* an explicit set of unknowns.
 *
 * This is the "solve for ..." entry point: it answers both "solve this system
 * for x and y" and "express y in terms of x". Every letter that appears in the
 * equations but is not listed in @p vars becomes a symbolic parameter, so the
 * result stays exact, e.g.:
 *
 *   eqs = {"y = 2x+1"}, vars = "y"              -> "y = 1 + 2x"
 *   eqs = {"x+y=3", "x-y=1"}, vars = "x,y"      -> "x = 2 / y = 1"
 *   eqs = {"x+y+z=6", "x-y=0"}, vars = "x,y"    -> "x = 3 - 1/2z / y = 3 - 1/2z"
 *
 * When a linear system is underdetermined the remaining unknowns are reported
 * as free and the others are expressed in terms of them, so a single equation
 * like "x+y=3" yields "x = 3 - y" instead of a bare "infinite solutions".
 *
 * @param eqs      Array of equation strings.
 * @param n_eqs    Number of equations (<= EQSOLVER_MAX_EQS).
 * @param vars     Unknown letters, e.g. "x,y", "x y" or "xy" (order is kept in
 *                 the output). NULL or "" restores auto-detection: every letter
 *                 in the equations is an unknown (same as cos_eqsolve()).
 * @param out      Output buffer for the human-readable result.
 * @param out_size Size of out.
 * @return int 0 if a result string was produced (even if "No solution"),
 *             non-zero only on a fatal internal error.
 */
int cos_eqsolve_for(const char *eqs[], int n_eqs, const char *vars,
                    char *out, size_t out_size);

#endif /* COS_EQSOLVER_SOLVER_H */
