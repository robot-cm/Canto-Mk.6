/**
 * @file eos_spotify_tree.c
 * @brief SD 懒加载音乐文件树实现(仅 .mp3 / .wav)。
 *
 * ── 与 Texthub FM 的一致性 ──────────────────────────────────
 *   本模块**直接照搬** src/apps/texthub/eos_texthub.c 中 Lazy File Manager
 *   的设计,只把"文本文件过滤"换成"音频文件过滤":
 *
 *     Texthub                          → 本模块
 *     ---------------------------------------------------------------
 *     th_node_t (定长 name/path 数组)   → sp_node_t(同样定长数组)
 *     _fm_node_load() 两遍扫描           → _node_load() 两遍扫描
 *     n->loaded / n->expanded            → 同名同义
 *     _fm_children_free() 释放子树       → _node_children_free()
 *     _fm_build_items() 摊平为 (node,depth) → _build_items()
 *     _fm_node_unload()                  → _node_unload()
 *     TH_FM_DEPTH_MAX 6 / MAX_CHILD 300  → SP_TREE_DEPTH_MAX 6 / CHILD_MAX 300
 *
 *   关键点:**没有**任何"打开时预展开"逻辑。
 *   初始只有虚拟根节点的第一层被加载;用户点开哪个目录,才读哪个目录;
 *   收起时立即释放整棵子树。这与 Texthub 完全一致。
 *
 * ── 内存策略(与 Texthub 一致) ──────────────────────────────
 *   · 一个目录的全部子节点是**一次 malloc 的连续数组**(不是逐个节点分配);
 *   · name/path 为定长内嵌数组,不额外 malloc;
 *   · 数组整体随父节点一起 free,避免碎片;
 *   · 分配走 eos_malloc(PSRAM 优先由 eos_mem 决定)。
 */
#include "eos_spotify_tree.h"

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define EOS_LOG_TAG "SpotifyTree"
#include "eos_log.h"

#include "eos_mem.h"
#include "eos_service_storage.h"
#include "eos_spotify_audio.h"   /* eos_spotify_audio_probe() 判扩展名 */

/* ── 与 Texthub 对齐的限制常量 ────────────────────────────── */

#define SP_TREE_DEPTH_MAX   EOS_SPOTIFY_TREE_DEPTH_MAX    /* 6   */
#define SP_TREE_MAX_CHILD   EOS_SPOTIFY_TREE_CHILD_MAX    /* 300 */
#define SP_TREE_MAX_VISIBLE EOS_SPOTIFY_TREE_VISIBLE_MAX  /* 200 */

/* ── 节点(对照 Texthub 的 th_node_t) ─────────────────────── */
/* 结构体标签与 .h 中前置声明的 eos_spotify_node_s 必须一致。 */

typedef eos_spotify_node_t sp_node_t;

struct eos_spotify_node_s
{
    char    name[EOS_FS_NAME_MAX];    /* 条目名(内嵌,不单独 malloc) */
    char    path[EOS_FS_PATH_MAX];    /* 完整路径                     */
    bool    is_dir;
    bool    expanded;                 /* 是否已展开                   */
    bool    loaded;                   /* 是否已读取过子节点           */
    int     depth;                    /* 在树中的深度                 */
    int     child_count;
    sp_node_t *children;              /* 子节点数组(懒加载,一次 malloc) */
};

/* 摊平视图条目(对照 th_item_t)。.h 已定义 eos_spotify_tree_item_t。 */
typedef eos_spotify_tree_item_t sp_item_t;

/* ── 状态 ─────────────────────────────────────────────────── */

struct eos_spotify_tree_s
{
    sp_node_t *root;                 /* 虚拟根:path 即音乐目录 */
    char      *root_dir;

    sp_item_t *items;
    int        item_cap;

    int  song_count;
    bool warned_depth;
};

static bool _is_audio_ext(const char *name)
{
    return eos_spotify_audio_probe(name) != EOS_SPOTIFY_FMT_UNKNOWN;
}

/* ── 子树释放(对照 _fm_children_free) ────────────────────── */

/* 递归释放 n 的全部后代(含各自 children 数组),但不释放 n 本身。
 * n->children 是**一整块** malloc 的数组,内部节点无需单独 free。 */
static void _node_children_free(sp_node_t *n)
{
    if (n == NULL)
    {
        return;
    }
    if (n->children != NULL)
    {
        for (int i = 0; i < n->child_count; i++)
        {
            _node_children_free(&n->children[i]);
        }
        eos_free(n->children);
        n->children = NULL;
    }
    n->child_count = 0;
    n->expanded = false;
    n->loaded = false;
}

/* ── 懒加载(对照 _fm_node_load) ──────────────────────────── */

/**
 * @brief 读取一个目录的子节点(仅在尚未 loaded 时执行)。
 *
 * 与 Texthub 相同的两遍扫描:
 *   第一遍:数出符合条件的条目数(不分配);
 *   第二遍:一次性 malloc 整块数组并填充。
 * 这样目录内 N 个条目只产生 **1 次** 分配,而不是 N 次。
 *
 * @return true 表示加载到至少一个子节点。
 */
static bool _node_load(sp_node_t *n)
{
    if (n == NULL)
    {
        return false;
    }
    if (n->loaded)
    {
        return true;
    }
    if (n->depth >= SP_TREE_DEPTH_MAX)
    {
        n->loaded = true;
        return false;
    }

    /* 第一遍:计数 */
    eos_dir_t d = eos_storage_dir_open(n->path);
    if (d == EOS_DIR_INVALID)
    {
        n->loaded = true;
        EOS_LOG_W("tree: opendir FAIL %s", n->path);
        return false;
    }

    int cnt = 0;
    char name[EOS_FS_NAME_MAX];
    while (eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK)
    {
        if (name[0] == '\0' || name[0] == '.')
        {
            continue;
        }
        /* 这里不做 is_dir 判断(那需要额外的 stat),
         * 只按键数上限截断;真正的类型判断在第二遍做。 */
        if (++cnt > SP_TREE_MAX_CHILD)
        {
            cnt = SP_TREE_MAX_CHILD;
            break;
        }
    }
    eos_storage_dir_close(d);

    if (cnt == 0)
    {
        n->loaded = true;
        return false;
    }

    sp_node_t *arr = (sp_node_t *)eos_malloc_zeroed((size_t)cnt * sizeof(sp_node_t));
    if (arr == NULL)
    {
        n->loaded = true;
        EOS_LOG_W("tree: alloc %d nodes failed", cnt);
        return false;
    }

    /* 第二遍:填充 */
    d = eos_storage_dir_open(n->path);
    if (d == EOS_DIR_INVALID)
    {
        eos_free(arr);
        n->loaded = true;
        return false;
    }

    int i = 0;
    while (i < cnt && eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK)
    {
        if (name[0] == '\0' || name[0] == '.')
        {
            continue;
        }

        char full[EOS_FS_PATH_MAX];
        size_t dl = strlen(n->path);
        size_t nl = strlen(name);
        if (dl + 1 + nl + 1 > sizeof(full))
        {
            continue;   /* 路径过长,跳过 */
        }
        memcpy(full, n->path, dl);
        full[dl] = '/';
        strcpy(full + dl + 1, name);

        bool is_dir = eos_storage_is_dir(full);
        /* 与原 Texthub 的 _is_text_ext 对应:这里只保留目录 + 音频 */
        if (!is_dir && !_is_audio_ext(name))
        {
            continue;
        }

        sp_node_t *c = &arr[i];
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        strncpy(c->path, full, sizeof(c->path) - 1);
        c->path[sizeof(c->path) - 1] = '\0';
        c->is_dir = is_dir;
        c->depth = n->depth + 1;
        i++;

        if (i >= SP_TREE_MAX_CHILD)
        {
            break;
        }
    }
    eos_storage_dir_close(d);

    n->children = arr;
    n->child_count = i;
    n->loaded = true;

    EOS_LOG_I("tree: dir %s children=%d", n->path, i);
    return i > 0;
}

/* 卸载:释放子树但保留节点自身(对照 _fm_node_unload) */
static void _node_unload(sp_node_t *n)
{
    if (n == NULL)
    {
        return;
    }
    _node_children_free(n);
}

/* ── 摊平(对照 _fm_build_items) ──────────────────────────── */

static void _build_items(eos_spotify_tree_t *t, sp_node_t *n, int depth, int *cnt)
{
    if (*cnt >= SP_TREE_MAX_VISIBLE || n == NULL)
    {
        return;
    }
    t->items[*cnt].node = n;
    t->items[*cnt].depth = depth;
    (*cnt)++;

    /* 只有已展开的目录才递归(文件是叶子:点击即播放) */
    if (n->is_dir && n->expanded && n->children != NULL)
    {
        for (int i = 0; i < n->child_count && *cnt < SP_TREE_MAX_VISIBLE; i++)
        {
            _build_items(t, &n->children[i], depth + 1, cnt);
        }
    }
}

/* 递归统计歌曲数(仅计数,不分配) */
static void _count_songs(sp_node_t *n, int *out)
{
    if (n == NULL || !n->is_dir || !n->loaded || n->children == NULL)
    {
        return;
    }
    for (int i = 0; i < n->child_count; i++)
    {
        if (n->children[i].is_dir)
        {
            _count_songs(&n->children[i], out);
        }
        else
        {
            (*out)++;
        }
    }
}

/* ── 对外 API ─────────────────────────────────────────────── */

eos_spotify_tree_t *eos_spotify_tree_create(const char *root_dir)
{
    if (root_dir == NULL)
    {
        root_dir = EOS_SPOTIFY_MUSIC_DIR;
    }

    eos_spotify_tree_t *t = (eos_spotify_tree_t *)eos_malloc(sizeof(*t));
    if (t == NULL)
    {
        return NULL;
    }
    memset(t, 0, sizeof(*t));

    size_t rl = strlen(root_dir);
    t->root_dir = (char *)eos_malloc(rl + 1);
    if (t->root_dir == NULL)
    {
        eos_free(t);
        return NULL;
    }
    memcpy(t->root_dir, root_dir, rl + 1);

    /* 虚拟根节点:path 即音乐目录,不占用 depth 层级 */
    t->root = (sp_node_t *)eos_malloc_zeroed(sizeof(sp_node_t));
    if (t->root == NULL)
    {
        eos_free(t->root_dir);
        eos_free(t);
        return NULL;
    }
    strncpy(t->root->path, root_dir, sizeof(t->root->path) - 1);
    t->root->is_dir = true;
    t->root->depth = 0;

    t->item_cap = SP_TREE_MAX_VISIBLE;
    t->items = (sp_item_t *)eos_malloc((size_t)t->item_cap * sizeof(sp_item_t));
    if (t->items == NULL)
    {
        eos_free(t->root);
        eos_free(t->root_dir);
        eos_free(t);
        return NULL;
    }

    /* 懒加载:只加载根目录的第一层。不递归、不预展开。 */
    _node_load(t->root);

    return t;
}

void eos_spotify_tree_destroy(eos_spotify_tree_t *t)
{
    if (t == NULL)
    {
        return;
    }
    if (t->root != NULL)
    {
        _node_children_free(t->root);
        eos_free(t->root);
    }
    eos_free(t->root_dir);
    eos_free(t->items);
    eos_free(t);
}

const eos_spotify_tree_item_t *eos_spotify_tree_items(eos_spotify_tree_t *t, int *out_count)
{
    if (t == NULL || out_count == NULL)
    {
        return NULL;
    }
    int cnt = 0;
    /* 与 Texthub 相同:虚拟根自身不显示,只摊平其子节点 */
    if (t->root != NULL && t->root->children != NULL)
    {
        for (int i = 0; i < t->root->child_count && cnt < SP_TREE_MAX_VISIBLE; i++)
        {
            _build_items(t, &t->root->children[i], 0, &cnt);
        }
    }
    *out_count = cnt;
    return cnt ? t->items : NULL;
}

bool eos_spotify_tree_toggle(eos_spotify_tree_t *t, eos_spotify_node_t *n)
{
    (void)t;
    if (n == NULL || !n->is_dir)
    {
        return false;
    }

    if (n->expanded)
    {
        /* 收起:释放整棵子树(对照 Texthub 的 _fm_toggle_node) */
        _node_unload(n);
        return true;
    }

    /* 展开:加载一层(仅首次真正读 SD) */
    if (!n->loaded)
    {
        _node_load(n);
    }
    n->expanded = true;
    return true;
}

bool eos_spotify_tree_node_is_dir(const eos_spotify_node_t *n)
{
    return n ? n->is_dir : false;
}

bool eos_spotify_tree_node_expanded(const eos_spotify_node_t *n)
{
    return n ? n->expanded : false;
}

const char *eos_spotify_tree_node_name(const eos_spotify_node_t *n)
{
    return n ? n->name : "";
}

const char *eos_spotify_tree_node_path(const eos_spotify_node_t *n)
{
    return n ? n->path : "";
}

const char *eos_spotify_tree_node_label(const eos_spotify_node_t *n)
{
    return n ? n->name : "";
}

int eos_spotify_tree_song_count(const eos_spotify_tree_t *t)
{
    if (t == NULL)
    {
        return 0;
    }
    /* 只统计**已加载**部分,避免为了计数而全盘扫描。
     * 这与懒加载的语义一致:未展开的目录不计入。 */
    int n = 0;
    _count_songs((sp_node_t *)t->root, &n);
    return n;
}

/* ── 同目录兄弟文件(供上/下一首) ───────────────────────────
 * 这一项 Texthub 没有对应实现(它用 _scan_text_names 取相邻文本),
 * 语义等价:列出同一目录下的音频文件并按名排序。
 * 仅当用户在文件树里点开某首歌时才调用一次。 */

#define SIBLING_MAX  128
static char *s_siblings[SIBLING_MAX];
static int   s_sibling_count = 0;

static void _siblings_clear(void)
{
    for (int i = 0; i < s_sibling_count; i++)
    {
        if (s_siblings[i])
        {
            eos_free(s_siblings[i]);
            s_siblings[i] = NULL;
        }
    }
    s_sibling_count = 0;
}

const char *const *eos_spotify_tree_siblings(const char *path, int *out_count)
{
    if (out_count)
    {
        *out_count = 0;
    }
    if (path == NULL)
    {
        return NULL;
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
    {
        return NULL;
    }

    char dir[EOS_FS_PATH_MAX];
    size_t dl = (size_t)(slash - path);
    if (dl + 1 > sizeof(dir))
    {
        return NULL;
    }
    memcpy(dir, path, dl);
    dir[dl] = '\0';

    _siblings_clear();

    eos_dir_t d = eos_storage_dir_open(dir);
    if (d == EOS_DIR_INVALID)
    {
        return NULL;
    }

    char name[EOS_FS_NAME_MAX];
    while (s_sibling_count < SIBLING_MAX &&
           eos_storage_dir_read(d, name, sizeof(name)) == EOS_OK)
    {
        if (name[0] == '\0' || name[0] == '.')
        {
            continue;
        }
        char full[EOS_FS_PATH_MAX];
        size_t nl = strlen(name);
        if (dl + 1 + nl + 1 > sizeof(full))
        {
            continue;
        }
        memcpy(full, dir, dl);
        full[dl] = '/';
        strcpy(full + dl + 1, name);

        if (eos_storage_is_dir(full))
        {
            continue;
        }
        if (!_is_audio_ext(name))
        {
            continue;
        }

        size_t fl = strlen(full);
        char *copy = (char *)eos_malloc(fl + 1);
        if (copy == NULL)
        {
            break;
        }
        memcpy(copy, full, fl + 1);
        s_siblings[s_sibling_count++] = copy;
    }
    eos_storage_dir_close(d);

    /* 按文件名排序,保证上/下曲顺序稳定 */
    for (int i = 0; i < s_sibling_count; i++)
    {
        for (int j = i + 1; j < s_sibling_count; j++)
        {
            if (strcasecmp(s_siblings[i], s_siblings[j]) > 0)
            {
                char *tmp = s_siblings[i];
                s_siblings[i] = s_siblings[j];
                s_siblings[j] = tmp;
            }
        }
    }

    if (out_count)
    {
        *out_count = s_sibling_count;
    }
    return s_sibling_count ? (const char *const *)s_siblings : NULL;
}

#endif /* CONFIG_USB_UAC_APP_ENABLE */
