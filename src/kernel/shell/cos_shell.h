/**
 * @file cos_shell.h
 * @brief Canto Mk.6 low-level Shell (belongs to Core, must not depend on SD/UI)
 *
 * The Shell is a Core subsystem. It must remain usable even when the SD card
 * is missing, corrupted, every app has crashed, or the Launcher UI is down.
 * Therefore command handlers only call stable Core/Service APIs and deliver
 * output through a caller-provided callback (so it can be routed to a serial
 * console, a log listener, or an on-screen widget).
 */

#ifndef COS_SHELL_H
#define COS_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Public typedefs --------------------------------------------*/

/**
 * @brief Output sink for shell command results.
 * @param line A single line of output (no trailing newline).
 * @param user Opaque user data passed to cos_shell_exec.
 */
typedef void (*cos_shell_output_cb_t)(const char *line, void *user);

/**
 * @brief Reboot hook. When set, the `reboot` command invokes it instead of
 *        only printing a message. The simulator uses this to quit cleanly.
 */
typedef void (*cos_shell_reboot_cb_t)(void);

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize the shell subsystem. Safe to call once during boot.
 */
void cos_shell_init(void);

/**
 * @brief Tear down the shell subsystem.
 */
void cos_shell_deinit(void);

/**
 * @brief Register a reboot hook (called by the `reboot` command).
 * @param cb Reboot callback, or NULL to clear.
 */
void cos_shell_set_reboot_cb(cos_shell_reboot_cb_t cb);

/**
 * @brief Execute a single command line.
 * @param cmdline Command string (will not be modified).
 * @param out Output callback, called once per output line.
 * @param user Opaque data forwarded to @p out.
 * @return 0 on success, negative on error (e.g. not initialized).
 */
int cos_shell_exec(const char *cmdline, cos_shell_output_cb_t out, void *user);

/**
 * @brief Convenience helper: execute a command and route output through the
 *        log system at INFO level.
 * @param cmdline Command string.
 */
void cos_shell_exec_log(const char *cmdline);

#ifdef __cplusplus
}
#endif

#endif /* COS_SHELL_H */
