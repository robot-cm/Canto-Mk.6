/**
 * @file eos_theme_manager.h
 * @brief Theme manager — Liquid Glass theme params + apply (req #5/#6).
 *
 * Holds the visual theme (background alpha, accent, radius, glow, glass flag)
 * and can apply it to any LVGL object. Serialisable for persistence.
 */
#ifndef EOS_THEME_MANAGER_H
#define EOS_THEME_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_obj_t lv_obj_t;   /* forward decl — no lvgl in header */

typedef struct {
    char    name[24];
    uint8_t bg_alpha;     /**< glass background opacity 0..255        */
    uint32_t accent;      /**< accent color (0xRRGGBB)                */
    uint16_t radius;      /**< card corner radius (px)                */
    uint8_t glow;         /**< subtle glow / shadow strength 0..255   */
    bool    glass;        /**< use liquid-glass look                  */
} eos_theme_t;

void eos_theme_manager_init(eos_theme_t *t);
void eos_theme_manager_set(eos_theme_t *t, const char *name,
                           uint8_t bg_alpha, uint32_t accent,
                           uint16_t radius, uint8_t glow, bool glass);

/** @brief Apply the theme to an object (NULL-safe). is_card => card styling. */
void eos_theme_manager_apply(const eos_theme_t *t, lv_obj_t *obj, bool is_card);

/** @brief Serialise/deserialise to a text buffer (for persistence). */
int  eos_theme_manager_serialize(const eos_theme_t *t, char *buf, int buflen);
bool eos_theme_manager_deserialize(eos_theme_t *t, const char *buf);

#ifdef __cplusplus
}
#endif
#endif /* EOS_THEME_MANAGER_H */
