/**
 * @file cos_activity.h
 * @brief Activity controller
 */

#ifndef COS_ACTIVITY_H
#define COS_ACTIVITY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "cos_core.h"
#include "cos_lang.h"
/* Public macros ----------------------------------------------*/

#define COS_VIEW_SWITCH_DURATION 200

/* Public typedefs --------------------------------------------*/

typedef struct cos_activity_t cos_activity_t;

typedef enum
{
    COS_ACTIVITY_TYPE_NULL = 0,
    COS_ACTIVITY_TYPE_APP,
    COS_ACTIVITY_TYPE_INPUT_PAGE,
    COS_ACTIVITY_TYPE_APP_LIST,
    COS_ACTIVITY_TYPE_WATCHFACE,
    COS_ACTIVITY_TYPE_WATCHFACE_LIST,
    COS_ACTIVITY_TYPE_LOCK_SCREEN,
    COS_ACTIVITY_TYPE_COUNT
} cos_activity_type_t;

typedef void (*cos_activity_on_enter_t)(cos_activity_t *activity);
typedef void (*cos_activity_on_destroy_t)(cos_activity_t *activity);
typedef void (*cos_activity_on_pause_t)(cos_activity_t *activity);
typedef void (*cos_activity_on_resume_t)(cos_activity_t *activity);
typedef bool (*cos_activity_on_swipe_back_t)(cos_activity_t *self, lv_dir_t dir);
typedef void (*cos_activity_anim_cb_t)(lv_anim_timeline_t *at, cos_activity_t *this, cos_activity_t *next);

typedef struct
{
    cos_activity_on_enter_t on_enter;
    cos_activity_on_destroy_t on_destroy;
    cos_activity_on_pause_t on_pause;
    cos_activity_on_resume_t on_resume;
    cos_activity_on_swipe_back_t on_swipe_back;
} cos_activity_lifecycle_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize Activity controller with root Activity
 * @param root_activity Root Activity (e.g., watchface), cannot be NULL
 * @return cos_result_t COS_OK success, COS_FAILED failed
 * @note The root Activity is managed separately from the stack and will not be destroyed by back()
 * @note The root Activity should be created with cos_activity_create_root() which auto-creates the view
 */
cos_result_t cos_activity_controller_init(cos_activity_t *root_activity);

/**
 * @brief Create an Activity
 * @return cos_activity_t* Returns Activity pointer on success, NULL on failure
 */
cos_activity_t *cos_activity_create(const cos_activity_lifecycle_t *lifecycle);

/**
 * @brief Create a root Activity (watchface) with immediate view creation
 * @param lifecycle Activity lifecycle callbacks
 * @return cos_activity_t* Returns Activity pointer on success, NULL on failure
 *
 * Differences from cos_activity_create():
 * - Always creates view immediately with standard style (no lazy creation)
 * - Designed for root activities (watchfaces) that need custom content
 * - Auto-creates root_screen if not exists (lazy initialization)
 *
 * Usage:
 * 1. Create: cos_activity_create_root(&lifecycle)  // view created immediately
 * 2. Initialize: cos_activity_controller_init(root_activity)
 *
 * Note: Can be called before or after cos_activity_controller_init()
 */
cos_activity_t *cos_activity_create_root(const cos_activity_lifecycle_t *lifecycle);

/**
 * @brief Get Activity user data
 * @param activity Activity pointer
 * @return void* User data pointer, returns NULL on failure
 */
void *cos_activity_get_user_data(cos_activity_t *activity);

/**
 * @brief Set Activity user data
 * @param activity Activity pointer
 * @param user_data User data pointer
 */
void cos_activity_set_user_data(cos_activity_t *activity, void *user_data);

/**
 * @brief Register a swipe-back handler for this activity's view.
 *        Triggered on a swipe-back gesture (e.g. left-swipe). Return true to
 *        indicate the activity consumed the gesture (framework will NOT pop
 *        the activity); return false to let the framework go back.
 * @note If no handler is registered, the framework always goes back on a
 *       swipe-back gesture.
 */
void cos_activity_set_swipe_back_handler(cos_activity_t *activity, cos_activity_on_swipe_back_t cb);

/**
 * @brief Set fault panel for activity (internal use)
 * @param activity Activity pointer
 * @param fault_panel Fault panel pointer
 */
void cos_activity_set_fault_panel(cos_activity_t *activity, void *fault_panel);

/**
 * @brief Get fault panel from activity
 * @param activity Activity pointer
 * @return void* Fault panel pointer, NULL if not set
 */
void *cos_activity_get_fault_panel(cos_activity_t *activity);

/**
 * @brief Get Activity title
 * @param activity Activity pointer
 * @return const char* Title string, returns NULL on failure
 */
const char *cos_activity_get_title(cos_activity_t *activity);

/**
 * @brief Set Activity title
 * @param activity Activity pointer
 * @param title Title string
 */
void cos_activity_set_title(cos_activity_t *activity, const char *title);

/**
 * @brief Set Activity title
 * @param activity Activity pointer
 * @param id Title string ID
 */
void cos_activity_set_title_id(cos_activity_t *activity, lang_string_id_t id);

/**
 * @brief Get Activity title color
 * @param activity Activity pointer
 * @return lv_color_t Title color
 */
lv_color_t cos_activity_get_title_color(cos_activity_t *activity);

/**
 * @brief Set Activity title color
 * @param activity Activity pointer
 * @param color Title color
 */
void cos_activity_set_title_color(cos_activity_t *activity, lv_color_t color);

/**
 * @brief Set Activity page type
 * @param activity Activity pointer
 * @param type Page type
 */
void cos_activity_set_type(cos_activity_t *activity, cos_activity_type_t type);

/**
 * @brief Get Activity page type
 * @param activity Activity pointer
 * @return cos_activity_type_t Page type
 */
cos_activity_type_t cos_activity_get_type(cos_activity_t *activity);

/**
 * @brief Register page transition animation route
 * @param from_type Source page type
 * @param to_type Target page type
 * @param cb Animation callback
 * @return cos_result_t COS_OK success, COS_FAILED failed
 */
cos_result_t cos_activity_register_anim_route(cos_activity_type_t from_type,
                                              cos_activity_type_t to_type,
                                              cos_activity_anim_cb_t cb);

/**
 * @brief Query page transition animation route
 * @param from_type Source page type
 * @param to_type Target page type
 * @return cos_activity_anim_cb_t Animation callback, returns NULL if not found
 */
cos_activity_anim_cb_t cos_activity_get_anim_route(cos_activity_type_t from_type, cos_activity_type_t to_type);

/**
 * @brief Set Activity title visibility
 * @param activity Activity pointer
 * @param visible Whether visible
 */
void cos_activity_set_app_header_visible(cos_activity_t *activity, bool visible);

/**
 * @brief Set Activity title visibility with animation
 * @param activity Activity pointer
 * @param visible Whether visible
 * @param duration_ms Animation duration (milliseconds), switches immediately when 0
 */
void cos_activity_set_app_header_visible_animated(cos_activity_t *activity, bool visible, uint32_t duration_ms);

/**
 * @brief Check if Activity title is visible
 * @param activity Activity pointer
 * @return bool true visible, false not visible
 */
bool cos_activity_is_app_header_visible(cos_activity_t *activity);

/**
 * @brief Set whether Activity AppHeader shows only time label
 * @param activity Activity pointer
 * @param time_only true shows only time, false shows full AppHeader
 */
void cos_activity_set_app_header_time_only(cos_activity_t *activity, bool time_only);

/**
 * @brief Check if Activity AppHeader shows only time label
 * @param activity Activity pointer
 * @return bool true shows only time, false shows full AppHeader
 */
bool cos_activity_is_app_header_time_only(cos_activity_t *activity);

/**
 * @brief Set Activity time font color in AppHeader time-only mode
 * @param activity Activity pointer
 * @param color Time font color
 */
void cos_activity_set_app_header_time_only_text_color(cos_activity_t *activity, lv_color_t color);

/**
 * @brief Get Activity time font color in AppHeader time-only mode
 * @param activity Activity pointer
 * @return lv_color_t Time font color
 */
lv_color_t cos_activity_get_app_header_time_only_text_color(cos_activity_t *activity);

/**
 * @brief Get Activity corresponding View
 * @param activity Activity pointer
 * @return lv_obj_t* View object, returns NULL on failure
 */
lv_obj_t *cos_activity_get_view(cos_activity_t *activity);

/**
 * @brief Set Activity View (internal use only)
 * @param activity Activity pointer
 * @param view View object
 * @note This function should only be called inside on_enter callback.
 *       External modification of Activity's view is prohibited to prevent view loss.
 */
void cos_activity_set_view(cos_activity_t *activity, lv_obj_t *view);

/**
 * @brief Get root Screen
 * @return lv_obj_t* Root Screen object, returns NULL on failure
 */
lv_obj_t *cos_activity_get_root_screen(void);

/**
 * @brief Replace the root Activity (e.g., switch watchface)
 * @param new_root New root Activity to set
 * @return cos_result_t COS_OK success, COS_FAILED failed
 *
 * Lifecycle:
 * 1. If old root exists, call its on_destroy()
 * 2. Set new root as current root
 * 3. Call new root's on_enter()
 * 4. Display new root's view
 *
 * @note The old root Activity is destroyed and should not be used after this call.
 *       The new root Activity must have been created with cos_activity_create().
 */
cos_result_t cos_activity_replace_root(cos_activity_t *new_root);

/**
 * @brief Get current root Activity
 * @return cos_activity_t* Root Activity pointer, returns NULL if not initialized
 */
cos_activity_t *cos_activity_get_root(void);

/**
 * @brief Get Activity view snapshot
 * @param activity Activity pointer
 * @param include_header Whether to include Header (only valid when current Activity and Header is visible)
 * @return lv_obj_t* Snapshot image object (lv_image), returns NULL on failure
 * @note Image resources are automatically released when the object is deleted
 */
lv_obj_t *cos_activity_take_snapshot(cos_activity_t *activity, bool include_header);

/**
 * @brief Get Watchface Activity (deprecated, use cos_activity_get_root instead)
 * @return cos_activity_t* Watchface (root) Activity pointer, returns NULL on failure
 * @deprecated This function is kept for backward compatibility. Use cos_activity_get_root() instead.
 */
cos_activity_t *cos_activity_get_watchface(void);

/**
 * @brief Get current Activity View
 * @return lv_obj_t* Current Activity View object, returns NULL on failure
 */
lv_obj_t *cos_view_active(void);

/**
 * @brief Enter specified Activity
 * @param activity Activity pointer
 */
void cos_activity_enter(cos_activity_t *activity);

/**
 * @brief Return to previous Activity and destroy current Activity
 * @return cos_result_t COS_OK success, COS_FAILED failed
 */
cos_result_t cos_activity_back(void);

/**
 * @brief Return directly to root Activity and clear all stacked Activities
 * @return cos_result_t COS_OK success, COS_FAILED failed
 * @note This function destroys all Activities in the stack and returns to the root Activity.
 *       The root Activity's on_resume() will be called.
 */
cos_result_t cos_activity_back_to_watchface(void);

/**
 * @brief Mark whether this Activity stays alive (keeps its view + user data +
 *        running program) when the user navigates back, instead of being destroyed.
 *
 * This enables "Background App" semantics: a keep-alive activity is popped from
 * the stack but NOT destroyed on back / back-to-watchface; its on_pause() is
 * called and its view is hidden. The caller (e.g. app layer) is responsible for
 * suspending the app program inside on_pause() and registering it in the
 * Background App Indicator.
 *
 * @param activity Activity to mark
 * @param keep_alive true to keep alive on back, false to destroy normally
 */
void cos_activity_set_keep_alive_on_back(cos_activity_t *activity, bool keep_alive);

/**
 * @brief Query whether an activity is marked keep-alive on back
 * @param activity Activity
 * @return true if keep-alive
 */
bool cos_activity_is_keep_alive_on_back(cos_activity_t *activity);

/**
 * @brief Restore a keep-alive (backgrounded) Activity back to the foreground.
 *
 * Pushes the activity onto the stack and switches to it, which triggers its
 * on_resume() (used to resume the suspended app program). The caller must have
 * removed the activity from the Background App Indicator beforehand if desired.
 *
 * @param activity A keep-alive activity that is currently backgrounded
 * @return cos_result_t COS_OK success, COS_FAILED failed
 */
cos_result_t cos_activity_restore_background(cos_activity_t *activity);

/**
 * @brief Wrapper for returning to previous Activity and destroying current Activity
 * @param e Event object
 */
void cos_activity_back_cb(lv_event_t *e);

/**
 * @brief Get current Activity
 * @return cos_activity_t* Current Activity, returns NULL on failure
 */
cos_activity_t *cos_activity_get_current(void);

/**
 * @brief Get current completed display Activity
 * @return cos_activity_t* Completed display Activity, returns NULL on failure
 */
cos_activity_t *cos_activity_get_visible(void);

/**
 * @brief Get previous Activity (used in event callbacks to get the source page)
 * @return cos_activity_t* Previous Activity, returns NULL on failure
 */
cos_activity_t *cos_activity_get_previous(void);

/**
 * @brief Check if Activity transition animation is in progress
 * @return bool true transitioning, false idle
 */
bool cos_activity_is_transition_in_progress(void);

/**
 * @brief Get bottom Activity in stack (or root Activity if stack is empty)
 * @return cos_activity_t* Bottom Activity in stack, or root Activity if stack empty, returns NULL on failure
 */
cos_activity_t *cos_activity_get_bottom(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_ACTIVITY_H */
