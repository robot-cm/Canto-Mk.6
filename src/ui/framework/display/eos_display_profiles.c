/**
 * @file eos_display_profiles.c
 * @brief The four reference profiles required for testing (req #12).
 */
#include "eos_display_profiles.h"

eos_display_profile_t eos_display_profiles_get(eos_profile_id_t id)
{
    switch (id) {
        case EOS_PROFILE_320C:
            return eos_display_profile_create(EOS_DISPLAY_SHAPE_CIRCLE, 320, 320);
        case EOS_PROFILE_240S:
            return eos_display_profile_create(EOS_DISPLAY_SHAPE_SQUARE, 240, 280);
        case EOS_PROFILE_240R:
            return eos_display_profile_create(EOS_DISPLAY_SHAPE_RECTANGLE, 240, 320);
        case EOS_PROFILE_240C:
        default:
            return eos_display_profile_create(EOS_DISPLAY_SHAPE_CIRCLE, 240, 240);
    }
}

const char *eos_display_profiles_name(eos_profile_id_t id)
{
    switch (id) {
        case EOS_PROFILE_240C: return "CIRCLE 240x240";
        case EOS_PROFILE_320C: return "CIRCLE 320x320";
        case EOS_PROFILE_240S: return "SQUARE 240x280";
        case EOS_PROFILE_240R: return "RECTANGLE 240x320";
        default:               return "?";
    }
}
