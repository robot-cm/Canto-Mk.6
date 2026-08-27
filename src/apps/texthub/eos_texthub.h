/**
 * @file eos_texthub.h
 * @brief Texthub - native C text reader (.txt/.md) for /sdcard/texthub/
 *
 * Replaces the old JS app (apps/texthub) which OOM-crashed the JerryScript
 * 512KB heap by synchronously listing tens of thousands of files. The C
 * version is a native app registered like Album:
 *   - Lazy File Manager overlay: a directory is read only when expanded,
 *     children are freed when collapsed / on close.
 *   - Chunked reading: files <=128KB load at once, larger ones are read
 *     segment-by-segment (16KB, line-aligned) as the user scrolls.
 *   - History: last opened file path in /sdcard/history/texthub/latest.txt,
 *     auto-opened on next launch (File Manager shown if no record).
 *   - UI (240x240 round): [FM] [scrolling filename] [>], text card, bottom
 *     "idx/total" indicator. English only.
 */

#ifndef EOS_TEXTHUB_H
#define EOS_TEXTHUB_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter the Texthub app (native app entry, called by App List).
 */
void eos_texthub_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_TEXTHUB_H */
