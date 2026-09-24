/**
 * @file cos_error.h
 * @brief Canto Mk.6 error code definitions
 */

#ifndef COS_ERROR_H
#define COS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief Canto Mk.6 return value status
 */
typedef enum
{
    COS_OK = 0, /**< Operation successful */
    COS_FAILED = -1, /**< Generic failure */

    /* Common errors (-100 ~ -199) */
    COS_ERR_INVALID_ARG = -100, /**< Invalid input argument */
    COS_ERR_VAR_NULL = -101, /**< Required argument is NULL */
    COS_ERR_MEM = -102, /**< Memory related error */
    COS_ERR_NOT_FOUND = -103, /**< Resource not found */
    COS_ERR_ALREADY_EXISTS = -104, /**< Resource already exists */
    COS_ERR_BUSY = -105, /**< Busy */
    COS_ERR_TIMEOUT = -106, /**< Timeout */

    /* Lifecycle/state errors (-200 ~ -299) */
    COS_ERR_NOT_INITIALIZED = -200, /**< Not initialized */
    COS_ERR_ALREADY_INITIALIZED = -201, /**< Already initialized */
    COS_ERR_INVALID_STATE = -202, /**< Invalid state */

    /* Storage/IO errors (-300 ~ -399) */
    COS_ERR_FILE_ERROR = -300, /**< File related error */
    COS_ERR_IO = -301, /**< IO failure */
    COS_ERR_PATH_TOO_LONG = -302, /**< Path too long */

    /* Data/format errors (-400 ~ -499) */
    COS_ERR_JSON_ERROR = -400, /**< JSON related error */
    COS_ERR_VALUE_MISMATCH = -401, /**< Value mismatch */
    COS_ERR_SDK_VERSION = -402, /**< Package requires higher SDK/API level */

    /* Container/data-structure errors (-500 ~ -599) */
    COS_ERR_STACK_EMPTY = -500, /**< Stack empty */
    COS_ERR_STACK_FULL = -501, /**< Stack full */

    /* Device/driver errors (-600 ~ -699) */
    COS_ERR_DEV_NOT_FOUND = -600, /**< Device not found */
    COS_ERR_DEV_BUSY = -601, /**< Device busy */
    COS_ERR_DEV_ERROR = -602, /**< Device error */
    COS_ERR_DEV_OPS_NOT_SUPPORTED = -603, /**< Device operation not supported */

    /* Script engine errors (-700 ~ -799) */
    COS_ERR_SCRIPT_NULL_PACKAGE = -700, /**< Script package argument is NULL */
    COS_ERR_SCRIPT_INVALID_JS = -701, /**< JavaScript syntax or runtime error */
    COS_ERR_SCRIPT_EXCEPTION = -702, /**< JerryScript VM exception */
    COS_ERR_SCRIPT_ALREADY_RUNNING = -703, /**< Another script is already running */
    COS_ERR_SCRIPT_INIT_FAIL = -704, /**< JerryScript VM initialization failed */
    COS_ERR_SCRIPT_NOT_RUNNING = -705, /**< No script is currently running */
    COS_ERR_SCRIPT_NO_SAVED_CTX = -706, /**< No saved realm/context to restore */
    COS_ERR_SCRIPT_SEC_ERROR = -707, /**< Security check failed in script program */
    COS_ERR_SCRIPT_PROG_NOT_FOUND = -708, /**< Script program not found in program list */
    COS_ERR_SCRIPT_INVALID_TYPE = -709, /**< Invalid script program type for operation */

    /* Network / SOCKS5 errors (-800 ~ -899) */
    COS_ERR_NET_NOT_CONFIGURED = -800, /**< Proxy server not configured */
    COS_ERR_NET_PROTOCOL = -801,       /**< SOCKS5 protocol violation / bad reply */
    COS_ERR_NET_SOCK = -802,           /**< Socket layer failure */
    COS_ERR_NET_HANDSHAKE = -803,      /**< SOCKS5 handshake rejected by server */

    /* Wi-Fi / Bluetooth errors (-810 ~ -819) */
    COS_ERR_NET_NOT_CONNECTED = -810,  /**< Interface not connected */
    COS_ERR_NET_NO_SSID = -811,        /**< No SSID supplied / configured */
    COS_ERR_NET_SCAN = -812,           /**< Scan failed */
    COS_ERR_NET_BT_NOT_ENABLED = -813, /**< Bluetooth radio disabled */
    COS_ERR_NET_BT = -814,             /**< Bluetooth radio backend failure */

    COS_ERR_UNKNOWN = -999,
} cos_result_t;

/* Public function prototypes --------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* COS_ERROR_H */
