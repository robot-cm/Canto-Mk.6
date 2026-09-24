/**
 * @file cos_eqsolver_parser.h
 * @brief Equation string parser -> AST, with evaluation and Jacobian support.
 *
 * Uses single-precision float throughout (ESP32-S3 FPU friendly).
 * Variables are single letters a-z (case-insensitive, upper folded to lower).
 */

#ifndef COS_EQSOLVER_PARSER_H
#define COS_EQSOLVER_PARSER_H

#include <stddef.h>
#include <stdint.h>

/* Hard caps to keep memory/CPU bounded on the MCU. */
#define EQSOLVER_MAX_VARS 6
#define EQSOLVER_MAX_EQS 8
#define EQSOLVER_PARSE_ERR_SIZE 96

/* AST node kinds. */
typedef enum
{
    EQ_NODE_NUM, /* constant */
    EQ_NODE_VAR, /* single variable letter (stored as 'a'..'z') */
    EQ_NODE_ADD,
    EQ_NODE_SUB,
    EQ_NODE_MUL,
    EQ_NODE_DIV,
    EQ_NODE_POW,
    EQ_NODE_NEG, /* unary minus */
    EQ_NODE_FUNC,/* function call: fn = code ('s','c','t','n','g','q','a','e'), a = arg */
    EQ_NODE_REL  /* relational: (lhs - rhs) compared via relop */
} eq_node_kind_t;

typedef struct _eq_node
{
    eq_node_kind_t kind;
    float value;          /* for NUM */
    char var;             /* for VAR */
    char fn;              /* for FUNC: function code */
    struct _eq_node *a;   /* left / operand (for REL: the expression lhs - rhs) */
    struct _eq_node *b;   /* right operand (binary ops); unused for REL/FUNC */
    char relop;           /* for REL: '=' '>' '<' 'G'(>=) 'L'(<=) */
} eq_node_t;

/* Variable value table indexed by letter: varvals[letter-'a'] (a..z). */
typedef float eq_varvals_t[26];

/**
 * @brief Parse one equation/inequality string.
 * @param src      Input string.
 * @param errmsg   Receives a human readable error on failure (English).
 * @param errsize  Size of errmsg buffer.
 * @return eq_node_t* AST root on success (free with eq_ast_free), NULL on error.
 *
 * The relation may be '=', '>', '<', '>=', '<='. The AST wraps the
 * comparison in an EQ_NODE_REL node whose 'a' child is the expression
 * (LHS - RHS); eq_eval() of a REL node returns that expression, so the
 * whole relation reduces to f(x) relop 0.
 * If no relation is present, the whole expression is treated as == 0.
 */
eq_node_t *eq_parse(const char *src, char *errmsg, size_t errsize);

/* Evaluate AST given variable values (varvals[letter-'a']). */
float eq_eval(eq_node_t *node, const eq_varvals_t varvals);

/* Free an AST. */
void eq_ast_free(eq_node_t *node);

/**
 * @brief Collect distinct variable letters used in the AST.
 * @param out_letters Buffer to fill with distinct letters (not NUL terminated).
 * @param max         Capacity of out_letters.
 * @return int Number of distinct variables found.
 */
int eq_collect_vars(eq_node_t *node, char *out_letters, int max);

#endif /* COS_EQSOLVER_PARSER_H */
