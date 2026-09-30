/**
 * @file eos_layout_square.h
 * @brief Grid / flex layout strategy for square & rectangular displays.
 */
#ifndef EOS_LAYOUT_SQUARE_H
#define EOS_LAYOUT_SQUARE_H

#include "eos_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create the square/rectangle (grid) LayoutManager. */
eos_layout_manager_t *eos_layout_square_create(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_LAYOUT_SQUARE_H */
