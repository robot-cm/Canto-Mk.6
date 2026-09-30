/**
 * @file eos_display_profiles.h
 * @brief The four reference profiles required for testing (req #12).
 */
#ifndef EOS_DISPLAY_PROFILES_H
#define EOS_DISPLAY_PROFILES_H

#include "eos_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EOS_PROFILE_240C = 0,   /**< 240x240 round   */
    EOS_PROFILE_320C,       /**< 320x320 round   */
    EOS_PROFILE_240S,       /**< 240x280 square  */
    EOS_PROFILE_240R,       /**< 240x320 rectangle */
    EOS_PROFILE_COUNT
} eos_profile_id_t;

/** @brief Get one of the four reference profiles. */
eos_display_profile_t eos_display_profiles_get(eos_profile_id_t id);

/** @brief Human-readable name for logging/tests. */
const char *eos_display_profiles_name(eos_profile_id_t id);

#ifdef __cplusplus
}
#endif
#endif /* EOS_DISPLAY_PROFILES_H */
