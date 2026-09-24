/**
 * @file eos_layout_square.c
 * @brief Grid / flex layout strategy for square & rectangular displays.
 */
#include "eos_layout_square.h"
#include "eos_mem.h"
#include <stdio.h>

static void _square_layout(eos_layout_manager_t *self,
                           const eos_display_profile_t *p,
                           eos_widget_t *widgets, int count)
{
    /* Auto-flow into a grid that fits the safe rectangle. Wider panels get an
     * extra column so more information can be shown (req #8 / #10). */
    int cols = (p->aspect_ratio >= 1.2f) ? 3 : 2;
    if (cols < 1) cols = 1;

    float pad = eos_dp(p, 8.0f);
    float cell_w = (p->safe_w - pad * (float)(cols + 1)) / (float)cols;
    int rows = (count + cols - 1) / cols;
    float cell_h = (rows > 0)
                 ? (p->safe_h - pad * (float)(rows + 1)) / (float)rows
                 : p->safe_h;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        eos_widget_t *w = &widgets[i];
        w->w = eos_dp(p, w->w_dp);
        w->h = eos_dp(p, w->h_dp);

        /* Fit the widget inside its grid cell (never overflow the safe rect).
         * NOTE: the widget must SHRINK to the cell size — clamping only the
         * center leaves the 48px card sticking out of a smaller cell (Round
         * 38e: launcher 12-app grid, cell_h=32.5 < 48 -> bottom row OUT). */
        float cw = (w->w < cell_w) ? w->w : cell_w;
        float ch = (w->h < cell_h) ? w->h : cell_h;
        w->w = cw;
        w->h = ch;
        float cx = p->safe_x + pad + (float)col * (cell_w + pad) + cell_w * 0.5f;
        float cy = p->safe_y + pad + (float)row * (cell_h + pad) + cell_h * 0.5f;
        float r = (cw > ch ? cw : ch) * 0.5f;
        eos_display_profile_clamp_inside(p, &cx, &cy, r);
        w->x = cx - cw * 0.5f;
        w->y = cy - ch * 0.5f;
        w->scale = 1.0f;
        w->opacity = 1.0f;
        w->visible = true;
    }
}

static void _square_destroy(eos_layout_manager_t *self)
{
    eos_free(self);
}

eos_layout_manager_t *eos_layout_square_create(void)
{
    eos_layout_manager_t *m =
        (eos_layout_manager_t *)eos_malloc(sizeof(eos_layout_manager_t));
    m->shape = EOS_DISPLAY_SHAPE_SQUARE;   /* also serves RECTANGLE */
    m->layout = _square_layout;
    m->destroy = _square_destroy;
    return m;
}
