/**
 * @file cos_service_haptic.h
 * @brief Haptic service
 */

#ifndef COS_SERVICE_HAPTIC_H
#define COS_SERVICE_HAPTIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_dev_vibrator.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef enum
{
    COS_HAPTIC_STRENGTH_OFF = 0,
    COS_HAPTIC_STRENGTH_NORMAL = 128,
    COS_HAPTIC_STRENGTH_INTENSE = 255,
} cos_haptic_strength_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Short and light tick vibration
 */
void cos_haptic_tick(void);

/**
 * @brief Slightly stronger and longer vibration
 */
void cos_haptic_buzz(void);

/**
 * @brief Strongest and longest vibration
 */
void cos_haptic_vibrate_long(void);

/**
 * @brief Haptic service initialization
 */
void cos_service_haptic_init(void);

/**
 * @brief Set global haptic strength
 */
void cos_haptic_set_strength(cos_haptic_strength_t s);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_HAPTIC_H */
