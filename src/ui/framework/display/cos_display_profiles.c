/**
 * @file cos_display_profiles.c
 * @brief The four reference profiles required for testing (req #12).
 */
#include "cos_display_profiles.h"

cos_display_profile_t cos_display_profiles_get(cos_profile_id_t id)
{
    switch (id) {
        case COS_PROFILE_320C:
            return cos_display_profile_create(COS_DISPLAY_SHAPE_CIRCLE, 320, 320);
        case COS_PROFILE_240S:
            return cos_display_profile_create(COS_DISPLAY_SHAPE_SQUARE, 240, 280);
        case COS_PROFILE_240R:
            return cos_display_profile_create(COS_DISPLAY_SHAPE_RECTANGLE, 240, 320);
        case COS_PROFILE_240C:
        default:
            return cos_display_profile_create(COS_DISPLAY_SHAPE_CIRCLE, 240, 240);
    }
}

const char *cos_display_profiles_name(cos_profile_id_t id)
{
    switch (id) {
        case COS_PROFILE_240C: return "CIRCLE 240x240";
        case COS_PROFILE_320C: return "CIRCLE 320x320";
        case COS_PROFILE_240S: return "SQUARE 240x280";
        case COS_PROFILE_240R: return "RECTANGLE 240x320";
        default:               return "?";
    }
}
