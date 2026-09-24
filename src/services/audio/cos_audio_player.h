/**
 * @file cos_audio_player.h
 * @brief Audio player - orchestrates decoder to speaker pipeline
 */
#ifndef COS_AUDIO_PLAYER_H
#define COS_AUDIO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_audio_decoder.h"
#include "cos_error.h"

/* Forward declarations ---------------------------------------*/
struct cos_audio_feed;

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef enum
{
    COS_AUDIO_IDLE,
    COS_AUDIO_PLAYING,
    COS_AUDIO_PAUSED,
} cos_audio_player_state_t;

typedef struct cos_audio_player_t cos_audio_player_t;

/** Called when playback completes naturally (not on user stop/pause). */
typedef void (*cos_audio_player_done_cb)(cos_audio_player_t *player, void *user_data);

/**
 * @brief Audio player instance.
 * Initialize with cos_audio_player_init() before use.
 */
struct cos_audio_player_t
{
    cos_audio_player_state_t state;
    cos_audio_decoder_dsc_t dsc;
    bool decoder_open;
    struct cos_audio_feed *feed;
    uint8_t volume;
    bool muted;
    void *cached_src;
    cos_audio_src_type_t cached_src_type;
    cos_audio_player_done_cb done_cb;
    void *done_user_data;
};

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize a player instance. Must be called before use.
 * @param p Pointer to player instance (caller-allocated)
 */
void cos_audio_player_init(cos_audio_player_t *p);

cos_result_t cos_audio_player_play(cos_audio_player_t *player, const void *src, cos_audio_src_type_t src_type);
cos_result_t cos_audio_player_stop(cos_audio_player_t *player);
cos_result_t cos_audio_player_pause(cos_audio_player_t *player);
cos_result_t cos_audio_player_resume(cos_audio_player_t *player);
cos_result_t cos_audio_player_seek(cos_audio_player_t *player, uint32_t sample);

cos_audio_player_state_t cos_audio_player_get_state(cos_audio_player_t *player);
uint32_t cos_audio_player_get_position(cos_audio_player_t *player);
uint32_t cos_audio_player_get_duration(cos_audio_player_t *player);
uint32_t cos_audio_player_get_sample_rate(cos_audio_player_t *player);
cos_result_t cos_audio_player_set_volume(cos_audio_player_t *player, uint8_t vol);
void cos_audio_player_set_mute(cos_audio_player_t *player, bool mute);
bool cos_audio_player_is_muted(cos_audio_player_t *player);

/**
 * @brief Apply player's internal volume to the speaker device.
 * Call this mid-playback after set_volume() to push volume to hardware.
 */
void cos_audio_player_apply_volume(cos_audio_player_t *player);

/**
 * @brief Set a callback invoked when playback reaches end naturally.
 * Not called on explicit stop() or pause().
 */
void cos_audio_player_set_done_callback(cos_audio_player_t *player, cos_audio_player_done_cb cb, void *user_data);

/**
 * @brief Save playback state for later restoration (interrupt/resume pattern).
 * Transfers ownership of cached_src to caller; player forgets it.
 * @param[out] saved_src   Source pointer (caller must manage lifetime)
 * @param[out] saved_type  Source type
 * @param[out] saved_pos   Current sample position
 */
void cos_audio_player_save_state(cos_audio_player_t *player,
                                 void **saved_src,
                                 cos_audio_src_type_t *saved_type,
                                 uint32_t *saved_pos);

/**
 * @brief Restore playback from a previously saved state.
 * Player must be in IDLE state.
 */
cos_result_t cos_audio_player_restore_state(cos_audio_player_t *player,
                                            void *src,
                                            cos_audio_src_type_t src_type,
                                            uint32_t position);

#ifdef __cplusplus
}
#endif

#endif /* COS_AUDIO_PLAYER_H */
