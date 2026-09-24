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

#define MAXV EQSOLVER_MAX_VARS
#define MAXE EQSOLVER_MAX_EQS

#define RES_TOL_LIN 1e-3f
#define RES_TOL_SQ 1e-3f
#define RES_TOL_OVER 1e-2f
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

/* ---------------- linear solver (Gaussian elimination + rank) ---------------- */

static int _solve_linear(eq_node_t *asts[], int n, const char letters[], int m,
                         char *out, size_t cap)
{
    /* Augmented matrix [A | -c], A is Jacobian (constant), c = F(0). */
    float aug[MAXE][MAXV + 1];
    memset(aug, 0, sizeof(aug));

    float v0[26];
    memset(v0, 0, sizeof(v0));
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            float h = 1e-3f * fmaxf(1.0f, fabsf(v0[(int)(letters[j] - 'a')]));
            float saved = v0[(int)(letters[j] - 'a')];
            v0[(int)(letters[j] - 'a')] = saved + h;
            float fp = eq_eval(asts[i], v0);
            v0[(int)(letters[j] - 'a')] = saved - h;
            float fm = eq_eval(asts[i], v0);
            v0[(int)(letters[j] - 'a')] = saved;
            aug[i][j] = (fp - fm) / (2.0f * h);
        }
        aug[i][m] = -eq_eval(asts[i], v0);
    }

    /* Gaussian elimination with partial pivoting (forward elimination). */
    int rank = 0;
    int pivot_col[MAXV];
    memset(pivot_col, -1, sizeof(pivot_col));
    for (int col = 0; col < m; col++)
    {
        int piv = -1;
        float maxv = RES_TOL_LIN;
        for (int r = rank; r < n; r++)
        {
            if (fabsf(aug[r][col]) > maxv)
            {
                maxv = fabsf(aug[r][col]);
                piv = r;
            }
        }
        if (piv < 0)
        {
            continue; /* dependent column */
        }
        if (piv != rank)
        {
            for (int j = 0; j <= m; j++)
            {
                float t = aug[rank][j];
                aug[rank][j] = aug[piv][j];
                aug[piv][j] = t;
            }
        }
        float pv = aug[rank][col];
        for (int j = col; j <= m; j++)
        {
            aug[rank][j] /= pv;
        }
        for (int r = rank + 1; r < n; r++)
        {
            float f = aug[r][col];
            if (f != 0.0f)
            {
                for (int j = col; j <= m; j++)
                {
                    aug[r][j] -= f * aug[rank][j];
                }
            }
        }
        pivot_col[rank] = col;
        rank++;
    }

    /* Consistency: zero-coefficient rows with nonzero RHS => inconsistent. */
    bool inconsistent = false;
    for (int r = rank; r < n; r++)
    {
        bool allzero = true;
        for (int j = 0; j < m; j++)
        {
            if (fabsf(aug[r][j]) > RES_TOL_LIN)
            {
                allzero = false;
                break;
            }
        }
        if (allzero && fabsf(aug[r][m]) > RES_TOL_LIN)
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
    if (rank < m)
    {
        _append(out, &off, cap, "Infinite solutions\n(underdetermined system)");
        return 0;
    }

    /* Unique solution via back-substitution. */
    float sol[MAXV];
    memset(sol, 0, sizeof(sol));
    for (int i = rank - 1; i >= 0; i--)
    {
        int pc = pivot_col[i];
        float sum = 0.0f;
        for (int j = pc + 1; j < m; j++)
        {
            sum += aug[i][j] * sol[j];
        }
        sol[pc] = aug[i][m] - sum;
    }
    char vbuf[64];
    for (int j = 0; j < m; j++)
    {
        _fmt_val(vbuf, sizeof(vbuf), sol[j]);
        _append_fmt(out, &off, cap, "%c = %s", letters[j], vbuf);
        if (j + 1 < m)
        {
            _append(out, &off, cap, "\n");
        }
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
            if (rn < (n == m ? RES_TOL_SQ : RES_TOL_OVER))
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
                for (int a = 0; a < m; a++)
                {
                    x[a] = xnew[a];
                }
                lambda *= 0.5f;
                if (lambda < 1e-6f)
                {
                    lambda = 1e-6f;
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
        _append(out, &off, cap, "No solution");
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

static bool _is_linear(eq_node_t *asts[], int n, const char letters[], int m)
{
    float base1[26];
    float base2[26];
    memset(base1, 0, sizeof(base1));
    memset(base2, 0, sizeof(base2));
    for (int j = 0; j < m; j++)
    {
        base1[(int)(letters[j] - 'a')] = 1.0f;
        base2[(int)(letters[j] - 'a')] = -2.0f;
    }
    float J1[MAXE][MAXV];
    float J2[MAXE][MAXV];
    for (int i = 0; i < n; i++)
    {
        eq_node_t *e = asts[i];
        if (e->kind == EQ_NODE_REL)
        {
            e = e->a; /* unwrap relation to the expression LHS - RHS */
        }
        for (int j = 0; j < m; j++)
        {
            float h1 = 1e-3f * fmaxf(1.0f, fabsf(base1[(int)(letters[j] - 'a')]));
            float saved = base1[(int)(letters[j] - 'a')];
            base1[(int)(letters[j] - 'a')] = saved + h1;
            float fp = eq_eval(e, base1);
            base1[(int)(letters[j] - 'a')] = saved - h1;
            float fm = eq_eval(asts[i], base1);
            base1[(int)(letters[j] - 'a')] = saved;
            J1[i][j] = (fp - fm) / (2.0f * h1);

            float h2 = 1e-3f * fmaxf(1.0f, fabsf(base2[(int)(letters[j] - 'a')]));
            float sv2 = base2[(int)(letters[j] - 'a')];
            base2[(int)(letters[j] - 'a')] = sv2 + h2;
            float fp2 = eq_eval(asts[i], base2);
            base2[(int)(letters[j] - 'a')] = sv2 - h2;
            float fm2 = eq_eval(e, base2);
            base2[(int)(letters[j] - 'a')] = sv2;
            J2[i][j] = (fp2 - fm2) / (2.0f * h2);
        }
    }
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            if (fabsf(J1[i][j] - J2[i][j]) > 1e-2f * (1.0f + fabsf(J1[i][j])))
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

int cos_eqsolve(const char *eqs[], int n_eqs, char *out, size_t out_size)
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

    /* Variable union (sorted alphabetically). */
    char letters[MAXV + 1];
    int m = 0;
    memset(letters, 0, sizeof(letters));
    char tmp[MAXV];
    for (int i = 0; i < n_eqs; i++)
    {
        int cnt = eq_collect_vars(asts[i], tmp, MAXV);
        for (int a = 0; a < cnt; a++)
        {
            bool has = false;
            for (int b = 0; b < m; b++)
            {
                if (letters[b] == tmp[a])
                {
                    has = true;
                    break;
                }
            }
            if (!has && m < MAXV)
            {
                letters[m++] = tmp[a];
            }
        }
    }

    int ret = 0;
    if (m == 0)
    {
        snprintf(out, out_size, "No variable found in equations");
    }
    else
    {
        /* bubble sort letters */
        for (int a = 0; a < m; a++)
        {
            for (int b = a + 1; b < m; b++)
            {
                if (letters[b] < letters[a])
                {
                    char t = letters[a];
                    letters[a] = letters[b];
                    letters[b] = t;
                }
            }
        }

        bool any_ineq = false;
        for (int i = 0; i < n_eqs; i++)
        {
            if (asts[i]->kind == EQ_NODE_REL && asts[i]->relop != '=')
            {
                any_ineq = true;
                break;
            }
        }

        if (n_eqs == 1 && m == 1)
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
        else if (_is_linear(asts, n_eqs, letters, m))
        {
            ret = _solve_linear(asts, n_eqs, letters, m, out, out_size);
        }
        else
        {
            ret = _solve_nonlinear(asts, n_eqs, letters, m, out, out_size);
        }
    }

    for (int i = 0; i < n_eqs; i++)
    {
        eq_ast_free(asts[i]);
    }
    return ret;
}
