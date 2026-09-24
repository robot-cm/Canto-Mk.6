/**
 * @file cos_layout_square.h
 * @brief Grid / flex layout strategy for square & rectangular displays.
 */
#ifndef COS_LAYOUT_SQUARE_H
#define COS_LAYOUT_SQUARE_H

#include "cos_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create the square/rectangle (grid) LayoutManager. */
cos_layout_manager_t *cos_layout_square_create(void);

#ifdef __cplusplus
}
#endif
#endif /* COS_LAYOUT_SQUARE_H */
