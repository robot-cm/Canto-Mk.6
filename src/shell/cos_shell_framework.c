/**
 * @file cos_shell_framework.c
 * @brief Interactive Shell framework implementation.
 */

#include "cos_shell_framework.h"

#include <string.h>
#include "cos_shell.h"
#include "cos_log.h"

/* State ------------------------------------------------------*/
typedef struct
{
    char     line[COS_SF_LINE_MAX];
    int      pos;
    char     history[COS_SF_HISTORY_MAX][COS_SF_LINE_MAX];
    int      hist_count;
    int      hist_idx;   /* -1 = editing current line; else index into history */
    int      esc_state;  /* 0=normal, 1=saw ESC, 2=saw '[' */
    bool     inited;
} cos_shell_framework_ctx_t;

static cos_shell_framework_ctx_t s_ctx;

/* Runtime console control state ------------------------------*/
#if defined(COS_BUILD_RELEASE) && COS_BUILD_RELEASE
/* Release profile: console starts disabled (persisted DEV switch re-applies at boot) */
static bool s_console_enabled = false;
#else
/* Dev profile: console starts enabled */
static bool s_console_enabled = true;
#endif
static cos_shell_console_ctl_t s_console_ctl = NULL;
static void *s_console_ctl_user = NULL;

/* Helpers ----------------------------------------------------*/

static void _prompt(cos_shell_output_cb_t echo_fn, void *user)
{
    if (echo_fn)
        echo_fn(COS_SF_PROMPT, user);
}

static void _redraw_line(cos_shell_output_cb_t echo_fn, void *user)
{
    /* carriage return, clear to end of line, prompt, current buffer */
    if (echo_fn)
    {
        echo_fn("\r\033[K", user);
        echo_fn(COS_SF_PROMPT, user);
        echo_fn(s_ctx.line, user);
    }
}

static void _push_history(const char *line)
{
    if (s_ctx.hist_count > 0 &&
        strcmp(s_ctx.history[s_ctx.hist_count - 1], line) == 0)
        return; /* ignore consecutive duplicates */
    if (s_ctx.hist_count < COS_SF_HISTORY_MAX)
    {
        strncpy(s_ctx.history[s_ctx.hist_count], line, COS_SF_LINE_MAX - 1);
        s_ctx.history[s_ctx.hist_count][COS_SF_LINE_MAX - 1] = '\0';
        s_ctx.hist_count++;
    }
    else
    {
        /* shift down, drop oldest */
        for (int i = 1; i < COS_SF_HISTORY_MAX; i++)
            strcpy(s_ctx.history[i - 1], s_ctx.history[i]);
        strncpy(s_ctx.history[COS_SF_HISTORY_MAX - 1], line, COS_SF_LINE_MAX - 1);
        s_ctx.history[COS_SF_HISTORY_MAX - 1][COS_SF_LINE_MAX - 1] = '\0';
    }
}

/* Public API -------------------------------------------------*/

void cos_shell_framework_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.hist_idx = -1;
    s_ctx.inited = true;
}

void cos_shell_framework_prompt(cos_shell_output_cb_t echo_fn, void *user)
{
    _prompt(echo_fn, user);
}

void cos_shell_framework_set_console_ctl(cos_shell_console_ctl_t cb, void *user)
{
    s_console_ctl = cb;
    s_console_ctl_user = user;
}

void cos_shell_framework_set_console_enabled(bool enabled)
{
    s_console_enabled = enabled;

    /* Serial log follows the console: ON -> full DEBUG, OFF -> silent. */
    cos_log_set_min_level(enabled ? COS_LOG_LEVEL_DEBUG : COS_LOG_LEVEL_OFF);

    /* Forward to the transport hook (task create/delete). The hook must be
     * idempotent: it is called on every state change, including boot apply. */
    if (s_console_ctl)
        s_console_ctl(enabled, s_console_ctl_user);
}

bool cos_shell_framework_get_console_enabled(void)
{
    return s_console_enabled;
}

void cos_shell_framework_feed(char c,
                              cos_shell_output_cb_t echo_fn,
                              cos_shell_line_handler_t line_fn,
                              void *user)
{
    if (!s_ctx.inited)
        cos_shell_framework_init();

    /* ---- ANSI escape sequence state machine (arrow keys) ---- */
    if (s_ctx.esc_state == 1)
    {
        if (c == '[')
            s_ctx.esc_state = 2;
        else
            s_ctx.esc_state = 0;
        return;
    }
    if (s_ctx.esc_state == 2)
    {
        s_ctx.esc_state = 0;
        if (c == 'A' && s_ctx.hist_count > 0) /* Up */
        {
            if (s_ctx.hist_idx < 0)
                s_ctx.hist_idx = s_ctx.hist_count - 1;
            else if (s_ctx.hist_idx > 0)
                s_ctx.hist_idx--;
            strncpy(s_ctx.line, s_ctx.history[s_ctx.hist_idx], COS_SF_LINE_MAX - 1);
            s_ctx.line[COS_SF_LINE_MAX - 1] = '\0';
            s_ctx.pos = (int)strlen(s_ctx.line);
            _redraw_line(echo_fn, user);
        }
        else if (c == 'B') /* Down */
        {
            if (s_ctx.hist_idx < 0)
                return;
            s_ctx.hist_idx++;
            if (s_ctx.hist_idx >= s_ctx.hist_count)
            {
                s_ctx.hist_idx = -1;
                s_ctx.line[0] = '\0';
                s_ctx.pos = 0;
            }
            else
            {
                strncpy(s_ctx.line, s_ctx.history[s_ctx.hist_idx], COS_SF_LINE_MAX - 1);
                s_ctx.line[COS_SF_LINE_MAX - 1] = '\0';
                s_ctx.pos = (int)strlen(s_ctx.line);
            }
            _redraw_line(echo_fn, user);
        }
        /* ignore Left/Right for now */
        return;
    }

    if (c == 0x1B) /* ESC */
    {
        s_ctx.esc_state = 1;
        return;
    }

    /* ---- editing ---- */
    if (c == '\r' || c == '\n')
    {
        if (echo_fn)
            echo_fn("\r\n", user);
        if (s_ctx.pos > 0)
        {
            s_ctx.line[s_ctx.pos] = '\0';
            _push_history(s_ctx.line);
            if (line_fn)
                line_fn(s_ctx.line, user);
        }
        s_ctx.line[0] = '\0';
        s_ctx.pos = 0;
        s_ctx.hist_idx = -1;
        _prompt(echo_fn, user);
        return;
    }

    if (c == '\b' || c == 0x7F) /* backspace / DEL */
    {
        if (s_ctx.pos > 0)
        {
            s_ctx.pos--;
            s_ctx.line[s_ctx.pos] = '\0';
            if (echo_fn)
                echo_fn("\b \b", user);
        }
        return;
    }

    /* printable ASCII */
    if (c >= 32 && c < 127 && s_ctx.pos < (int)sizeof(s_ctx.line) - 1)
    {
        s_ctx.line[s_ctx.pos++] = c;
        s_ctx.line[s_ctx.pos] = '\0';
        if (echo_fn)
        {
            char buf[2] = { c, '\0' };
            echo_fn(buf, user);
        }
    }
}
