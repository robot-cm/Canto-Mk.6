/**
 * @file cos_layout_storage.c
 * @brief Persist user layout + app order.
 */
#include "cos_layout_storage.h"
#include "cos_service_storage.h"
#include "cos_mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void cos_layout_storage_init(cos_layout_storage_t *ls, const char *default_style)
{
    memset(ls, 0, sizeof(*ls));
    const char *d = default_style ? default_style : "Bubble";
    strncpy(ls->style, d, COS_LAYOUT_STYLE_LEN - 1);
    ls->style[COS_LAYOUT_STYLE_LEN - 1] = '\0';
}

bool cos_layout_storage_save(const cos_layout_storage_t *ls,
                             const cos_app_manager_t *m)
{
    char buf[1024];
    int off = snprintf(buf, sizeof(buf), "style=%s\n", ls->style);
    off += snprintf(buf + off, sizeof(buf) - off, "order_count=%d\n", m->count);
    off += snprintf(buf + off, sizeof(buf) - off, "order=");
    for (int i = 0; i < m->count; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "%s%d", (i ? "," : ""), m->order[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "\n");

    cos_result_t r = cos_storage_write_file(COS_LAYOUT_FILE_PATH, buf, (size_t)off);
    return r == COS_OK;
}

bool cos_layout_storage_load(cos_layout_storage_t *ls,
                             cos_app_manager_t *m)
{
    char *data = cos_storage_read_file(COS_LAYOUT_FILE_PATH);
    if (!data) return false;

    char *save = NULL;
    char *line = strtok_r(data, "\n", &save);
    int order[COS_APP_MAX];
    int order_count = 0;

    while (line) {
        if (strncmp(line, "style=", 6) == 0) {
            strncpy(ls->style, line + 6, COS_LAYOUT_STYLE_LEN - 1);
            ls->style[COS_LAYOUT_STYLE_LEN - 1] = '\0';
        } else if (strncmp(line, "order_count=", 12) == 0) {
            order_count = atoi(line + 12);
        } else if (strncmp(line, "order=", 6) == 0) {
            char *tok_save = NULL;
            char *tok = strtok_r(line + 6, ",", &tok_save);
            int idx = 0;
            while (tok && idx < COS_APP_MAX) {
                order[idx++] = atoi(tok);
                tok = strtok_r(NULL, ",", &tok_save);
            }
            if (idx > order_count) order_count = idx;
        }
        line = strtok_r(NULL, "\n", &save);
    }

    if (order_count > 0 && order_count <= m->count) {
        for (int i = 0; i < order_count; i++) m->order[i] = order[i];
    }

    cos_free(data);
    return true;
}
