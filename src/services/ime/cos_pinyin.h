/**
 * @file cos_pinyin.h
 * @brief Compact pinyin -> Chinese candidate engine (Core, LVGL-free).
 *
 * A small embedded pinyin input-method dictionary. Maps a pinyin syllable
 * (or short word) to one or more common Chinese characters / words. Intended
 * for resource-constrained devices (e.g. a 240x240 round display) where a
 * full pinyin IME with a complete dictionary is impractical.
 *
 * The lookup is exact (case-insensitive) on the typed pinyin string. The
 * on-screen keyboard buffers the user's pinyin and queries this module to
 * render candidate characters; the user then picks one.
 *
 * This module is intentionally free of any LVGL / UI dependency so it can be
 * unit-tested from the Core shell (`ime <pinyin>`) and reused by the keyboard
 * widget.
 */

#ifndef COS_PINYIN_H
#define COS_PINYIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Public typedefs --------------------------------------------*/

/**
 * @brief A single pinyin -> candidate mapping.
 */
typedef struct
{
    const char *py;   /**< pinyin syllable/word, lowercase ascii */
    const char *han;  /**< candidate Chinese character(s) (UTF-8) */
} cos_pinyin_entry_t;

/* Public function prototypes --------------------------------*/

/**
 * @brief Look up candidates for a pinyin string.
 * @param py      Pinyin syllable/word (case-insensitive). May be NULL.
 * @param out     Caller-provided array of const char* to receive candidates.
 * @param max_out Capacity of @p out.
 * @param out_count Filled with the number of candidates written (may exceed
 *                 @p max_out; in that case only the first @p max_out are stored).
 * @return The total number of candidates available for @p py (>= written count).
 */
int cos_pinyin_lookup(const char *py, const char **out, int max_out, int *out_count);

/**
 * @brief Total number of dictionary entries (for diagnostics).
 */
int cos_pinyin_dict_size(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_PINYIN_H */
