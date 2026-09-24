/**
 * @file cos_audio_decoder.h
 * @brief Audio decoder abstraction - pluggable format decoders
 */

#ifndef COS_AUDIO_DECODER_H
#define COS_AUDIO_DECODER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include "cos_error.h"

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/

typedef enum
{
    COS_AUDIO_SRC_NONE,
    COS_AUDIO_SRC_FILE, /**< File path string */
} cos_audio_src_type_t;

typedef struct
{
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t total_samples; /**< 0 if unknown (streaming) */
    uint32_t duration_ms; /**< 0 if unknown */
} cos_audio_format_t;

struct cos_audio_decoder_t;
struct cos_audio_decoder_dsc_t;

typedef cos_result_t (*cos_audio_decoder_probe_f_t)(const void *src,
                                                    cos_audio_src_type_t src_type,
                                                    cos_audio_format_t *format);

typedef cos_result_t (*cos_audio_decoder_open_f_t)(struct cos_audio_decoder_dsc_t *dsc);

typedef cos_result_t (*cos_audio_decoder_read_f_t)(struct cos_audio_decoder_dsc_t *dsc,
                                                   void *buf,
                                                   uint32_t buf_size,
                                                   uint32_t *bytes_read);

typedef void (*cos_audio_decoder_close_f_t)(struct cos_audio_decoder_dsc_t *dsc);

typedef cos_result_t (*cos_audio_decoder_seek_f_t)(struct cos_audio_decoder_dsc_t *dsc, uint32_t sample);

typedef struct cos_audio_decoder_dsc_t
{
    struct cos_audio_decoder_t *decoder;
    const void *src;
    cos_audio_src_type_t src_type;
    cos_audio_format_t format;
    uint32_t current_sample;
    void *user_data;
} cos_audio_decoder_dsc_t;

typedef struct cos_audio_decoder_t
{
    cos_audio_decoder_probe_f_t probe_cb;
    cos_audio_decoder_open_f_t open_cb;
    cos_audio_decoder_read_f_t read_cb;
    cos_audio_decoder_close_f_t close_cb;
    cos_audio_decoder_seek_f_t seek_cb;
    const char *name;
    void *user_data;
} cos_audio_decoder_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Initialize audio decoder subsystem and built-in decoders
 */
void cos_audio_decoder_init(void);

/**
 * @brief Detect the source type for an audio source
 * @param src Audio source (file path)
 * @return Source type
 */
cos_audio_src_type_t cos_audio_src_get_type(const void *src);

/**
 * @brief Create a new audio decoder instance and add to registry
 * @return cos_audio_decoder_t* Pointer to new decoder, or NULL on failure
 */
cos_audio_decoder_t *cos_audio_decoder_create(void);
void cos_audio_decoder_delete(cos_audio_decoder_t *decoder);
cos_audio_decoder_t *cos_audio_decoder_get_next(cos_audio_decoder_t *decoder);

void cos_audio_decoder_set_probe_cb(cos_audio_decoder_t *d, cos_audio_decoder_probe_f_t cb);
void cos_audio_decoder_set_open_cb(cos_audio_decoder_t *d, cos_audio_decoder_open_f_t cb);
void cos_audio_decoder_set_read_cb(cos_audio_decoder_t *d, cos_audio_decoder_read_f_t cb);
void cos_audio_decoder_set_close_cb(cos_audio_decoder_t *d, cos_audio_decoder_close_f_t cb);
void cos_audio_decoder_set_seek_cb(cos_audio_decoder_t *d, cos_audio_decoder_seek_f_t cb);

cos_result_t cos_audio_decoder_open(cos_audio_decoder_dsc_t *dsc, const void *src, cos_audio_src_type_t src_type);
cos_result_t cos_audio_decoder_read(cos_audio_decoder_dsc_t *dsc, void *buf, uint32_t buf_size, uint32_t *bytes_read);
cos_result_t cos_audio_decoder_seek(cos_audio_decoder_dsc_t *dsc, uint32_t sample);
void cos_audio_decoder_close(cos_audio_decoder_dsc_t *dsc);

#ifdef __cplusplus
}
#endif

#endif /* COS_AUDIO_DECODER_H */
