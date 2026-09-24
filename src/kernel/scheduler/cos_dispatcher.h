/**
 * @file cos_dispatcher.h
 * @brief Task dispatcher
 */

#ifndef COS_DISPATCHER_H
#define COS_DISPATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef void (*cos_dispatcher_cb_t)(void *); /**< Callback definition */
/* Public function prototypes --------------------------------*/
/**
 * @brief Initialize task dispatcher (must be called before GUI thread starts or during initialization phase)
 */
void cos_dispatcher_init(void);
/**
 * @brief Add callback to callback queue
 * @param cb Callback function
 * @param user_data User data
 * @note Can be called from any thread or ISR, returns as quickly as possible.
 */
void cos_dispatcher_call(cos_dispatcher_cb_t cb, void *user_data);
/**
 * @brief Main handler, called periodically in GUI thread loop to execute all callbacks
 */
void cos_dispatch_tick(void);
#ifdef __cplusplus
}
#endif

#endif /* COS_DISPATCHER_H */
