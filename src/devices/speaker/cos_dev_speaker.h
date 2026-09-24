/**
 * @file cos_dev_speaker.h
 * @brief Speaker device - pure PCM output sink
 */

#ifndef COS_DEV_SPEAKER_H
#define COS_DEV_SPEAKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_device.h"
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef struct
{
    int (*init)(void);
    int (*deinit)(void);
    int (*open)(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
    int (*borrow)(void **p_buf, uint32_t *p_capacity);
    int (*enqueue)(void *buf, uint32_t size);
    int (*stop)(void);
    int (*pause)(void);
    int (*resume)(void);
    int (*set_volume)(uint8_t volume);
    bool (*is_available)(void);
} cos_dev_speaker_ops_t;

typedef struct
{
    const cos_dev_speaker_ops_t *ops;
    cos_dev_state_t _state;
} cos_dev_speaker_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Get the singleton speaker device instance
 * @return cos_dev_speaker_t* Pointer to speaker device, or NULL if not registered
 */
cos_dev_speaker_t *cos_dev_speaker_get_instance(void);

/**
 * @brief Register speaker device operations
 * @param ops Pointer to operations struct, must have open and borrow functions
 * @return cos_result_t COS_OK on success, or error code on failure
 */
cos_result_t cos_dev_speaker_register(const cos_dev_speaker_ops_t *ops);

/**
 * @brief Get the current state of the speaker device
 * @return cos_dev_state_t Current state of the speaker device
 */
cos_dev_state_t cos_dev_speaker_get_state(void);

/**
 * @brief Report the state of the speaker device
 * @param state New state to report
 */
void cos_dev_speaker_report_state(cos_dev_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* COS_DEV_SPEAKER_H */
