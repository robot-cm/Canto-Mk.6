/**
 * @file eos_layout_storage.c
 * @brief Persist user layout + app order.
 */
#include "eos_layout_storage.h"
#include "eos_service_storage.h"
#include "eos_mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void eos_layout_storage_init(eos_layout_storage_t *ls, const char *default_style)
{
    memset(ls, 0, sizeof(*ls));
    const char *d = default_style ? default_style : "Bubble";
    strncpy(ls->style, d, EOS_LAYOUT_STYLE_LEN - 1);
    ls->style[EOS_LAYOUT_STYLE_LEN - 1] = '\0';
}

bool eos_layout_storage_save(const eos_layout_storage_t *ls,
                             const eos_app_manager_t *m)
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

    eos_result_t r = eos_storage_write_file(EOS_LAYOUT_FILE_PATH, buf, (size_t)off);
    return r == EOS_OK;
}

bool eos_layout_storage_load(eos_layout_storage_t *ls,
                             eos_app_manager_t *m)
{
    char *data = eos_storage_read_file(EOS_LAYOUT_FILE_PATH);
    if (!data) return false;

    char *save = NULL;
    char *line = strtok_r(data, "\n", &save);
    int order[EOS_APP_MAX];
    int order_count = 0;

    while (line) {
        if (strncmp(line, "style=", 6) == 0) {
            strncpy(ls->style, line + 6, EOS_LAYOUT_STYLE_LEN - 1);
            ls->style[EOS_LAYOUT_STYLE_LEN - 1] = '\0';
        } else if (strncmp(line, "order_count=", 12) == 0) {
            order_count = atoi(line + 12);
        } else if (strncmp(line, "order=", 6) == 0) {
            char *tok_save = NULL;
            char *tok = strtok_r(line + 6, ",", &tok_save);
            int idx = 0;
            while (tok && idx < EOS_APP_MAX) {
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

    eos_free(data);
    return true;
}
