/**
 * @file cos_spotify_tree.h
 * @brief SD 懒加载音乐文件树(仅 .mp3 / .wav),参考 Texthub FM 的实现。
 *
 * 交付批次:Batch 3。
 *
 * ── 为什么懒加载 ────────────────────────────────────────────
 *   AGENTS.md §19 要求"绝不能让 SD IO 长时间阻塞 UI"。
 *   Texthub 早期版本因把整个 /sdcard/texthub 一次性枚举进固定堆而 OOM;
 *   本模块沿用其修正后的策略:
 *     · 只读取"当前展开的目录"(cos_storage_dir_open);
 *     · 收起时立即释放子树(free children);
 *     · 递归深度与单目录条目数均有上限。
 *
 * ── 与 Texthub 的差异 ───────────────────────────────────────
 *   · 只保留 .mp3 / .wav(不含 .txt/.md);
 *   · **自动递归预展开**:打开时按目录树逐层展开(每层有限),
 *     让用户直接看到歌曲而不用逐层点开;
 *   · 点击箭头 = 播放该文件(而不是打开文本)。
 */
#ifndef COS_SPOTIFY_TREE_H
#define COS_SPOTIFY_TREE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** 音乐根目录。 */
#define COS_SPOTIFY_MUSIC_DIR   "/sdcard/spotify"

/** 文件树最大递归深度(对齐 Texthub FM 的 TH_FM_DEPTH_MAX)。 */
#define COS_SPOTIFY_TREE_DEPTH_MAX   6

/** 单个目录最多读取的条目数(对齐 Texthub 的 TH_FM_MAX_CHILD)。 */
#define COS_SPOTIFY_TREE_CHILD_MAX   300

/** 展开后可见行数上限(对齐 Texthub 的 TH_FM_MAX_VISIBLE)。 */
#define COS_SPOTIFY_TREE_VISIBLE_MAX 200

typedef struct cos_spotify_node_s cos_spotify_node_t;

typedef struct
{
    cos_spotify_node_t *node;
    int                 depth;
} cos_spotify_tree_item_t;

typedef struct cos_spotify_tree_s cos_spotify_tree_t;

/**
 * @brief 创建文件树。
 * @param root_dir 根目录,传 NULL 用 COS_SPOTIFY_MUSIC_DIR
 * @note 只加载根目录的**第一层**(懒加载,与 Texthub FM 一致);
 *       不递归、不预展开。后续每展开一个目录才读一次 SD。
 */
cos_spotify_tree_t *cos_spotify_tree_create(const char *root_dir);

/** @brief 销毁文件树,释放全部节点。 */
void cos_spotify_tree_destroy(cos_spotify_tree_t *tree);

/**
 * @brief 把当前展开状态摊平成可见行数组。
 * @param out_count 输出行数
 * @return 内部数组指针(不可 free);无内容返回 NULL。
 */
const cos_spotify_tree_item_t *cos_spotify_tree_items(cos_spotify_tree_t *tree,
                                                     int *out_count);

/** @brief 展开/收起一个目录节点。返回 true 表示状态发生变化。 */
bool cos_spotify_tree_toggle(cos_spotify_tree_t *tree, cos_spotify_node_t *node);

/** @brief 节点是否是目录。 */
bool cos_spotify_tree_node_is_dir(const cos_spotify_node_t *node);

/** @brief 节点是否已展开。 */
bool cos_spotify_tree_node_expanded(const cos_spotify_node_t *node);

/** @brief 节点名(不含路径)。 */
const char *cos_spotify_tree_node_name(const cos_spotify_node_t *node);

/** @brief 节点完整路径。 */
const char *cos_spotify_tree_node_path(const cos_spotify_node_t *node);

/** @brief 节点显示用标签(目录带尾随 '/')。 */
const char *cos_spotify_tree_node_label(const cos_spotify_node_t *node);

/**
 * @brief 按路径收集同目录下的所有音频文件(供"上一首/下一首")。
 * @param path     当前文件路径
 * @param out_count 输出数量
 * @return 内部维护的路径数组(不可 free);失败返回 NULL。
 */
const char *const *cos_spotify_tree_siblings(const char *path, int *out_count);

/** @brief 当前树中歌曲总数(仅统计文件,不含目录)。 */
int cos_spotify_tree_song_count(const cos_spotify_tree_t *tree);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* COS_SPOTIFY_TREE_H */
