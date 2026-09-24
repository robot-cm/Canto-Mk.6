/**
 * @file cos_eqsolver_parser.c
 * @brief Recursive-descent parser for equation strings.
 */

#include "cos_eqsolver_parser.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cos_mem.h"

/* ---------------- tokenizer ---------------- */

typedef enum
{
    T_NUM,
    T_VAR,
    T_OP,
    T_FUNC,
    T_END
} tok_type_t;

typedef struct
{
    tok_type_t type;
    float num;
    char ch;
} tok_t;

#define MAX_TOKENS 128

typedef struct
{
    const char *src;
    size_t pos;
    size_t len;
    tok_t toks[MAX_TOKENS];
    int ntok;
    char err[EQSOLVER_PARSE_ERR_SIZE];
} parser_t;

static void _set_err(parser_t *p, const char *msg)
{
    strncpy(p->err, msg, EQSOLVER_PARSE_ERR_SIZE - 1);
    p->err[EQSOLVER_PARSE_ERR_SIZE - 1] = '\0';
}

static bool _is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool _is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Parse a number (with optional decimal point and exponent). */
static bool _parse_number(parser_t *p, float *out)
{
    size_t start = p->pos;
    while (p->pos < p->len && (_is_digit(p->src[p->pos]) || p->src[p->pos] == '.'))
    {
        p->pos++;
    }
    /* exponent part: e[+-]?digits */
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E'))
    {
        size_t save = p->pos;
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-'))
        {
            p->pos++;
        }
        if (p->pos < p->len && _is_digit(p->src[p->pos]))
        {
            while (p->pos < p->len && _is_digit(p->src[p->pos]))
            {
                p->pos++;
            }
        }
        else
        {
            /* not a real exponent; rewind */
            p->pos = save;
        }
    }

    if (p->pos == start)
    {
        return false;
    }

    char buf[32];
    size_t n = p->pos - start;
    if (n >= sizeof(buf))
    {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, p->src + start, n);
    buf[n] = '\0';
    *out = (float)atof(buf);
    return true;
}

static bool _tokenize(parser_t *p)
{
    p->ntok = 0;
    while (p->pos < p->len)
    {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t')
        {
            p->pos++;
            continue;
        }
        if (p->ntok >= MAX_TOKENS)
        {
            _set_err(p, "Equation too long");
            return false;
        }

        if (_is_digit(c) || c == '.')
        {
            float v;
            if (!_parse_number(p, &v))
            {
                _set_err(p, "Invalid number");
                return false;
            }
            p->toks[p->ntok].type = T_NUM;
            p->toks[p->ntok].num = v;
            p->ntok++;
            continue;
        }

        if (_is_alpha(c))
        {
            const char *s = p->src + p->pos;
            size_t rem = (size_t)(p->len - p->pos);
            /* functions: name immediately followed by '(' */
            if (rem >= 4 && strncmp(s, "sqrt", 4) == 0 && s[4] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'q';
                p->pos += 4; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "sin", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 's';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "cos", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'c';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "tan", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 't';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "exp", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'e';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "log", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'g';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 3 && strncmp(s, "abs", 3) == 0 && s[3] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'a';
                p->pos += 3; p->ntok++; continue;
            }
            if (rem >= 2 && strncmp(s, "ln", 2) == 0 && s[2] == '(')
            {
                p->toks[p->ntok].type = T_FUNC; p->toks[p->ntok].ch = 'n';
                p->pos += 2; p->ntok++; continue;
            }
            /* constants */
            if (rem >= 2 && strncmp(s, "pi", 2) == 0 &&
                (rem == 2 || !_is_alpha((unsigned char)s[2])))
            {
                p->toks[p->ntok].type = T_NUM;
                p->toks[p->ntok].num = (float)M_PI;
                p->pos += 2; p->ntok++; continue;
            }
            if (rem >= 1 && s[0] == 'e' && (rem == 1 || !_is_alpha((unsigned char)s[1])))
            {
                p->toks[p->ntok].type = T_NUM;
                p->toks[p->ntok].num = (float)M_E;
                p->pos += 1; p->ntok++; continue;
            }
            /* plain variable */
            p->toks[p->ntok].type = T_VAR;
            p->toks[p->ntok].ch = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            p->pos++;
            p->ntok++;
            continue;
        }

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
            c == '(' || c == ')' || c == '=' || c == '<' || c == '>')
        {
            p->toks[p->ntok].type = T_OP;
            p->toks[p->ntok].ch = c;
            p->pos++;
            p->ntok++;
            continue;
        }

        char msg[48];
        snprintf(msg, sizeof(msg), "Invalid character '%c'", c);
        _set_err(p, msg);
        return false;
    }

    if (p->ntok >= MAX_TOKENS)
    {
        _set_err(p, "Equation too long");
        return false;
    }
    p->toks[p->ntok].type = T_END;
    p->ntok++;
    return true;
}

/* Insert implicit multiplication: between a value-ending token and a
 * value-starting token (e.g. 2x -> 2*x, x y -> x*y, 2( -> 2*( ). */
static bool _insert_implicit_mul(parser_t *p)
{
    tok_t out[MAX_TOKENS * 2];
    int nout = 0;
    for (int i = 0; i < p->ntok; i++)
    {
        tok_t cur = p->toks[i];
        if (i > 0)
        {
            tok_t prev = p->toks[i - 1];
            bool prev_value_end = (prev.type == T_NUM || prev.type == T_VAR || prev.ch == ')');
            bool cur_value_start = (cur.type == T_NUM || cur.type == T_VAR ||
                                    cur.type == T_FUNC || cur.ch == '(');
            if (prev_value_end && cur_value_start)
            {
                if (nout >= MAX_TOKENS * 2)
                {
                    _set_err(p, "Equation too long");
                    return false;
                }
                out[nout].type = T_OP;
                out[nout].ch = '*';
                nout++;
            }
        }
        if (nout >= MAX_TOKENS * 2)
        {
            _set_err(p, "Equation too long");
            return false;
        }
        out[nout++] = cur;
    }
    if (nout > MAX_TOKENS)
    {
        _set_err(p, "Equation too long");
        return false;
    }
    memcpy(p->toks, out, (size_t)nout * sizeof(tok_t));
    p->ntok = nout;
    return true;
}

/* ---------------- AST ---------------- */

static eq_node_t *_mk_node(parser_t *p, eq_node_kind_t kind)
{
    eq_node_t *n = (eq_node_t *)cos_malloc(sizeof(eq_node_t));
    if (!n)
    {
        _set_err(p, "Out of memory");
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    return n;
}

static int g_ti; /* token cursor during parse */

static tok_t *_peek(parser_t *p)
{
    return &p->toks[g_ti];
}

static tok_t *_next(parser_t *p)
{
    return &p->toks[g_ti++];
}

static eq_node_t *_parse_expr(parser_t *p);
static eq_node_t *_parse_unary(parser_t *p);

static eq_node_t *_parse_base(parser_t *p)
{
    tok_t *t = _peek(p);
    if (t->type == T_NUM)
    {
        _next(p);
        eq_node_t *n = _mk_node(p, EQ_NODE_NUM);
        if (!n)
        {
            return NULL;
        }
        n->value = t->num;
        return n;
    }
    if (t->type == T_VAR)
    {
        _next(p);
        eq_node_t *n = _mk_node(p, EQ_NODE_VAR);
        if (!n)
        {
            return NULL;
        }
        n->var = t->ch;
        return n;
    }
    if (t->type == T_OP && t->ch == '(')
    {
        _next(p);
        eq_node_t *e = _parse_expr(p);
        if (!e)
        {
            return NULL;
        }
        tok_t *cl = _peek(p);
        if (!(cl->type == T_OP && cl->ch == ')'))
        {
            _set_err(p, "Missing ')'");
            return NULL;
        }
        _next(p);
        return e;
    }
    if (t->type == T_FUNC)
    {
        _next(p);
        eq_node_t *n = _mk_node(p, EQ_NODE_FUNC);
        if (!n)
        {
            return NULL;
        }
        n->fn = t->ch;
        n->a = _parse_unary(p);
        if (!n->a)
        {
            eq_ast_free(n);
            return NULL;
        }
        return n;
    }
    _set_err(p, "Unexpected token");
    return NULL;
}

static eq_node_t *_parse_unary(parser_t *p)
{
    tok_t *t = _peek(p);
    if (t->type == T_OP && (t->ch == '+' || t->ch == '-'))
    {
        _next(p);
        eq_node_t *operand = _parse_unary(p);
        if (!operand)
        {
            return NULL;
        }
        if (t->ch == '-')
        {
            eq_node_t *n = _mk_node(p, EQ_NODE_NEG);
            if (!n)
            {
                return NULL;
            }
            n->a = operand;
            return n;
        }
        return operand;
    }
    return _parse_base(p);
}

static eq_node_t *_parse_factor(parser_t *p)
{
    eq_node_t *base = _parse_unary(p);
    if (!base)
    {
        return NULL;
    }
    tok_t *t = _peek(p);
    while (t->type == T_OP && t->ch == '^')
    {
        _next(p);
        eq_node_t *rhs = _parse_factor(p); /* right associative */
        if (!rhs)
        {
            return NULL;
        }
        eq_node_t *n = _mk_node(p, EQ_NODE_POW);
        if (!n)
        {
            return NULL;
        }
        n->a = base;
        n->b = rhs;
        base = n;
        t = _peek(p);
    }
    return base;
}

static eq_node_t *_parse_term(parser_t *p)
{
    eq_node_t *lhs = _parse_factor(p);
    if (!lhs)
    {
        return NULL;
    }
    tok_t *t = _peek(p);
    while (t->type == T_OP && (t->ch == '*' || t->ch == '/'))
    {
        _next(p);
        eq_node_t *rhs = _parse_factor(p);
        if (!rhs)
        {
            return NULL;
        }
        eq_node_t *n = _mk_node(p, (t->ch == '*') ? EQ_NODE_MUL : EQ_NODE_DIV);
        if (!n)
        {
            return NULL;
        }
        n->a = lhs;
        n->b = rhs;
        lhs = n;
        t = _peek(p);
    }
    return lhs;
}

static eq_node_t *_parse_expr(parser_t *p)
{
    eq_node_t *lhs = _parse_term(p);
    if (!lhs)
    {
        return NULL;
    }
    tok_t *t = _peek(p);
    while (t->type == T_OP && (t->ch == '+' || t->ch == '-'))
    {
        _next(p);
        eq_node_t *rhs = _parse_term(p);
        if (!rhs)
        {
            return NULL;
        }
        eq_node_t *n = _mk_node(p, (t->ch == '+') ? EQ_NODE_ADD : EQ_NODE_SUB);
        if (!n)
        {
            return NULL;
        }
        n->a = lhs;
        n->b = rhs;
        lhs = n;
        t = _peek(p);
    }
    return lhs;
}

eq_node_t *eq_parse(const char *src, char *errmsg, size_t errsize)
{
    parser_t p;
    memset(&p, 0, sizeof(p));
    p.src = src ? src : "";
    p.len = strlen(p.src);
    p.pos = 0;
    if (errmsg && errsize > 0)
    {
        errmsg[0] = '\0';
    }

    if (!_tokenize(&p))
    {
        if (errmsg)
        {
            strncpy(errmsg, p.err, errsize - 1);
            errmsg[errsize - 1] = '\0';
        }
        return NULL;
    }
    if (!_insert_implicit_mul(&p))
    {
        if (errmsg)
        {
            strncpy(errmsg, p.err, errsize - 1);
            errmsg[errsize - 1] = '\0';
        }
        return NULL;
    }

    g_ti = 0;
    eq_node_t *root = _parse_expr(&p);
    if (!root)
    {
        if (errmsg)
        {
            strncpy(errmsg, p.err, errsize - 1);
            errmsg[errsize - 1] = '\0';
        }
        return NULL;
    }

    tok_t *t = _peek(&p);
    if (t->type == T_OP && (t->ch == '=' || t->ch == '>' || t->ch == '<'))
    {
        char op = t->ch;
        _next(&p);
        tok_t *nxt = _peek(&p);
        if (nxt->type == T_OP && nxt->ch == '=' && (op == '>' || op == '<'))
        {
            op = (op == '>') ? 'G' : 'L';
            _next(&p);
        }
        eq_node_t *rhs = _parse_expr(&p);
        if (!rhs)
        {
            eq_ast_free(root);
            if (errmsg)
            {
                strncpy(errmsg, p.err, errsize - 1);
                errmsg[errsize - 1] = '\0';
            }
            return NULL;
        }
        eq_node_t *sub = _mk_node(&p, EQ_NODE_SUB);
        if (!sub)
        {
            eq_ast_free(root);
            eq_ast_free(rhs);
            if (errmsg)
            {
                strncpy(errmsg, p.err, errsize - 1);
                errmsg[errsize - 1] = '\0';
            }
            return NULL;
        }
        sub->a = root;
        sub->b = rhs;
        eq_node_t *rel = _mk_node(&p, EQ_NODE_REL);
        if (!rel)
        {
            eq_ast_free(sub);
            if (errmsg)
            {
                strncpy(errmsg, p.err, errsize - 1);
                errmsg[errsize - 1] = '\0';
            }
            return NULL;
        }
        rel->a = sub;
        rel->relop = op;
        root = rel;
    }

    if (_peek(&p)->type != T_END)
    {
        eq_ast_free(root);
        if (errmsg)
        {
            strncpy(errmsg, "Unexpected trailing text", errsize - 1);
            errmsg[errsize - 1] = '\0';
        }
        return NULL;
    }
    return root;
}

float eq_eval(eq_node_t *node, const eq_varvals_t varvals)
{
    if (!node)
    {
        return 0.0f;
    }
    switch (node->kind)
    {
        case EQ_NODE_NUM:
            return node->value;
        case EQ_NODE_VAR:
            return varvals[(int)(node->var - 'a')];
        case EQ_NODE_ADD:
            return eq_eval(node->a, varvals) + eq_eval(node->b, varvals);
        case EQ_NODE_SUB:
            return eq_eval(node->a, varvals) - eq_eval(node->b, varvals);
        case EQ_NODE_MUL:
            return eq_eval(node->a, varvals) * eq_eval(node->b, varvals);
        case EQ_NODE_DIV:
            return eq_eval(node->a, varvals) / eq_eval(node->b, varvals);
        case EQ_NODE_POW:
        {
            float b = eq_eval(node->b, varvals);
            float x = eq_eval(node->a, varvals);
            return powf(x, b);
        }
        case EQ_NODE_NEG:
            return -eq_eval(node->a, varvals);

        case EQ_NODE_FUNC:
        {
            float a = eq_eval(node->a, varvals);
            switch (node->fn)
            {
                case 's': return sinf(a);
                case 'c': return cosf(a);
                case 't': return tanf(a);
                case 'n': return logf(a > 0.0f ? a : -a);   /* ln(|a|) */
                case 'g': return log10f(a > 0.0f ? a : -a); /* log10(|a|) */
                case 'q': return sqrtf(fabsf(a));
                case 'a': return fabsf(a);
                case 'e': return expf(a);
                default:  return 0.0f;
            }
        }

        case EQ_NODE_REL:
            /* Evaluate the underlying expression (LHS - RHS); the caller applies
               the relation against 0. */
            return eq_eval(node->a, varvals);
        default:
            return 0.0f;
    }
}

void eq_ast_free(eq_node_t *node)
{
    if (!node)
    {
        return;
    }
    eq_ast_free(node->a);
    eq_ast_free(node->b);
    cos_free(node);
}

/* Variable collection with de-duplication (iterative DFS). */
int eq_collect_vars(eq_node_t *node, char *out_letters, int max)
{
    char tmp[EQSOLVER_MAX_VARS + 1];
    int n = 0;
    memset(tmp, 0, sizeof(tmp));

    /* iterative DFS to avoid deep recursion issues */
    eq_node_t *stack[64];
    int sp = 0;
    if (node)
    {
        stack[sp++] = node;
    }
    while (sp > 0)
    {
        eq_node_t *cur = stack[--sp];
        if (cur->kind == EQ_NODE_VAR)
        {
            bool found = false;
            for (int i = 0; i < n; i++)
            {
                if (tmp[i] == cur->var)
                {
                    found = true;
                    break;
                }
            }
            if (!found && n < (int)sizeof(tmp))
            {
                tmp[n++] = cur->var;
            }
        }
        if (cur->b)
        {
            stack[sp++] = cur->b;
        }
        if (cur->a)
        {
            stack[sp++] = cur->a;
        }
    }

    int out = 0;
    for (int i = 0; i < n && out < max; i++)
    {
        out_letters[out++] = tmp[i];
    }
    return out;
}
