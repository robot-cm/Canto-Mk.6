/**
 * @file eos_shell_framework.h
 * @brief Interactive Shell framework (front-end to the Core command engine).
 *
 * The Core command engine lives in src/kernel/shell (eos_shell_exec). This
 * framework is the *interactive front-end*: it owns the edit line buffer,
 * command history, the prompt, and basic line editing (backspace, Enter,
 * and Up/Down history recall). It is transport-agnostic — a serial driver,
 * a USB-CDC reader, or the simulator's console feeds it one byte at a time
 * via eos_shell_framework_feed().
 *
 * Raw echo (typed characters, prompt) is kept separate from command output
 * so it works correctly over a byte stream: echo_fn emits raw text (no added
 * newline) while command results go through eos_shell_exec's own callback
 * (which adds newlines).
 *
 * Core-level: depends only on the kernel shell, not on LVGL / SD / UI.
 */

#ifndef EOS_SHELL_FRAMEWORK_H
#define EOS_SHELL_FRAMEWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "eos_shell.h" /* eos_shell_output_cb_t */

/* Public macros ----------------------------------------------*/
#define EOS_SF_LINE_MAX     256
#define EOS_SF_HISTORY_MAX  8
#define EOS_SF_PROMPT       "EOS> "

/* Public typedefs --------------------------------------------*/

/**
 * @brief Called with a completed command line (no trailing newline).
 *        The usual implementation dispatches it via eos_shell_exec().
 */
typedef void (*eos_shell_line_handler_t)(const char *line, void *user);

/**
 * @brief Runtime console enable/disable hook.
 *
 * The interactive console (shell listener task + serial log) can be toggled
 * at runtime from the UI (Control Center "DEV" switch). The framework stores
 * the current state and applies it to the serial log level; the transport
 * itself (e.g. a USB-CDC listener task in main.c) registers this callback to
 * be started/stopped accordingly.
 *
 * @param enabled true to start the console transport, false to stop it
 * @param user    Opaque data registered with eos_shell_framework_set_console_ctl()
 */
typedef void (*eos_shell_console_ctl_t)(bool enabled, void *user);

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the framework (reset buffer/history). Safe to call once.
 */
void eos_shell_framework_init(void);

/**
 * @brief Register the transport start/stop hook (set by the platform layer).
 * @param cb   Callback invoked on every eos_shell_framework_set_console_enabled().
 *             May be NULL to detach.
 * @param user Opaque data forwarded to the callback.
 */
void eos_shell_framework_set_console_ctl(eos_shell_console_ctl_t cb, void *user);

/**
 * @brief Enable/disable the interactive console at runtime.
 *
 * Applies the serial log level (DEBUG when enabled, OFF when disabled) and
 * forwards the new state to the registered transport hook.
 *
 * @param enabled New console state
 */
void eos_shell_framework_set_console_enabled(bool enabled);

/**
 * @brief Get the current interactive console state.
 * @return true if the console is enabled
 */
bool eos_shell_framework_get_console_enabled(void);

/**
 * @brief Feed a single input character into the framework.
 * @param c        Next input byte (ASCII printable, '\\b', '\\r', '\\n', or
 *                 the first byte 0x1B of an ANSI escape sequence).
 * @param echo_fn  Raw echo sink (no added newline). May be NULL to suppress.
 * @param line_fn  Invoked with the completed line on Enter. May be NULL.
 * @param user     Opaque data forwarded to both callbacks.
 */
void eos_shell_framework_feed(char c,
                              eos_shell_output_cb_t echo_fn,
                              eos_shell_line_handler_t line_fn,
                              void *user);

/**
 * @brief Emit the prompt string via echo_fn (used after init / each command).
 */
void eos_shell_framework_prompt(eos_shell_output_cb_t echo_fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SHELL_FRAMEWORK_H */
