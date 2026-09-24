/**
 * @file cos_anim.h
 * @brief Animation library
 * @details
 *
 * # Animation System
 *
 * An animation library wrapped based on LVGL animation, providing unified and easy-to-use animation interfaces for Canto Mk.6.
 * This library summarizes common animation effects and standardizes them, representing them with the enum type `cos_anim`,
 * facilitating consistent creation and usage of standard animations throughout the system.
 *
 * ## Usage
 *
 * ### Create different types of animations using dedicated functions, such as scale animation and fade animation:
 *
 * ```c
 * cos_anim_t *anim = cos_anim_scale_create(obj, w_start, w_end, h_start, h_end, duration);
 * cos_anim_t *fade_anim = cos_anim_fade_create(obj, opa_start, opa_end, duration);
 * ```
 *
 * ### Callbacks triggered when animation playback completes:
 *
 * ```c
 * cos_anim_add_cb(anim, user_cb, user_data);
 * ```
 *
 * ### Start animation
 *
 * ```c
 * cos_anim_start(anim);
 * ```
 *
 * ### Shortcuts (create and play directly)
 *
 * ```c
 * cos_anim_scale_start(obj, w_start, w_end, h_start, h_end, duration);
 * ```
 *
 *  - No need to manually manage animation objects.
 *  - Automatically released after completion.
 *
 * ### Delete animation
 *
 * If you want to stop early or manually clean up the animation:
 *
 * ```c
 * cos_anim_del(anim);
 * ```
 *
 * ## Lite Animation
 *
 * Lite Animation is a lightweight wrapper based on the LVGL animation system,
 * providing a unified and concise interface for creating single-type LVGL animations.
 * Each lite animation generates and starts immediately after being called,
 * suitable for quick-use simple transition effects within scenes.
 *
 * >[!NOTE] The playback completion callback for lite animations differs from the standard animation library.
 * Lite animations directly use LVGL default callback functions.
 *
 * ## Notes
 *
 *  - Animation object cos_anim_t will be automatically released.
 *  - Supports multiple animations in parallel, managed by anim_count for sub-animation completion.
 *  - Extensible for different types of animations (scale, opacity, size, position, etc.).
 *  - Multiple animations can start simultaneously, but same type animations may have race conditions.
 */

#ifndef COS_ANIM_H
#define COS_ANIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

/**
 * @brief All animation types, can continue to add more.
 */
typedef enum
{
    COS_ANIM_SCALE, /**< Scale animation */
    COS_ANIM_FADE, /**< Opacity fade animation */
    COS_ANIM_MOVE, /**< Position move animation */
    COS_ANIM_TRANSFORM_SCALE /**< Scale animation, supports Label */
    // More animation types can be added here
} cos_anim;
typedef struct cos_anim_t cos_anim_t; // Forward declaration
/**
 * @brief Callback function type definition
 */
typedef void (*cos_anim_cb_t)(cos_anim_t *a);
/**
 * @brief Canto Mk.6 animation object structure
 */
struct cos_anim_t
{
    lv_anim_timeline_t *anim_timeline; /**< Animation timeline pointer */
    cos_anim type; /**< Animation type */
    uint32_t anim_count; /**< Total animation count for this type */
    uint32_t
        anim_completed_count; /**< Current completed animation count (used to determine if all animations are finished) */
    cos_anim_cb_t user_cb; /**< User-defined callback function */
    lv_obj_t *tar_obj;
    bool auto_delete_obj; /**< Automatically delete bound object when animation completes */
    void *user_data; /**< User data */
    union
    { /**< Union for storing animation objects */
        struct
        {
            lv_anim_t a_width; /**< Scale animation width animation object */
            lv_anim_t a_height; /**< Scale animation height animation object */
        } scale;
        struct
        {
            lv_anim_t a_opa;
        } fade;
        struct
        {
            lv_anim_t a_x; /**< X-axis position animation */
            lv_anim_t a_y; /**< Y-axis position animation */
        } move;
        struct
        {
            lv_anim_t a_scale;
        } transform_scale;
        // Other animation type structures can be added here
    } anim;
    union
    { /**< Union for storing animation objects */
        struct
        {
            bool layered; /**< Whether to adjust opacity with child objects */
        } fade;
        struct
        {
            bool disable_x; /**< Whether to adjust opacity with child objects */
            bool disable_y; /**< Whether to adjust opacity with child objects */
        } move;
        // Other animation type structures can be added here
    } cfg;
};
/* Public function prototypes --------------------------------*/

/************************** Common **************************/

/**
 * @brief Automatically delete `tar_obj` when animation finishes
 * @param anim Target animation
 */
void cos_anim_set_auto_delete(cos_anim_t *anim);

/**
 * @brief Start playing animation
 * @param anim Animation object created by create function
 * @return Returns true on success, false on failure
 */
bool cos_anim_start(cos_anim_t *anim);

/**
 * @brief Set callback for animation completion
 * @param anim Animation to set
 * @param user_cb Callback function
 * @param user_data User data pointer (user is responsible for managing lifecycle)
 */
void cos_anim_add_cb(cos_anim_t *anim, cos_anim_cb_t user_cb, void *user_data);

/**
 * @brief Get animation object's user data
 */
void *cos_anim_get_user_data(cos_anim_t *anim);
/**
 * @brief Delete animation object
 * @param anim Animation object pointer
 * @note Will automatically stop if animation is running
 */
void cos_anim_del(cos_anim_t *anim);
/**
 * @brief Add transparent blocker layer to disable user input
 */
void cos_anim_blocker_show(void);
/**
 * @brief Remove transparent blocker layer
 */
void cos_anim_blocker_hide(void);

/************************** Animation **************************/

/**
 * @brief Create scale animation object
 * @param tar_obj Target object
 * @param w_start Start width
 * @param w_end End width
 * @param h_start Start height
 * @param h_end End height
 * @param duration Duration (ms)
 * @return Created animation object pointer, returns NULL on failure
 */
cos_anim_t *cos_anim_scale_create(lv_obj_t *tar_obj,
                                  int32_t w_start,
                                  int32_t w_end,
                                  int32_t h_start,
                                  int32_t h_end,
                                  uint32_t duration,
                                  bool auto_delete);

/**
 * @brief Create and immediately play scale animation, cannot set callback
 * @note Animation will be automatically deleted after completion
 */
void cos_anim_scale_start(lv_obj_t *tar_obj,
                          int32_t w_start,
                          int32_t w_end,
                          int32_t h_start,
                          int32_t h_end,
                          uint32_t duration,
                          bool auto_delete);

/**
 * @brief Create opacity fade animation
 */
cos_anim_t *cos_anim_fade_create(lv_obj_t *tar_obj,
                                 int32_t opa_start,
                                 int32_t opa_end,
                                 uint32_t duration,
                                 bool auto_delete);
/**
 * @brief Create and immediately play opacity fade animation
 */
void cos_anim_fade_start(lv_obj_t *tar_obj, int32_t opa_start, int32_t opa_end, uint32_t duration, bool auto_delete);

/**
 * @brief Create and return a move animation object (position from start_x,start_y -> end_x,end_y)
 */
cos_anim_t *cos_anim_move_create(lv_obj_t *tar_obj,
                                 int32_t start_x,
                                 int32_t start_y,
                                 int32_t end_x,
                                 int32_t end_y,
                                 uint32_t duration,
                                 bool auto_delete);

/**
 * @brief Create and immediately play move animation
 */
void cos_anim_move_start(lv_obj_t *tar_obj,
                         int32_t start_x,
                         int32_t start_y,
                         int32_t end_x,
                         int32_t end_y,
                         uint32_t duration,
                         bool auto_delete);

/**
 * @brief Whether Fade animation adjusts opacity by layer
 */
void cos_anim_fade_set_layered(cos_anim_t *a, bool layered);

/**
 * @brief Create transform scale animation (based on transform_scale)
 * @note Scaling is overall scaling, i.e., width and height scale simultaneously
 */
cos_anim_t *cos_anim_transform_scale_create(lv_obj_t *tar_obj,
                                            int32_t scale_start,
                                            int32_t scale_end,
                                            uint32_t duration,
                                            bool auto_delete);

/**
 * @brief Start transform scale animation (with advanced configuration)
 */
void cos_anim_transform_scale_start_ex(lv_obj_t *tar_obj,
                                       int32_t scale_start,
                                       int32_t scale_end,
                                       uint32_t duration,
                                       uint32_t playback_time,
                                       uint16_t repeat_count,
                                       bool auto_delete);

/**
 * @brief Start simple transform scale animation (default parameters)
 */
void cos_anim_transform_scale_start(lv_obj_t *tar_obj,
                                    int32_t scale_start,
                                    int32_t scale_end,
                                    uint32_t duration,
                                    bool auto_delete);

/************************** Lite Animation **************************/

/**
 * @brief Create horizontal move animation
 * @param target_obj Target object
 * @param start Start X coordinate
 * @param end End X coordinate
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_move_hor_start(lv_obj_t *target_obj,
                                  int32_t start,
                                  int32_t end,
                                  uint32_t duration,
                                  uint32_t delay,
                                  lv_anim_completed_cb_t completed_cb,
                                  void *user_data);

/**
 * @brief Create vertical move animation
 * @param target_obj Target object
 * @param start Start Y coordinate
 * @param end End Y coordinate
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_move_ver_start(lv_obj_t *target_obj,
                                  int32_t start,
                                  int32_t end,
                                  uint32_t duration,
                                  uint32_t delay,
                                  lv_anim_completed_cb_t completed_cb,
                                  void *user_data);

/**
 * @brief Create width scale animation
 * @param target_obj Target object
 * @param start Start width
 * @param end End width
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_scale_w_start(lv_obj_t *target_obj,
                                 int32_t start,
                                 int32_t end,
                                 uint32_t duration,
                                 uint32_t delay,
                                 lv_anim_completed_cb_t completed_cb,
                                 void *user_data);

/**
 * @brief Create height scale animation
 * @param target_obj Target object
 * @param start Start height
 * @param end End height
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_scale_h_start(lv_obj_t *target_obj,
                                 int32_t start,
                                 int32_t end,
                                 uint32_t duration,
                                 uint32_t delay,
                                 lv_anim_completed_cb_t completed_cb,
                                 void *user_data);

/**
 * @brief Create transform scale animation
 * @param target_obj Target object
 * @param start Start scale value
 * @param end End scale value
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_transform_scale_start(lv_obj_t *target_obj,
                                         int32_t start,
                                         int32_t end,
                                         uint32_t duration,
                                         uint32_t delay,
                                         lv_anim_completed_cb_t completed_cb,
                                         void *user_data);

/**
 * @brief Create fade animation
 * @param target_obj Target object
 * @param start Start opacity (0-255), use `LV_OPA_*`
 * @param end End opacity (0-255), use `LV_OPA_*`
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_fade_start(lv_obj_t *target_obj,
                              int32_t start,
                              int32_t end,
                              uint32_t duration,
                              uint32_t delay,
                              lv_anim_completed_cb_t completed_cb,
                              void *user_data);

/**
 * @brief Create fade animation
 * @param target_obj Target object
 * @param start Start opacity (0-255), use `LV_OPA_*`
 * @param end End opacity (0-255), use `LV_OPA_*`
 * @param duration Animation duration (ms)
 * @param delay Animation delay (ms)
 * @param completed_cb Animation completion callback, can be NULL
 * @param user_data User data bound to `lv_anim_t`, can be NULL
 */
void cos_lite_anim_fade_layered_start(lv_obj_t *target_obj,
                                      int32_t start,
                                      int32_t end,
                                      uint32_t duration,
                                      uint32_t delay,
                                      lv_anim_completed_cb_t completed_cb,
                                      void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* COS_ANIM_H */
