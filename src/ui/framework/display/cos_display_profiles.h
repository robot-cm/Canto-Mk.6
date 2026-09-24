/**
 * @file cos_display_profiles.h
 * @brief The four reference profiles required for testing (req #12).
 */
#ifndef COS_DISPLAY_PROFILES_H
#define COS_DISPLAY_PROFILES_H

#include "cos_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COS_PROFILE_240C = 0,   /**< 240x240 round   */
    COS_PROFILE_320C,       /**< 320x320 round   */
    COS_PROFILE_240S,       /**< 240x280 square  */
    COS_PROFILE_240R,       /**< 240x320 rectangle */
    COS_PROFILE_COUNT
} cos_profile_id_t;

/** @brief Get one of the four reference profiles. */
cos_display_profile_t cos_display_profiles_get(cos_profile_id_t id);

/** @brief Human-readable name for logging/tests. */
const char *cos_display_profiles_name(cos_profile_id_t id);

#ifdef __cplusplus
}
#endif
#endif /* COS_DISPLAY_PROFILES_H */
