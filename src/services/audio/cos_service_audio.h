/**
 * @file cos_service_audio.h
 * @brief Audio service - high-level API for speaker and microphone
 */

#ifndef COS_SERVICE_AUDIO_H
#define COS_SERVICE_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"
#include "cos_audio_player.h"

/* Public macros ----------------------------------------------*/

#define COS_SPEAKER_VOLUME_MIN 0
#define COS_SPEAKER_VOLUME_MAX 100

/* Public typedefs --------------------------------------------*/

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize audio service
 */
void cos_service_audio_init(void);

/**
 * @brief Set speaker volume and persist to config
 * @param volume Volume level (0-100)
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_set_volume(uint8_t volume);

/**
 * @brief Get current volume from config
 * @return Volume level (0-100), default 50
 */
uint8_t cos_service_audio_get_volume(void);

/**
 * @brief Enable or disable mute
 * @param mute true to mute, false to unmute
 */
void cos_service_audio_set_mute(bool mute);

/**
 * @brief Check if audio is muted
 * @return true if muted
 */
bool cos_service_audio_is_muted(void);

/**
 * @brief Play an audio file
 * @param file_path Path to audio file (format handled by port layer)
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_play(const char *file_path);

/**
 * @brief Play a tone at given frequency and duration
 * @param freq Frequency in Hz
 * @param duration_ms Duration in milliseconds
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_play_tone(uint16_t freq, uint32_t duration_ms);

/**
 * @brief Stop current playback
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_stop(void);

/**
 * @brief Pause current playback (keeps queue alive for resume)
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_pause(void);

/**
 * @brief Resume paused playback
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_resume(void);

/**
 * @brief Start recording from microphone
 * @param file_path Path to save recorded audio
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_start_recording(const char *file_path);

/**
 * @brief Stop current recording
 * @return COS_OK if successful
 */
cos_result_t cos_service_audio_stop_recording(void);

/**
 * @brief Check if speaker hardware is available
 * @return true if available
 */
bool cos_service_audio_speaker_available(void);

/**
 * @brief Check if microphone hardware is available
 * @return true if available
 */
bool cos_service_audio_microphone_available(void);

/**
 * @brief Get the service-managed player instance for advanced queries.
 * For normal playback use the service play/stop/pause/resume functions.
 * @return Pointer to the media channel player (never NULL after init).
 */
cos_audio_player_t *cos_service_audio_get_player(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SERVICE_AUDIO_H */
