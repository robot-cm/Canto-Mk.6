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

#endif /* COS_EQSOLVER_SOLVER_H */
