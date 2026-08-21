/**
 * @file eos_app_manager.c
 * @brief App registry + user ordering.
 */
#include "eos_app_manager.h"
#include <string.h>

void eos_app_manager_init(eos_app_manager_t *m)
{
    memset(m, 0, sizeof(*m));
    m->count = 0;
}

int eos_app_manager_register(eos_app_manager_t *m, const char *id,
                             const char *name, int icon_res)
{
    if (m->count >= EOS_APP_MAX) return -1;
    int slot = m->count;
    strncpy(m->apps[slot].id, id, EOS_APP_ID_LEN - 1);
    m->apps[slot].id[EOS_APP_ID_LEN - 1] = '\0';
    strncpy(m->apps[slot].name, name, EOS_APP_NAME_LEN - 1);
    m->apps[slot].name[EOS_APP_NAME_LEN - 1] = '\0';
    m->apps[slot].icon_res = icon_res;
    m->order[slot] = slot;     /* default display order == registration order */
    m->count++;
    return slot;
}

bool eos_app_manager_move(eos_app_manager_t *m, int from, int to)
{
    if (from < 0 || from >= m->count || to < 0 || to >= m->count) return false;
    if (from == to) return true;
    int val = m->order[from];
    if (from < to) {
        for (int i = from; i < to; i++) m->order[i] = m->order[i + 1];
    } else {
        for (int i = from; i > to; i--) m->order[i] = m->order[i - 1];
    }
    m->order[to] = val;
    return true;
}

int eos_app_manager_count(const eos_app_manager_t *m) { return m->count; }

const eos_app_info_t *eos_app_manager_at(const eos_app_manager_t *m, int display_pos)
{
    if (display_pos < 0 || display_pos >= m->count) return NULL;
    return &m->apps[m->order[display_pos]];
}

int eos_app_manager_find(const eos_app_manager_t *m, const char *id)
{
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->apps[m->order[i]].id, id) == 0) return i;
    }
    return -1;
}
