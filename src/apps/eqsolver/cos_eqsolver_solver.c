/**
 * @file cos_eqsolver_solver.c
 * @brief Equation system solver implementation.
 */

#include "cos_eqsolver_parser.h"
#include "cos_eqsolver_solver.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAXU EQSOLVER_MAX_VARS
#define MAXV EQSOLVER_MAX_VARS
#define MAXP EQSOLVER_MAX_PARAMS
#define MAXS EQSOLVER_MAX_SYMBOLS
#define MAXE EQSOLVER_MAX_EQS

#define RES_TOL_LIN 1e-3f
#define RES_TOL_SQ 1e-3f
#define RES_TOL_OVER 1e-2f
#define RES_TOL_POLISH 1e-9f
#define SOLVE_TOL 1e-2f

/* ---------------- helpers ---------------- */

static void _append(char *out, size_t *off, size_t cap, const char *s)
{
    if (!s)
    {
        return;
    }
    while (*s && *off + 1 < cap)
    {
        out[(*off)++] = *s++;
    }
    if (*off < cap)
    {
        out[*off] = '\0';
    }
}

static void _append_fmt(char *out, size_t *off, size_t cap, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _append(out, off, cap, buf);
}

/* Greatest common divisor (returns a non-negative value). */
static long _gcd_long(long a, long b)
{
    a = (a < 0) ? -a : a;
    b = (b < 0) ? -b : b;
    while (b != 0)
    {
        long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* A fraction q/p is a terminating decimal iff p (in lowest terms) has no
 * prime factors other than 2 and 5. */
static bool _is_terminating(long p)
{
    p = (p < 0) ? -p : p;
    while ((p % 2) == 0) p /= 2;
    while ((p % 5) == 0) p /= 5;
    return p == 1;
}

/* Approximate |x| with a rational q/p (p <= pmax) whose error is <= tol.
 * Returns 0 on success (writes *q, *p), -1 if no suitable fraction is found. */
static int _double_to_frac(double x, long pmax, double tol, long *q, long *p)
{
    if (x < 0.0) x = -x;
    if (x < 1e-12)
    {
        *q = 0;
        *p = 1;
        return 0;
    }

    long a0 = (long)floor(x);
    double frac = x - (double)a0;
    long h_prev = 1, h_curr = a0; /* numerator of convergents */
    long k_prev = 0, k_curr = 1;  /* denominator of convergents */

    /* Initial convergent a0/1. */
    if (fabs((double)a0 - x) <= tol)
    {
        *q = a0;
        *p = 1;
        return 0;
    }

    for (int iter = 0; iter < 200; iter++)
    {
        if (frac < 1e-12) break;
        double inv = 1.0 / frac;
        if (!isfinite(inv)) break;
        long ai = (long)floor(inv);
        long h2 = ai * h_curr + h_prev;
        long k2 = ai * k_curr + k_prev;
        if (k2 <= 0 || k2 > pmax) break;
        if (fabs((double)h2 / (double)k2 - x) <= tol)
        {
            *q = h2;
            *p = k2;
            return 0;
        }
        h_prev = h_curr;
        h_curr = h2;
        k_prev = k_curr;
        k_curr = k2;
        frac = inv - (double)ai;
    }
    return -1;
}

/* Format a single variable value.
 * - Integers are shown as plain integers.
 * - Terminating decimals are shown with 5 decimal places.
 * - Non-terminating decimals are shown as a simplest fraction plus a
 *   5-decimal approximation, e.g. "1/3 (nearly: 0.33333)". */
static void _fmt_val(char *buf, size_t bsz, float v)
{
    if (fabsf(v) < 5e-4f)
    {
        snprintf(buf, bsz, "0");
        return;
    }

    bool neg = (v < 0.0f);
    double x = fabs((double)v);

    long qi, pi;
    if (_double_to_frac(x, 10000, 1e-4, &qi, &pi) == 0)
    {
        long g = _gcd_long(qi, pi);
        if (g != 0)
        {
            qi /= g;
            pi /= g;
        }

        if (pi == 1)
        {
            snprintf(buf, bsz, "%s%ld", neg ? "-" : "", qi);
            return;
        }
        if (_is_terminating(pi))
        {
            snprintf(buf, bsz, "%.5f", (double)v);
            return;
        }
        snprintf(buf, bsz, "%s%ld/%ld (nearly: %.5f)",
                 neg ? "-" : "", qi, pi, (double)v);
        return;
    }

    /* Fallback: plain 5-decimal value. */
    snprintf(buf, bsz, "%.5f", (double)v);
}

/* Format a positive coefficient used in front of a symbolic term, e.g. the
 * "1/2" of "1/2z". Prefers integers and simple fractions over long decimals. */
static void _fmt_coef(char *buf, size_t bsz, float v)
{
    double x = fabs((double)v);
    long qi, pi;
    if (_double_to_frac(x, 10000, 5e-5, &qi, &pi) == 0)
    {
        long g = _gcd_long(qi, pi);
        if (g != 0)
        {
            qi /= g;
            pi /= g;
        }
        if (pi == 1)
        {
            snprintf(buf, bsz, "%ld", qi);
            return;
        }
        if (pi <= 1000)
        {
            snprintf(buf, bsz, "%ld/%ld", qi, pi);
            return;
        }
    }
    snprintf(buf, bsz, "%.4g", x);
}

/* ---------------- affine expressions (constant + symbolic terms) ------------
 * A solution component is not always a number: when the system is solved "with
 * respect to" only some of its letters, or when it is underdetermined, an
 * unknown is an affine expression of the remaining symbols (parameters and
 * free unknowns), e.g. x = 3 - 1/2z.                                        */

typedef struct
{
    float c;         /* constant term */
    float k[MAXS];   /* coefficient of letters[j] */
} aff_t;

static void _aff_zero(aff_t *a)
{
    memset(a, 0, sizeof(*a));
}

/* a -= s * b */
static void _aff_msub(aff_t *a, float s, const aff_t *b)
{
    a->c -= s * b->c;
    for (int j = 0; j < MAXS; j++)
    {
        a->k[j] -= s * b->k[j];
    }
}

/* Render "c + k0*l0 + k1*l1 ..." into buf, skipping zero terms.
 * A pure number keeps the usual value formatting (integers, fractions);
 * as soon as symbols are involved every coefficient is rendered in the same
 * compact style so "3 - 1/2z" does not come out as "3.00000 - 0.50000z". */
static void _fmt_affine(char *buf, size_t bsz, const aff_t *a,
                        const char letters[], int ns)
{
    size_t off = 0;
    buf[0] = '\0';
    bool first = true;

    int nterms = 0;
    for (int j = 0; j < ns; j++)
    {
        if (fabsf(a->k[j]) > 5e-5f)
        {
            nterms++;
        }
    }

    if (fabsf(a->c) > 5e-5f)
    {
        char cb[64];
        if (nterms == 0)
        {
            _fmt_val(cb, sizeof(cb), a->c);
        }
        else
        {
            char mag[48];
            _fmt_coef(mag, sizeof(mag), a->c);
            snprintf(cb, sizeof(cb), "%s%s", (a->c < 0.0f) ? "-" : "", mag);
        }
        _append(buf, &off, bsz, cb);
        first = false;
    }
    for (int j = 0; j < ns; j++)
    {
        float co = a->k[j];
        if (fabsf(co) < 5e-5f)
        {
            continue;
        }
        char cb[48];
        cb[0] = '\0';
        bool frac = false;
        if (fabsf(fabsf(co) - 1.0f) > 1e-5f)
        {
            _fmt_coef(cb, sizeof(cb), co);
            /* "1/2*z" instead of "1/2z" so the fraction is not read as 1/(2z) */
            frac = (strchr(cb, '/') != NULL);
        }
        if (first)
        {
            _append_fmt(buf, &off, bsz, "%s%s%s%c",
                        (co < 0.0f) ? "-" : "", cb, frac ? "*" : "", letters[j]);
            first = false;
        }
        else
        {
            _append_fmt(buf, &off, bsz, " %s %s%s%c",
                        (co < 0.0f) ? "-" : "+", cb, frac ? "*" : "", letters[j]);
        }
    }
    if (first)
    {
        _append(buf, &off, bsz, "0");
    }
}

/* Solve m x m linear system (A|rhs) in place; A[i][m] becomes solution.
 * Returns 0 on success, -1 if singular. */
static int _solve_mxm(float A[MAXV][MAXV + 1], int m)
{
    for (int col = 0; col < m; col++)
    {
        int piv = col;
        float maxv = fabsf(A[col][col]);
        for (int r = col + 1; r < m; r++)
        {
            if (fabsf(A[r][col]) > maxv)
            {
                maxv = fabsf(A[r][col]);
                piv = r;
            }
        }
        if (maxv < 1e-6f)
        {
            return -1; /* singular */
        }
        if (piv != col)
        {
            for (int j = col; j <= m; j++)
            {
                float t = A[col][j];
                A[col][j] = A[piv][j];
                A[piv][j] = t;
            }
        }
        float pv = A[col][col];
        for (int j = col; j <= m; j++)
        {
            A[col][j] /= pv;
        }
        for (int r = 0; r < m; r++)
        {
            if (r == col)
            {
                continue;
            }
            float f = A[r][col];
            if (f != 0.0f)
            {
                for (int j = col; j <= m; j++)
                {
                    A[r][j] -= f * A[col][j];
                }
            }
        }
    }
    return 0;
}

/* ---------------- linear solver (Gauss-Jordan + rank + parameters) ----------
 *
 * Handles the whole linear family in one place:
 *   - square systems        -> unique numeric solution
 *   - overdetermined        -> least-structure consistency check (rank based)
 *   - underdetermined       -> general solution, free unknowns reported
 *   - "solve for" subset    -> unknowns expressed through the other letters
 *
 * The augmented matrix is [ unknowns | parameters | rhs ]: the first nu
 * columns are the letters we solve for, the remaining ns-nu columns are
 * symbolic parameters. Pivoting spans every column, unknowns first, so a
 * parameter that is pinned by an extra equation (e.g. "a = 5") is solved too
 * and folded into the unknowns' expressions instead of being printed.
 */
static int _solve_linear_general(eq_node_t *asts[], int n, const char letters[],
                                 int ns, int nu, char *out, size_t cap)
{
    /* Augmented matrix [A | P | -c], A/P are Jacobians (constant), c = F(0). */
    float M[MAXE][MAXS + 1];
    memset(M, 0, sizeof(M));

    float v0[26];
    memset(v0, 0, sizeof(v0));
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < ns; j++)
        {
            /* Step of ~1 (not a tiny 1e-3): a central difference of a *linear*
             * function is exact for any step, and a small step makes
             * f(x+h)-f(x-h) suffer catastrophic cancellation in float32 (the
             * difference is ~1e-3 while |f| can be ~1e2), which shows up as
             * solutions like 3499/350 instead of 10. */
            int li = (int)(letters[j] - 'a');
            float h = fmaxf(1.0f, fabsf(v0[li]));
            float saved = v0[li];
            v0[li] = saved + h;
            float fp = eq_eval(asts[i], v0);
            v0[li] = saved - h;
            float fm = eq_eval(asts[i], v0);
            v0[li] = saved;
            M[i][j] = (fp - fm) / (2.0f * h);
        }
        M[i][ns] = -eq_eval(asts[i], v0);
    }

    /* Gauss-Jordan elimination with partial pivoting. */
    int rank = 0;
    int pivot_col[MAXS];
    bool is_pivot[MAXS];
    memset(pivot_col, -1, sizeof(pivot_col));
    memset(is_pivot, 0, sizeof(is_pivot));
    for (int col = 0; col < ns; col++)
    {
        int piv = -1;
        float maxv = RES_TOL_LIN;
        for (int r = rank; r < n; r++)
        {
            if (fabsf(M[r][col]) > maxv)
            {
                maxv = fabsf(M[r][col]);
                piv = r;
            }
        }
        if (piv < 0)
        {
            continue; /* dependent column -> free / parameter */
        }
        if (piv != rank)
        {
            for (int j = 0; j <= ns; j++)
            {
                float t = M[rank][j];
                M[rank][j] = M[piv][j];
                M[piv][j] = t;
            }
        }
        float pv = M[rank][col];
        for (int j = col; j <= ns; j++)
        {
            M[rank][j] /= pv;
        }
        /* Eliminate from every row (not only the rows below) so that each
         * pivot column becomes a unit vector: back-substitution then turns
         * into a direct read of the rhs expression. */
        for (int r = 0; r < n; r++)
        {
            if (r == rank || M[r][col] == 0.0f)
            {
                continue;
            }
            float f = M[r][col];
            for (int j = col; j <= ns; j++)
            {
                M[r][j] -= f * M[rank][j];
            }
        }
        pivot_col[rank] = col;
        is_pivot[col] = true;
        rank++;
    }

    /* Consistency: a row with no pivot left and a nonzero rhs is impossible.
     * (If a row only constrains parameters, the parameter was pivoted above
     * and folded into the other rows, so it never reaches here.) */
    bool inconsistent = false;
    for (int r = rank; r < n; r++)
    {
        bool allzero = true;
        for (int j = 0; j < ns; j++)
        {
            if (fabsf(M[r][j]) > RES_TOL_LIN)
            {
                allzero = false;
                break;
            }
        }
        if (allzero && fabsf(M[r][ns]) > RES_TOL_LIN)
        {
            inconsistent = true;
            break;
        }
    }

    size_t off = 0;
    out[0] = '\0';
    if (inconsistent)
    {
        _append(out, &off, cap, "No solution");
        return 0;
    }

    /* Build the expression of each pivot column: start from the rhs and move
     * every remaining term to the left-hand side of the "= ". */
    aff_t expr[MAXS];
    for (int j = 0; j < MAXS; j++)
    {
        _aff_zero(&expr[j]);
    }
    for (int i = rank - 1; i >= 0; i--)
    {
        int pc = pivot_col[i];
        aff_t e;
        _aff_zero(&e);
        e.c = M[i][ns];
        for (int j = 0; j < ns; j++)
        {
            if (j == pc || M[i][j] == 0.0f)
            {
                continue;
            }
            if (is_pivot[j])
            {
                _aff_msub(&e, M[i][j], &expr[j]);
            }
            else
            {
                e.k[j] -= M[i][j]; /* free unknown or parameter: stays symbolic */
            }
        }
        expr[pc] = e;
    }

    char vbuf[96];
    for (int j = 0; j < nu; j++)
    {
        if (!is_pivot[j])
        {
            continue;
        }
        _fmt_affine(vbuf, sizeof(vbuf), &expr[j], letters, ns);
        _append_fmt(out, &off, cap, "%c = %s\n", letters[j], vbuf);
    }
    for (int j = 0; j < nu; j++)
    {
        if (is_pivot[j])
        {
            continue;
        }
        _append_fmt(out, &off, cap, "%c is free\n", letters[j]);
    }
    if (off > 0)
    {
        out[off - 1] = '\0'; /* drop the trailing newline */
    }
    else
    {
        _append(out, &off, cap, "Identity (always true)");
    }
    return 0;
}

/* ---------------- non-linear solver (multi-start damped Gauss-Newton) -------- */

/* Forward declaration (defined later). */
static float _res_norm_single(eq_node_t *asts[], int n, const char letters[], int m, const float x[], int i);

static uint32_t g_rng;
static float _rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return ((float)(g_rng & 0xffffff) / (float)(1 << 24)) * 2.0f - 1.0f; /* [-1,1] */
}

/* Residual using letter-indexed varvals (fills internal). */
static void _fill_varvals(const char letters[], int m, const float x[], float vv[26])
{
    memset(vv, 0, sizeof(float) * 26);
    for (int j = 0; j < m; j++)
    {
        vv[(int)(letters[j] - 'a')] = x[j];
    }
}

static float _res_norm(eq_node_t *asts[], int n, const char letters[], int m, const float x[])
{
    float vv[26];
    _fill_varvals(letters, m, x, vv);
    float s = 0.0f;
    for (int i = 0; i < n; i++)
    {
        float ri = eq_eval(asts[i], vv);
        if (!isfinite(ri))
        {
            return INFINITY;
        }
        s += ri * ri;
    }
    return sqrtf(s);
}

/* Relative rank test for a small square matrix: eliminates with partial
 * pivoting and reports a (near-)singular matrix when the smallest pivot is
 * negligible compared with the largest one. A relative test is required - an
 * absolute 1e-6 threshold misses the ~1e-3 pivots produced by J^T*J of an
 * exactly dependent system. */
static bool _near_singular(float A[MAXV][MAXV], int m)
{
    float pmax = 0.0f;
    for (int i = 0; i < m; i++)
    {
        for (int j = 0; j < m; j++)
        {
            if (fabsf(A[i][j]) > pmax)
            {
                pmax = fabsf(A[i][j]);
            }
        }
    }
    if (pmax < 1e-12f)
    {
        return true;
    }
    for (int col = 0; col < m; col++)
    {
        int piv = col;
        float mx = fabsf(A[col][col]);
        for (int r = col + 1; r < m; r++)
        {
            if (fabsf(A[r][col]) > mx)
            {
                mx = fabsf(A[r][col]);
                piv = r;
            }
        }
        if (mx < 1e-4f * pmax)
        {
            return true;
        }
        if (piv != col)
        {
            for (int j = 0; j < m; j++)
            {
                float t = A[col][j];
                A[col][j] = A[piv][j];
                A[piv][j] = t;
            }
        }
        for (int r = col + 1; r < m; r++)
        {
            float f = A[r][col] / A[col][col];
            if (f == 0.0f)
            {
                continue;
            }
            for (int j = col; j < m; j++)
            {
                A[r][j] -= f * A[col][j];
            }
        }
    }
    return false;
}

/* True when the normal-equation matrix J^T*J is singular at x. A singular
 * Jacobian means the equations are locally dependent at that point, i.e. the
 * solution set is a continuum (e.g. "x/y = 2" and "x = 2y"), not a set of
 * isolated roots. Reporting those as individual roots is misleading. */
static bool _jacobian_singular(eq_node_t *asts[], int n, const char letters[], int m,
                              const float x[])
{
    float xc[MAXV];
    float J[MAXE][MAXV];
    float JTJ[MAXV][MAXV];
    memcpy(xc, x, sizeof(float) * (size_t)m);

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            float h = 1e-3f * fmaxf(1.0f, fabsf(xc[j]));
            float saved = xc[j];
            xc[j] = saved + h;
            float fp = _res_norm_single(asts, n, letters, m, xc, i);
            xc[j] = saved - h;
            float fm = _res_norm_single(asts, n, letters, m, xc, i);
            xc[j] = saved;
            J[i][j] = (fp - fm) / (2.0f * h);
        }
    }
    memset(JTJ, 0, sizeof(JTJ));
    for (int a = 0; a < m; a++)
    {
        for (int b = 0; b < m; b++)
        {
            float s = 0.0f;
            for (int k = 0; k < n; k++)
            {
                s += J[k][a] * J[k][b];
            }
            JTJ[a][b] = s;
        }
    }
    return _near_singular(JTJ, m);
}

static int _solve_nonlinear(eq_node_t *asts[], int n, const char letters[], int m,
                            char *out, size_t cap)
{
    if (n < m)
    {
        size_t off = 0;
        out[0] = '\0';
        _append_fmt(out, &off, cap,
                    "Infinite solutions\n(underdetermined: %d eqs < %d vars)", n, m);
        return 0;
    }

    g_rng = 0x12345678u ^ ((uint32_t)n * 2654435761u) ^ ((uint32_t)m * 40503u);

    float found[MAXV][MAXV]; /* up to MAXV solutions */
    int nfound = 0;
    bool degenerate = false; /* a continuum of solutions was reached */
    const int STARTS = 14;
    const int ITERS = 90;

    for (int s = 0; s < STARTS && nfound < MAXV; s++)
    {
        float x[MAXV];
        for (int j = 0; j < m; j++)
        {
            x[j] = _rnd() * 3.0f;
        }

        float lambda = 1e-2f;
        for (int it = 0; it < ITERS; it++)
        {
            float rn = _res_norm(asts, n, letters, m, x);
            if (!isfinite(rn))
            {
                break;
            }
            /* Converged? Keep iterating well past the acceptance threshold:
             * Gauss-Newton is quadratic near a root, so polishing down to
             * ~1e-9 costs a couple of extra steps and is what makes the value
             * print as "3" instead of "29603/9868". */
            if (rn < RES_TOL_POLISH)
            {
                break;
            }

            /* Build Jacobian J[n][m] and residual r[n]. */
            float J[MAXE][MAXV];
            float r[MAXE];
            float vv[26];
            for (int i = 0; i < n; i++)
            {
                _fill_varvals(letters, m, x, vv);
                r[i] = eq_eval(asts[i], vv);
                for (int j = 0; j < m; j++)
                {
                    float h = 1e-3f * fmaxf(1.0f, fabsf(x[j]));
                    float saved = x[j];
                    x[j] = saved + h;
                    float fp = _res_norm_single(asts, n, letters, m, x, i);
                    x[j] = saved - h;
                    float fm = _res_norm_single(asts, n, letters, m, x, i);
                    x[j] = saved;
                    J[i][j] = (fp - fm) / (2.0f * h);
                }
            }

            /* JTJ[m][m] and JTr[m]. */
            float JTJ[MAXV][MAXV + 1];
            memset(JTJ, 0, sizeof(JTJ));
            for (int a = 0; a < m; a++)
            {
                for (int b = 0; b < m; b++)
                {
                    float s2 = 0.0f;
                    for (int k = 0; k < n; k++)
                    {
                        s2 += J[k][a] * J[k][b];
                    }
                    JTJ[a][b] = s2;
                }
                float s3 = 0.0f;
                for (int k = 0; k < n; k++)
                {
                    s3 += J[k][a] * r[k];
                }
                JTJ[a][m] = -s3;
            }
            for (int a = 0; a < m; a++)
            {
                JTJ[a][a] += lambda;
            }

            if (_solve_mxm(JTJ, m) != 0)
            {
                break; /* singular */
            }
            float delta[MAXV];
            for (int a = 0; a < m; a++)
            {
                delta[a] = JTJ[a][m];
            }

            float xnew[MAXV];
            for (int a = 0; a < m; a++)
            {
                xnew[a] = x[a] + delta[a];
            }
            float rn_new = _res_norm(asts, n, letters, m, xnew);
            if (!isfinite(rn_new))
            {
                lambda *= 2.0f;
                if (lambda > 1e3f)
                {
                    break;
                }
                continue;
            }
            if (rn_new < rn)
            {
                float step = 0.0f;
                for (int a = 0; a < m; a++)
                {
                    if (fabsf(delta[a]) > step)
                    {
                        step = fabsf(delta[a]);
                    }
                    x[a] = xnew[a];
                }
                lambda *= 0.5f;
                if (lambda < 1e-6f)
                {
                    lambda = 1e-6f;
                }
                /* Stagnating at minimal damping: no further progress possible,
                 * stop instead of burning the remaining iterations. */
                if (step < 1e-7f)
                {
                    break;
                }
            }
            else
            {
                lambda *= 2.0f;
                if (lambda > 1e3f)
                {
                    break;
                }
            }
        }

        float rn = _res_norm(asts, n, letters, m, x);
        if (isfinite(rn) && rn < (n == m ? RES_TOL_SQ : RES_TOL_OVER))
        {
            /* Dependent equations describe a curve/surface, not isolated roots:
             * keep them apart from genuine solutions. */
            if (_jacobian_singular(asts, n, letters, m, x))
            {
                degenerate = true;
                continue;
            }
            /* dedupe */
            bool dup = false;
            for (int f = 0; f < nfound; f++)
            {
                bool same = true;
                for (int j = 0; j < m; j++)
                {
                    if (fabsf(found[f][j] - x[j]) > SOLVE_TOL)
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                {
                    dup = true;
                    break;
                }
            }
            if (!dup)
            {
                for (int j = 0; j < m; j++)
                {
                    found[nfound][j] = x[j];
                }
                nfound++;
            }
        }
    }

    size_t off = 0;
    out[0] = '\0';
    if (nfound == 0)
    {
        if (degenerate)
        {
            _append(out, &off, cap, "Infinite solutions\n(dependent equations)");
        }
        else
        {
            _append(out, &off, cap, "No solution");
        }
        return 0;
    }
    if (nfound == 1)
    {
        char vbuf[64];
        for (int j = 0; j < m; j++)
        {
            _fmt_val(vbuf, sizeof(vbuf), found[0][j]);
            _append_fmt(out, &off, cap, "%c = %s", letters[j], vbuf);
            if (j + 1 < m)
            {
                _append(out, &off, cap, "\n");
            }
        }
        return 0;
    }
    for (int f = 0; f < nfound; f++)
    {
        _append_fmt(out, &off, cap, "Solution %d:\n", f + 1);
        char vbuf[64];
        for (int j = 0; j < m; j++)
        {
            _fmt_val(vbuf, sizeof(vbuf), found[f][j]);
            _append_fmt(out, &off, cap, "%c = %s", letters[j], vbuf);
            if (j + 1 < m)
            {
                _append(out, &off, cap, ", ");
            }
        }
        if (f + 1 < nfound)
        {
            _append(out, &off, cap, "\n");
        }
    }
    return 0;
}

/* helper used by Jacobian: residual of equation i at current x */
static float _res_norm_single(eq_node_t *asts[], int n, const char letters[], int m, const float x[], int i)
{
    (void)n;
    float vv[26];
    _fill_varvals(letters, m, x, vv);
    return eq_eval(asts[i], vv);
}

/* ---------------- linearity check + top entry ---------------- */

/* True when every equation is affine in ALL of its letters (unknowns and
 * symbolic parameters alike): the Jacobian must be the same at two different
 * points. Compared entry-by-entry instead of materialising both Jacobians,
 * which keeps the stack usage small even with MAXS symbols. */
static bool _is_linear(eq_node_t *asts[], int n, const char letters[], int ns)
{
    float base1[26];
    float base2[26];
    memset(base1, 0, sizeof(base1));
    memset(base2, 0, sizeof(base2));
    for (int j = 0; j < ns; j++)
    {
        base1[(int)(letters[j] - 'a')] = 1.0f;
        base2[(int)(letters[j] - 'a')] = -2.0f;
    }
    for (int i = 0; i < n; i++)
    {
        eq_node_t *e = asts[i];
        if (e->kind == EQ_NODE_REL)
        {
            e = e->a; /* unwrap relation to the expression LHS - RHS */
        }
        for (int j = 0; j < ns; j++)
        {
            int li = (int)(letters[j] - 'a');

            float h1 = 1e-3f * fmaxf(1.0f, fabsf(base1[li]));
            float saved = base1[li];
            base1[li] = saved + h1;
            float fp = eq_eval(e, base1);
            base1[li] = saved - h1;
            float fm = eq_eval(e, base1);
            base1[li] = saved;
            float j1 = (fp - fm) / (2.0f * h1);

            float h2 = 1e-3f * fmaxf(1.0f, fabsf(base2[li]));
            float sv2 = base2[li];
            base2[li] = sv2 + h2;
            float fp2 = eq_eval(e, base2);
            base2[li] = sv2 - h2;
            float fm2 = eq_eval(e, base2);
            base2[li] = sv2;
            float j2 = (fp2 - fm2) / (2.0f * h2);

            if (fabsf(j1 - j2) > 1e-2f * (1.0f + fabsf(j1)))
            {
                return false;
            }
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Univariate (single variable, single equation/inequality) solvers.
 *  - equations: complete set of real roots
 *  - inequalities: interval solution set
 * ------------------------------------------------------------------------- */

#define EQSOLVER_POLY_MAX_DEG 32

typedef struct
{
    double c[EQSOLVER_POLY_MAX_DEG + 1];
    int deg;
    bool ok;
} _poly_t;

static void _poly_zero(_poly_t *p)
{
    memset(p, 0, sizeof(*p));
    p->ok = true;
    p->deg = 0;
}
static void _poly_trim(_poly_t *p)
{
    int d = EQSOLVER_POLY_MAX_DEG;
    while (d > 0 && fabs(p->c[d]) < 1e-300)
    {
        d--;
    }
    p->deg = d;
}
static void _poly_const(_poly_t *p, double v)
{
    _poly_zero(p);
    p->c[0] = v;
}
static void _poly_x(_poly_t *p)
{
    _poly_zero(p);
    p->c[1] = 1.0;
    p->deg = 1;
}
static void _poly_neg(_poly_t *p)
{
    for (int i = 0; i <= p->deg; i++)
    {
        p->c[i] = -p->c[i];
    }
}
static void _poly_add(const _poly_t *a, const _poly_t *b, _poly_t *out)
{
    _poly_t r;
    _poly_zero(&r);
    int d = (a->deg > b->deg) ? a->deg : b->deg;
    for (int i = 0; i <= d; i++)
    {
        r.c[i] = a->c[i] + b->c[i];
    }
    r.deg = d;
    _poly_trim(&r);
    *out = r;
}
static void _poly_sub(const _poly_t *a, const _poly_t *b, _poly_t *out)
{
    _poly_t r;
    _poly_zero(&r);
    int d = (a->deg > b->deg) ? a->deg : b->deg;
    for (int i = 0; i <= d; i++)
    {
        r.c[i] = a->c[i] - b->c[i];
    }
    r.deg = d;
    _poly_trim(&r);
    *out = r;
}
static void _poly_mul(const _poly_t *a, const _poly_t *b, _poly_t *out)
{
    _poly_t r;
    _poly_zero(&r);
    if (a->deg + b->deg > EQSOLVER_POLY_MAX_DEG)
    {
        r.ok = false;
        *out = r;
        return;
    }
    for (int i = 0; i <= a->deg; i++)
    {
        for (int j = 0; j <= b->deg; j++)
        {
            r.c[i + j] += a->c[i] * b->c[j];
        }
    }
    r.deg = a->deg + b->deg;
    _poly_trim(&r);
    *out = r;
}
static void _poly_scale(_poly_t *p, double s)
{
    for (int i = 0; i <= p->deg; i++)
    {
        p->c[i] *= s;
    }
}
static void _extract_poly(eq_node_t *n, char var, _poly_t *out)
{
    _poly_zero(out);
    if (!n)
    {
        out->ok = false;
        return;
    }
    switch (n->kind)
    {
        case EQ_NODE_NUM:
            _poly_const(out, (double)n->value);
            break;
        case EQ_NODE_VAR:
            if (n->var == var)
            {
                _poly_x(out);
            }
            else
            {
                out->ok = false;
            }
            break;
        case EQ_NODE_NEG:
            _extract_poly(n->a, var, out);
            if (out->ok)
            {
                _poly_neg(out);
            }
            break;
        case EQ_NODE_ADD:
        {
            _poly_t a, b;
            _extract_poly(n->a, var, &a);
            _extract_poly(n->b, var, &b);
            if (a.ok && b.ok)
            {
                _poly_add(&a, &b, out);
            }
            else
            {
                out->ok = false;
            }
            break;
        }
        case EQ_NODE_SUB:
        {
            _poly_t a, b;
            _extract_poly(n->a, var, &a);
            _extract_poly(n->b, var, &b);
            if (a.ok && b.ok)
            {
                _poly_sub(&a, &b, out);
            }
            else
            {
                out->ok = false;
            }
            break;
        }
        case EQ_NODE_MUL:
        {
            _poly_t a, b;
            _extract_poly(n->a, var, &a);
            _extract_poly(n->b, var, &b);
            if (a.ok && b.ok)
            {
                _poly_mul(&a, &b, out);
            }
            else
            {
                out->ok = false;
            }
            break;
        }
        case EQ_NODE_DIV:
        {
            _poly_t a, b;
            _extract_poly(n->a, var, &a);
            _extract_poly(n->b, var, &b);
            if (!a.ok || !b.ok)
            {
                out->ok = false;
                break;
            }
            if (b.deg == 0)
            {
                _poly_scale(&a, 1.0 / b.c[0]);
                *out = a;
            }
            else
            {
                out->ok = false; /* rational -> fall back to bounded scan */
            }
            break;
        }
        case EQ_NODE_POW:
        {
            _poly_t a;
            _extract_poly(n->a, var, &a);
            if (!a.ok)
            {
                out->ok = false;
                break;
            }
            if (n->b->kind == EQ_NODE_NUM)
            {
                double dv = (double)n->b->value;
                long k = (long)floor(dv + 0.5);
                if (fabs(dv - (double)k) > 1e-9 || k < 0)
                {
                    out->ok = false;
                    break;
                }
                _poly_t r;
                _poly_const(&r, 1.0);
                for (long i = 0; i < k; i++)
                {
                    _poly_t t;
                    _poly_mul(&r, &a, &t);
                    r = t;
                    if (!r.ok)
                    {
                        break;
                    }
                }
                *out = r;
            }
            else
            {
                out->ok = false;
            }
            break;
        }
        default:
            out->ok = false;
            break;
    }
}
static double _poly_bound(const _poly_t *p)
{
    double maxc = 0.0;
    for (int i = 0; i < p->deg; i++)
    {
        maxc = fmax(maxc, fabs(p->c[i]));
    }
    double an = fabs(p->c[p->deg]);
    double B = 1.0 + maxc / (an > 1e-12 ? an : 1e-12);
    if (!isfinite(B) || B < 1.0)
    {
        B = 1.0;
    }
    return B;
}

static float _eval_uni(eq_node_t *expr, char var, double x)
{
    float vv[26];
    memset(vv, 0, sizeof(vv));
    vv[(int)(var - 'a')] = (float)x;
    return eq_eval(expr, vv);
}

static void _cat(char *out, size_t *off, size_t cap, const char *s)
{
    if (*off >= cap)
    {
        return;
    }
    int n = snprintf(out + *off, cap - *off, "%s", s);
    if (n > 0)
    {
        *off += (size_t)n;
    }
}
static void _catf(char *out, size_t *off, size_t cap, const char *fmt, ...)
{
    if (*off >= cap)
    {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        *off += (size_t)n;
    }
}

static void _fmt_num(double v, char *buf, size_t bsz)
{
    if (!isfinite(v))
    {
        snprintf(buf, bsz, "?");
        return;
    }
    if (fabs(v) < 1e-9)
    {
        snprintf(buf, bsz, "0");
        return;
    }
    double r = floor(v + 0.5);
    if (fabs(v - r) < 1e-6)
    {
        snprintf(buf, bsz, "%.0f", r);
        return;
    }
    snprintf(buf, bsz, "%.6g", v);
}

static void _add_root(double *roots, int *nr, int maxr, double r)
{
    if (!isfinite(r))
    {
        return;
    }
    for (int i = 0; i < *nr; i++)
    {
        if (fabs(roots[i] - r) < 1e-5)
        {
            return;
        }
    }
    if (*nr < maxr)
    {
        roots[(*nr)++] = r;
    }
}

static double _bisect(eq_node_t *expr, char var, double a, double b)
{
    double fa = (double)_eval_uni(expr, var, a);
    for (int i = 0; i < 200; i++)
    {
        double m = (a + b) * 0.5;
        double fm = (double)_eval_uni(expr, var, m);
        if (fabs(b - a) < 1e-9 || fabs(fm) < 1e-12)
        {
            return m;
        }
        if (fa * fm < 0.0)
        {
            b = m;
        }
        else
        {
            a = m;
            fa = fm;
        }
    }
    return (a + b) * 0.5;
}

/* Newton polish: drives a candidate root to full precision (also converges
   linearly for even-multiplicity roots). */
static double _newton_polish(eq_node_t *expr, char var, double x)
{
    for (int i = 0; i < 40; i++)
    {
        double f = (double)_eval_uni(expr, var, x);
        if (fabs(f) < 1e-11)
        {
            return x;
        }
        double h = (fabs(x) > 1.0) ? 1e-7 * fabs(x) : 1e-7;
        double fp = ((double)_eval_uni(expr, var, x + h) -
                     (double)_eval_uni(expr, var, x - h)) / (2.0 * h);
        if (fabs(fp) < 1e-12)
        {
            break;
        }
        double xn = x - f / fp;
        if (!isfinite(xn))
        {
            break;
        }
        if (fabs(xn - x) < 1e-12)
        {
            x = xn;
            break;
        }
        x = xn;
    }
    return x;
}

/* Locate x in [a,b] minimising |f(x)| (even-multiplicity roots). */
static double _min_abs(eq_node_t *expr, char var, double a, double b)
{
    const double g = 0.61803398875;
    double x0 = a, x3 = b;
    double x1 = x3 - g * (x3 - x0);
    double x2 = x0 + g * (x3 - x0);
    double f1 = fabs((double)_eval_uni(expr, var, x1));
    double f2 = fabs((double)_eval_uni(expr, var, x2));
    for (int i = 0; i < 100; i++)
    {
        if (fabs(x3 - x0) < 1e-11)
        {
            break;
        }
        if (f1 < f2)
        {
            x3 = x2;
            x2 = x1;
            x1 = x3 - g * (x3 - x0);
            f2 = f1;
            f1 = fabs((double)_eval_uni(expr, var, x1));
        }
        else
        {
            x0 = x1;
            x1 = x2;
            x2 = x0 + g * (x3 - x0);
            f1 = f2;
            f2 = fabs((double)_eval_uni(expr, var, x2));
        }
    }
    return (x0 + x3) * 0.5;
}

/* Find all real roots of f(x)=0 in [lo,hi]. Returns count (<= maxr). */
static int _find_real_roots(eq_node_t *expr, char var, double lo, double hi,
                            double *roots, int maxr)
{
    int nr = 0;
    double span = hi - lo;
    if (span <= 0.0)
    {
        return 0;
    }
    int N = (int)(span / 0.0025);
    if (N < 400)
    {
        N = 400;
    }
    if (N > 40000)
    {
        N = 40000;
    }
    double dx = span / (double)N;

    double fp2 = (double)_eval_uni(expr, var, lo);
    double fp1 = (double)_eval_uni(expr, var, lo + dx);
    double xp2 = lo, xp1 = lo + dx;

    if (fabs(fp2) < 1e-9)
    {
        _add_root(roots, &nr, maxr, _newton_polish(expr, var, lo));
    }

    for (int k = 2; k <= N; k++)
    {
        double x = lo + (double)k * dx;
        double f = (double)_eval_uni(expr, var, x);

        if (fp1 * f < 0.0 && fabs(fp1) > 1e-6 && fabs(f) > 1e-6)
        {
            double r = _newton_polish(expr, var, _bisect(expr, var, xp1, x));
            _add_root(roots, &nr, maxr, r);
        }
        if (k <= N - 1)
        {
            double a2 = fabs(fp2), a1 = fabs(fp1), a0 = fabs(f);
            if (a1 < a2 && a1 < a0 && a1 < 0.05)
            {
                double r = _newton_polish(expr, var, _min_abs(expr, var, xp2, x));
                if (fabs((double)_eval_uni(expr, var, r)) < 1e-5)
                {
                    _add_root(roots, &nr, maxr, r);
                }
            }
        }
        fp2 = fp1;
        fp1 = f;
        xp2 = xp1;
        xp1 = x;
    }
    return nr;
}

static bool _rel_sat(double v, char relop)
{
    switch (relop)
    {
        case '>': return v > 0.0;
        case '<': return v < 0.0;
        case 'G': return v >= 0.0;
        case 'L': return v <= 0.0;
        default:  return fabs(v) < 1e-9; /* '=' */
    }
}

static void _append_interval(char *out, size_t *off, size_t cap, char var,
                             double a, double b, bool aOpen, bool bOpen)
{
    char sa[64], sb[64];
    bool aInf = (a <= -1e29), bInf = (b >= 1e29);
    if (aInf) sa[0] = '\0'; else _fmt_num(a, sa, sizeof(sa));
    if (bInf) sb[0] = '\0'; else _fmt_num(b, sb, sizeof(sb));

    if (aInf && bInf)
    {
        _cat(out, off, cap, "All real numbers");
        return;
    }
    if (aInf)
    {
        _catf(out, off, cap, "%c %s %s", var, bOpen ? "<" : "≤", sb);
        return;
    }
    if (bInf)
    {
        _catf(out, off, cap, "%c %s %s", var, aOpen ? ">" : "≥", sa);
        return;
    }
    if (!aOpen && !bOpen)
    {
        if (fabs(a - b) < 1e-9)
        {
            _catf(out, off, cap, "%c = %s", var, sa);
        }
        else
        {
            _catf(out, off, cap, "%s ≤ %c ≤ %s", sa, var, sb);
        }
    }
    else if (aOpen && bOpen)
    {
        _catf(out, off, cap, "%s < %c < %s", sa, var, sb);
    }
    else if (!aOpen && bOpen)
    {
        _catf(out, off, cap, "%s ≤ %c < %s", sa, var, sb);
    }
    else
    {
        _catf(out, off, cap, "%s < %c ≤ %s", sa, var, sb);
    }
}

static int _solve_univariate_eq(eq_node_t *root, char var, char *out, size_t cap)
{
    eq_node_t *expr = (root->kind == EQ_NODE_REL) ? root->a : root;

    _poly_t p;
    _extract_poly(expr, var, &p);

    double lo, hi;
    bool used_bound = false;
    if (p.ok && p.deg >= 1)
    {
        double B = _poly_bound(&p);
        if (B > 1e4)
        {
            B = 1e4;
        }
        lo = -B - 1.0;
        hi = B + 1.0;
        used_bound = true;
    }
    else
    {
        lo = -50.0;
        hi = 50.0;
    }

    size_t off = 0;
    out[0] = '\0';

    if (p.ok && p.deg == 0)
    {
        if (fabs(p.c[0]) < 1e-9)
        {
            _cat(out, &off, cap, "Identity: all real numbers");
        }
        else
        {
            _cat(out, &off, cap, "No solution");
        }
        return 0;
    }

    double roots[64];
    int nr = _find_real_roots(expr, var, lo, hi, roots, 64);
    for (int i = 0; i < nr; i++)
    {
        for (int j = i + 1; j < nr; j++)
        {
            if (roots[j] < roots[i])
            {
                double t = roots[i];
                roots[i] = roots[j];
                roots[j] = t;
            }
        }
    }

    if (nr == 0)
    {
        _cat(out, &off, cap, "No real root");
        return 0;
    }

    _catf(out, &off, cap, "%c = ", var);
    for (int i = 0; i < nr; i++)
    {
        char buf[64];
        _fmt_num(roots[i], buf, sizeof(buf));
        _cat(out, &off, cap, buf);
        if (i + 1 < nr)
        {
            _cat(out, &off, cap, ", ");
        }
    }
    if (!used_bound)
    {
        _cat(out, &off, cap, "\n(within search range -50..50)");
    }
    return 0;
}

static int _solve_univariate_ineq(eq_node_t *root, char var, char relop,
                                  char *out, size_t cap)
{
    eq_node_t *expr = root->a; /* LHS - RHS */

    _poly_t p;
    _extract_poly(expr, var, &p);

    double lo, hi;
    bool used_bound = false;
    if (p.ok && p.deg >= 1)
    {
        double B = _poly_bound(&p);
        if (B > 1e4) B = 1e4;
        lo = -B - 1.0;
        hi = B + 1.0;
        used_bound = true;
    }
    else
    {
        lo = -50.0;
        hi = 50.0;
    }

    size_t off = 0;
    out[0] = '\0';

    if (p.ok && p.deg == 0)
    {
        if (_rel_sat(p.c[0], relop))
        {
            _cat(out, &off, cap, "Identity: all real numbers");
        }
        else
        {
            _cat(out, &off, cap, "No solution");
        }
        return 0;
    }

    double roots[64];
    int R = _find_real_roots(expr, var, lo, hi, roots, 64);
    for (int i = 0; i < R; i++)
    {
        for (int j = i + 1; j < R; j++)
        {
            if (roots[j] < roots[i])
            {
                double t = roots[i];
                roots[i] = roots[j];
                roots[j] = t;
            }
        }
    }

    bool sat[65];
    for (int i = 0; i <= R; i++)
    {
        double a = (i == 0) ? -1e30 : roots[i - 1];
        double b = (i == R) ? 1e30 : roots[i];
        double test;
        if (a <= -1e29)
        {
            test = b - 1.0;
        }
        else if (b >= 1e29)
        {
            test = a + 1.0;
        }
        else
        {
            test = (a + b) * 0.5;
        }
        if (test <= a || test >= b)
        {
            test = (a + b) * 0.5;
        }
        double fv = (double)_eval_uni(expr, var, test);
        sat[i] = _rel_sat(fv, relop);
    }

    bool strict = (relop == '>' || relop == '<');
    int printed = 0;
    int i = 0;
    while (i <= R)
    {
        if (!sat[i])
        {
            i++;
            continue;
        }
        int j = i;
        if (!strict)
        {
            while (j + 1 <= R && sat[j + 1])
            {
                j++;
            }
        }
        double a = (i == 0) ? -1e30 : roots[i - 1];
        double b = (j == R) ? 1e30 : roots[j];
        bool aOpen = (i == 0) ? true : strict;
        bool bOpen = (j == R) ? true : strict;
        if (printed > 0)
        {
            _cat(out, &off, cap, " 或 ");
        }
        _append_interval(out, &off, cap, var, a, b, aOpen, bOpen);
        printed++;
        i = j + 1;
    }

    if (!strict)
    {
        for (int k = 0; k < R; k++)
        {
            bool leftSat = sat[k];
            bool rightSat = sat[k + 1];
            if (!leftSat && !rightSat)
            {
                char buf[64];
                _fmt_num(roots[k], buf, sizeof(buf));
                if (printed > 0)
                {
                    _cat(out, &off, cap, " 或 ");
                }
                _catf(out, &off, cap, "%c = %s", var, buf);
                printed++;
            }
        }
    }

    if (printed == 0)
    {
        _cat(out, &off, cap, "No solution");
    }
    else if (!used_bound)
    {
        _cat(out, &off, cap, "\n(within search range -50..50)");
    }
    return 0;
}

/* Parse a "solve for" list such as "x,y", "x y" or "xy" into letters[].
 * Order is preserved (it drives the result order), duplicates are dropped and
 * non-letter characters are treated as separators. Returns the letter count. */
static int _parse_vars_spec(const char *spec, char *letters, int max)
{
    int n = 0;
    if (!spec)
    {
        return 0;
    }
    for (const char *p = spec; *p != '\0' && n < max; p++)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        if (c < 'a' || c > 'z')
        {
            continue; /* ',', ' ', ';' ... */
        }
        bool dup = false;
        for (int i = 0; i < n; i++)
        {
            if (letters[i] == c)
            {
                dup = true;
                break;
            }
        }
        if (!dup)
        {
            letters[n++] = c;
        }
    }
    return n;
}

int cos_eqsolve_for(const char *eqs[], int n_eqs, const char *vars,
                    char *out, size_t out_size)
{
    out[0] = '\0';
    if (n_eqs <= 0)
    {
        snprintf(out, out_size, "No equations entered");
        return 0;
    }
    if (n_eqs > MAXE)
    {
        n_eqs = MAXE;
    }

    eq_node_t *asts[MAXE];
    memset(asts, 0, sizeof(asts));
    char perr[EQSOLVER_PARSE_ERR_SIZE];

    for (int i = 0; i < n_eqs; i++)
    {
        asts[i] = eq_parse(eqs[i], perr, sizeof(perr));
        if (!asts[i])
        {
            snprintf(out, out_size, "Parse error (eq %d):\n%s", i + 1, perr);
            for (int k = 0; k <= i; k++)
            {
                eq_ast_free(asts[k]);
            }
            return 0;
        }
    }

    /* Every letter appearing anywhere in the system (sorted alphabetically). */
    char all[MAXS];
    int nall = 0;
    memset(all, 0, sizeof(all));
    bool overflow = false;
    for (int i = 0; i < n_eqs; i++)
    {
        char tmp[26];
        int cnt = eq_collect_vars(asts[i], tmp, (int)sizeof(tmp));
        for (int a = 0; a < cnt; a++)
        {
            bool has = false;
            for (int b = 0; b < nall; b++)
            {
                if (all[b] == tmp[a])
                {
                    has = true;
                    break;
                }
            }
            if (has)
            {
                continue;
            }
            if (nall < MAXS)
            {
                all[nall++] = tmp[a];
            }
            else
            {
                overflow = true;
            }
        }
    }
    /* bubble sort */
    for (int a = 0; a < nall; a++)
    {
        for (int b = a + 1; b < nall; b++)
        {
            if (all[b] < all[a])
            {
                char t = all[a];
                all[a] = all[b];
                all[b] = t;
            }
        }
    }

    int ret = 0;
    if (nall == 0)
    {
        snprintf(out, out_size, "No variable found in equations");
        goto done;
    }
    if (overflow)
    {
        snprintf(out, out_size, "Too many symbols\n(max %d letters)", MAXS);
        goto done;
    }

    /* letters[] = unknowns first (in the requested order), then the letters
     * that stay symbolic parameters. */
    char letters[MAXS];
    int nu = 0;
    memset(letters, 0, sizeof(letters));

    if (vars && vars[0] != '\0')
    {
        char raw[26];
        nu = _parse_vars_spec(vars, raw, (int)sizeof(raw));
        if (nu == 0)
        {
            snprintf(out, out_size, "Invalid \"solve for\" list\n(use letters, e.g. x,y)");
            goto done;
        }
        if (nu > MAXU)
        {
            snprintf(out, out_size, "Too many unknowns\n(max %d)", MAXU);
            goto done;
        }
        memcpy(letters, raw, (size_t)nu);
        for (int j = 0; j < nu; j++)
        {
            bool present = false;
            for (int b = 0; b < nall; b++)
            {
                if (all[b] == letters[j])
                {
                    present = true;
                    break;
                }
            }
            if (!present)
            {
                snprintf(out, out_size, "Letter '%c' is not in\nthe equations", letters[j]);
                goto done;
            }
        }
        if (nall - nu > MAXP)
        {
            snprintf(out, out_size, "Too many other letters\n(max %d unknowns + %d params)",
                     MAXU, MAXP);
            goto done;
        }
        /* parameters keep alphabetical order */
        int np = nu;
        for (int b = 0; b < nall; b++)
        {
            bool is_unk = false;
            for (int j = 0; j < nu; j++)
            {
                if (letters[j] == all[b])
                {
                    is_unk = true;
                    break;
                }
            }
            if (!is_unk)
            {
                letters[np++] = all[b];
            }
        }
        nall = np; /* ns */
    }
    else
    {
        /* Auto: every letter is an unknown. */
        if (nall > MAXU)
        {
            snprintf(out, out_size,
                     "Too many variables (max %d).\nUse \"solve for\" to pick\nthe unknowns.",
                     MAXU);
            goto done;
        }
        memcpy(letters, all, (size_t)nall);
        nu = nall;
    }

    {
        bool any_ineq = false;
        for (int i = 0; i < n_eqs; i++)
        {
            if (asts[i]->kind == EQ_NODE_REL && asts[i]->relop != '=')
            {
                any_ineq = true;
                break;
            }
        }

        if (n_eqs == 1 && nall == 1)
        {
            eq_node_t *root = asts[0];
            char relop = (root->kind == EQ_NODE_REL) ? root->relop : '=';
            if (relop != '=')
            {
                ret = _solve_univariate_ineq(root, letters[0], relop, out, out_size);
            }
            else
            {
                ret = _solve_univariate_eq(root, letters[0], out, out_size);
            }
        }
        else if (any_ineq)
        {
            snprintf(out, out_size,
                     "仅支持单变量不等式；多元/方程组的不等式暂不支持");
            ret = 0;
        }
        else if (_is_linear(asts, n_eqs, letters, nall))
        {
            ret = _solve_linear_general(asts, n_eqs, letters, nall, nu, out, out_size);
        }
        else if (nall > nu)
        {
            /* Symbolic parameters can only be carried through a linear system;
             * a non-linear one would need full symbolic algebra. */
            snprintf(out, out_size,
                     "Cannot solve symbolically:\nnon-linear equations with\nother letters.\nGive them values first.");
            ret = 0;
        }
        else
        {
            ret = _solve_nonlinear(asts, n_eqs, letters, nall, out, out_size);
        }
    }

done:
    for (int i = 0; i < n_eqs; i++)
    {
        eq_ast_free(asts[i]);
    }
    return ret;
}

int cos_eqsolve(const char *eqs[], int n_eqs, char *out, size_t out_size)
{
    return cos_eqsolve_for(eqs, n_eqs, NULL, out, out_size);
}
