# 页面切换层级审计（apps + src/apps）

统计口径：**为完成一个功能，用户/代码需要跨越多少个"页面容器"**。

- **层级 0**：单页内子部件增删（仅用一个 activity，内容重建）
- **层级 1**：同 activity 内 HIDDEN 显隐 / 状态机切换（无 activity 栈增长）
- **层级 2**：叠加遮罩面板（overlay，可直接点关闭）
- **层级 3**：入 activity 栈（`cos_activity_create` + `cos_activity_enter`）；返回需 1 次 `back`
- **层级 4+**：连续入栈 ≥2 次（"页中页中页"）

---

## 一、总览表

| App | 代码位置 | 页面数 | 最大栈深 | 最差路径 | 评级 |
|---|---|---|---|---|---|
| Settings | `src/apps/settings/cos_settings.c` | 主 + 9 子页 + App详情 + 权限页 | 4 | 设置 → Apps → 应用详情 → 权限 → 选择态 | 过深 |
| Spotify | `src/apps/spotify/cos_spotify.c` + `cos_spotify_player.c` | 5 状态 + 2 屏 | 1（单 activity） | 等待/错误 → 文件树 → 播放 | 合理 |
| EqSolver | `src/apps/eqsolver/cos_eqsolver.c` | 2 屏 + 1 编辑页 | 2 | 输入 → 结果 → 编辑框 | 合理 |
| Album | `src/apps/album/cos_album.c` | 1 页 + 2 overlay + 1 tile 模式 | 1 | 缩略图 ⇄ 1:1 / FM overlay / Stars overlay | 合理 |
| Texthub | `src/apps/texthub/cos_texthub.c` | 1 页 + 2 overlay | 1 | 阅读 ⇄ FM overlay / Stars overlay | 合理 |
| Dictionary | `src/apps/dictionary/cos_dictionary.c` | 4 模式（idle/输入/列表/详情） | 1 | 列表 ⇄ 详情（左滑） | 合理 |
| Files | `src/apps/files/cos_files.c` | LIST / VIEW | 1 | 列表 ⇄ 查看 | 合理 |
| USB MSC | `src/apps/usb_msc/cos_usb_msc.c` | INIT/WAIT/ACTIVE/DONE | 1 | 单页状态机 | 合理 |
| ProfMonitor | `src/apps/profmonitor/cos_profmonitor.c` | 1 页（4 资源循环） | 1 | 无 | 合理 |
| FlashLight | `src/apps/flash_light/cos_flash_light.c` | 2 pager 页 | 1 | 手电筒 ⇄ 色板 | 合理 |
| Gallery | `src/apps/gallery/cos_gallery.c` | 1 页 | 1 | 无（`_gallery_show()` 换图） | 合理 |
| Power Off | `src/apps/power_off/cos_power_off_page.c` | 3 阶段 | 1（非 activity） | 关机图标 → 选择 → 定时设置 | 合理 |
| Test (诊断) | `src/apps/test/` | 1 主列表 + ~15 个子页 | 2 | 主列表 → 任一子页 | 合理（工具类） |
| Timer (JS) | `apps/timer/main.js` | 2 容器 | 1 | Home ⇄ Edit；另 3 行分页 | 合理 |
| Alarm (JS) | `apps/alarm/main.js` | 3 容器 | 1 | Home / Edit / Ring | 合理 |
| Breach (JS) | `apps/breach/main.js` | 3 页 | 1 | Boot → Hack → Result | 合理 |
| Calculator (JS) | `apps/calculator/main.js` | 1 页（2 键位表） | 1 | 无 | 合理 |
| Calendar (JS) | `apps/calendar/main.js` | 1 页 | 1 | 无 | 合理 |
| Stopwatch / GlobalTime | `apps/*/main.js` | 1 页 | 1 | 无 | 合理 |

---

## 二、逐项证据

### Settings — 唯一明显过多的 app

`_create_activity_with_header()`（`cos_settings.c:94`）每次调用即入栈一层。子页清单：

| 子页 | 位置 | 进入方式 |
|---|---|---|
| Wi-Fi | `cos_settings.c:549` | `cos_settings.c:1881` |
| VPN | `cos_settings.c:622` | `cos_settings.c:1884` |
| Bluetooth | `cos_settings.c:317` | `cos_settings.c:1890` |
| Display | `cos_settings.c:763` | `cos_settings.c:1893` |
| Notification | `cos_settings.c:799` | `cos_settings.c:1899` |
| PWD | `cos_settings.c:1674` | `cos_settings.c:1902` |
| Apps（应用列表） | `cos_settings.c:1327` | `cos_settings.c:1908` |
| General | `cos_settings.c:1846` | `cos_settings.c:1911` |
| Device Info | `cos_settings.c:1765` | `cos_settings.c:1857`（General 内） |
| 应用详情 | `cos_settings.c:1094` | `_app_btn_create` → `cos_settings.c:1292` |
| 权限列表 | `cos_settings.c:969` | `cos_settings.c:1176` → `_settings_perm_entry_clicked_cb` |

最深路径（栈深 4）：

```
Settings(主)
 └─ Apps                cos_settings.c:1327  (enter :1358)
     └─ 应用详情        cos_settings.c:1094  (enter :1214)
         └─ 权限列表    cos_settings.c:969   (enter :1040)
             └─ 权限状态 radio 页  cos_settings.c:1080  cos_radio_page_show()
```

返回需连续 4 次 back。预期代码表现：

- 每一层都是独立 activity，`on_destroy` 时整棵 view 树销毁再重建。
- `General` 页只有 1 个按钮（Device Info）：`:1849` 建 activity → `:1856` 加唯一入口 → `:1858` enter。这层属于"零收益跳转"，可直接并入 Settings 主列表。
- `Notification` 页是纯空壳：`:802` 建 activity → `:804` 建 list → `:805` `LV_UNUSED(list)` → `:806` `// TODO` → `:807` enter。用户点进去只能看到一个空列表。
- 权限状态选择用 `cos_radio_page`（栈 +1，`cos_settings.c:1087`），函数返回时靠 `cos_activity_back()`（`cos_settings.c:1065`）回跳，属于标准的"选择即回跳"模式，可接受。

建议收敛（按收益排序）：
1. `Notification`（`:799-808`）删除入口按钮（`:1899`）或整页删除——进去是空的。
2. `General`（`:1846-1859`）并入 Settings 主列表，Device Info 按钮直接挂主列表（省一层）。
3. 权限列表（`:969`）改为应用详情页（`:1094`）内联展开区块，省一层；最深路径从 4 降到 3。

### Album — 2 个全屏 overlay + 1 个模式

- FM overlay：`_fm_open()` `cos_album.c:1561`（`lv_obj_create` 全屏 mask + panel），关闭 `_fm_close()` `:1377`。
- Stars overlay：`_stars_open()` `:2013`，关闭 `_stars_close()` `:1861`。
- 1:1 缩放态：`_tile_enter()` / `_tile_exit()`，由双击触发（`:2239`）。
- 入口：FM 由底部胶囊 `s_fm_btn`（`:2340`）触发，Stars 由 `s_star_btn` → `_star_btn_cb`（`:2104`）触发。

overlay 不增加 activity 栈，成本仅是一次 `lv_obj_create/delete`，**不算页面过多**。

⚠️ 与 Texthub 不一致（预期代码表现差异）：

```2368:2373:src/apps/album/cos_album.c
static bool _swipe_back_cb(cos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    return s_tile_mode;
}
```

专辑的 swipe_back 只处理 `s_tile_mode`，**没有先关闭 Stars / FM overlay**。即在 Stars 或 FM 面板打开时右滑，会直接退出整个 App（`s_tile_mode` 为 false → `return false` → 框架执行返回），而不是先关面板。对照 Texthub 的正确写法：

```1815:1822:src/apps/texthub/cos_texthub.c
static bool _on_swipe_back(cos_activity_t *self, lv_dir_t dir)
{
    (void)self;
    (void)dir;
    if (s_stars_open) { _stars_close(); return true; }
    if (s_fm_open)    { _fm_close();    return true; }
    return false;
}
```

修复方式：在 `_swipe_back_cb` 开头补上同样的两行 overlay 先关判断，再 `return s_tile_mode`。

### Texthub — 同构实现

- FM overlay：`_fm_open()`（`cos_texthub.c:1217` 附近 root 构建，`_on_enter` 末尾 `:1901` 调用）。
- Stars overlay：`_stars_open()` / `_stars_close()`（`:1574`、`:1583` 段）。
- `_on_destroy` `:1904` 里按 `stars_close → fm_close → free_text` 顺序释放。

与 Album 完全同一套模式，合理。

### Dictionary — 4 个"模式"共用一个 view

状态变量：`cos_dictionary.c:627-632`

```628:632:src/apps/dictionary/cos_dictionary.c
static bool s_fuzzy_mode;        /* 当前处于模糊结果列表模式 */
static bool s_fuzzy_more;        /* 还有更多匹配可加载 */
static int  s_fuzzy_y;           /* 列表当前渲染 y */
static char s_exact_word[DICT_WORD_CAP];    /* 精确命中词(非空 = 列表顶部置顶项) */
```

- idle：`_show_idle()` `:1057`（`lv_obj_add_flag(HIDDEN)`，不销毁）
- 输入：`_open_input()` `:677` / `_do_search()` `:690`（textarea 动画）
- 列表：`_render_search_list()` `:984`（`lv_obj_clean(s_scroll)` + 重建）
- 详情/消息：`_render_result()` `:768` / `_show_msg()` `:1039`（同样 `lv_obj_clean`）

输入态是"就地展开"，不新增容器：胶囊隐藏 → 黑色遮罩 `s_dim` 出现 → textarea 由圆点拉伸为胶囊（`_anim_ta`），收起动画结束回调 `_collapse_done()` `:667` 才隐藏遮罩并执行查询：

```667:674:src/apps/dictionary/cos_dictionary.c
static void _collapse_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_HIDDEN);
    _run_lookup();
}
```

查询结果落地为两种互斥的列表/详情（`_run_lookup()` `:707`）：

```723:727:src/apps/dictionary/cos_dictionary.c
    if (!dict_lookup(s_query, s_result, sizeof(s_result))) {
        /* 精确未命中 → 前缀匹配列表(懒加载) */
        _render_search_list();
        return;
    }
```

`_on_swipe_back` `:1237` 只在 `s_detail_mode` 时回列表并 `return true`，其余放行退出。这是"层级 1"，**没有额外 activity**，设计正确。

### Files — LIST / VIEW 两态

`_files_build_list()` `cos_files.c:113`（`_files_clear_content()` → 重建行）、`_files_open_viewer()` `:195`。`state` 字段 + Back 按钮显隐切换，`s_fctx.back_btn` 在 `:338` 初始隐藏、`:204` 显示。单 activity 双态，合理。

### EqSolver — 主页 2 屏 + 编辑子页

- 主 activity 内 `SCR_INPUT` ⇄ `SCR_RESULT`：`_build_input()` `cos_eqsolver.c:308`、`_build_result()` `:363`，切换靠 `_clear_cont()` `:96`。
- `_on_swipe_back` `:577` 在 RESULT 时回 INPUT 并 `return true`。
- 编辑页是真 activity：`_open_editor()` `:247`（`enter` `:273`）、`_open_vars_editor()` `:278`（复用同一 lifecycle）。栈深 2，合理。

### Spotify — 状态机 + 双屏，非 activity 栈

`cos_spotify.c` 的 stage 枚举（`STAGE_INIT / PROBE / TREE / PLAYING / DONE`），`_tick()` `:396` 驱动：

- 等待页：`_show_waiting()` `cos_spotify.c:215`
- 错误页：`_show_error()` `:229`
- 文件树 / 播放：`_show_player()` `:285` → `cos_spotify_player_create()`

player 内部两屏靠 HIDDEN 切换：

```319:342:src/apps/spotify/cos_spotify_player.c
static void _show_tree(cos_spotify_player_t *p)
...
static void _show_play(cos_spotify_player_t *p)
```

`_swipe_back` `cos_spotify.c:493`：播放态左滑 → `cos_spotify_player_show_browser()` 回树并消费手势（`:503-507`），第二次才退出。合理，且避免了 activity 栈。

⚠️ 顺带发现一处**预期代码表现不符**：播放页左上角返回按钮 `_btn_back_cb` `cos_spotify_player.c:487` 只调用 `p->on_back`，

```487:498:src/apps/spotify/cos_spotify_player.c
static void _btn_back_cb(lv_event_t *e)
{
    cos_spotify_player_t *p = (cos_spotify_player_t *)lv_event_get_user_data(e);
    ...
    if (p->on_back)
    {
        p->on_back(p->user);
    }
}
```

而宿主实现的 `_player_on_back()` 是空函数：

```269:275:src/apps/spotify/cos_spotify.c
/* 播放界面回调:用户在播放态点了返回 → 回文件树(此处无需额外动作,
 * player 内部已切换页面)。 */
static void _player_on_back(void *user)
{
    (void)user;
    COS_LOG_I("Spotify: back to browser");
}
```

但 player 内部 `_btn_back_cb` **没有**调用 `_show_tree(p)`，`cos_spotify.c` 的 `s_stage` 也不会从 `STAGE_PLAYING` 回落到 `STAGE_TREE`（`:479` 每 tick 按 `is_playing` 重算，播放中仍为 PLAYING）。结果：点左上角 `<` 只打日志，界面停在播放页。

### ProfMonitor — 单页 4 资源

`_pm_switch_cb` `cos_profmonitor.c:217` 仅改 `s_res` 并重绘同一组对象（`:225-226`），无页面。

### USB MSC — 单页状态机

`STAGE_INIT / WAIT_ENUM / ACTIVE / DONE`（`cos_usb_msc.c:80`），`_tick` `:308` 内切换 `_show_ok()` / `_show_error()`。单 activity。

### FlashLight — card_pager 2 页

`cos_flash_light.c:1135` 创建 pager，页 0 手电筒、页 1 色板（`:1141`、`:1147`）；切换由 `_flash_light_card_pager_page_changed_cb` `:853` 处理，同一 activity 内水平滑动，无栈增长。

### Gallery — 单页翻图

`_gallery_show(int idx)` `cos_gallery.c:119` 只换当前图片与计数，`_gallery_scan()` + `_gallery_show(0)` 在 `_on_enter` 内完成（`:252-253`）。单 activity。

### Power Off — 阶段淡出重建，不进 activity 栈

`_stage_t` 三态（`cos_power_off_page.c:29-34`）：`_STAGE_NONE`（红色关机按钮）→ `_STAGE_CHOOSE` → `_STAGE_TIMER`。

切换方式不是 HIDDEN，而是**淡出后销毁整棵阶段容器再重建**：

```62:73:src/apps/power_off/cos_power_off_page.c
static void _stage_fade_to(_stage_t next)
{
    s_next_stage = next;
    ...
    lv_anim_set_var(&a, s_stage);
    lv_anim_set_exec_cb(&a, _stage_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 250);
    lv_anim_set_ready_cb(&a, _stage_ready_cb);
    lv_anim_start(&a);
}
```

`_stage_ready_cb()` `:55` 在动画结束后 `lv_obj_delete(a->var)` 再 `_stage_create(s_next_stage)`。这是"层级 1 的变体"（销毁式切换），不涉及 activity 栈；开销为每次切换重建一次子树，与 3 个阶段的规模相称。

### Test (诊断) — 唯一"入口即 activity"的另一处

`cos_test.c` 内 `cos_activity_create` 出现 6 次（`:414`、`:708`、`:770`、`:840`、`:1952`、`:2054`），另有各子模块各自的 create（`permission/`、`event/`、`package/`、`sensor/`、`battery/`、`cos_test_framework.c`、`cos_test_input_page.c`）。

但与 Settings 不同：**主测试列表把所有入口压平在一页**，点进去即子页，栈深恒为 2，不存在"页中页"。作为诊断工具可以接受。

### JS apps

| 文件 | 页面容器 | 切换 |
|---|---|---|
| `apps/timer/main.js` | `homeC` / `editC` | `showHome()` `:466`、`showEdit()` `:472`（HIDDEN）；另有 `page` 变量 `:163` 做 3 条/页翻页 |
| `apps/alarm/main.js` | `homeC` / `editC` / `ringC` | `showHome()` `:656`、`showEdit()` `:662`、`showRing()` `:668` |
| `apps/breach/main.js` | `pageBoot` / `pageHack` / `pageResult` | `makePage()` `:211`，`fadeTo()` `:243`；Result 作半透明 overlay 压在 Hack 上（`:229`、`:246`） |
| `apps/calculator/main.js` | 单页 | `toggleMode()` `:297` 只改键位表 `STD/ADV` 并 `updateGrid()` `:281` |
| `apps/calendar/main.js` | 单页 | `paintAll()` `:490`、`showInfo()` `:509` 均原地更新 |

Timer 的 `page` 翻页（`:188`、`:197` 触发 `paintHome()`）属于**列表分页**，不是页面切换：

```210:213:apps/timer/main.js
    var n = tasks.length;
    var tp = Math.max(1, Math.ceil(n / 3));
    if (page >= tp) page = tp - 1;
```

与"Home ⇄ Edit"并存时容易被误认为多页，实际只占 1 个容器。

Timer 的 Home→Edit 路径是完整的 2 态闭环（`openNew()` `:445` → `showEdit()`；`okNew()` `:452` → `paintHome()` + `showHome()`；CANCEL 同 `:440`）。

Alarm 的 Ring 页是**由定时器强制进入**的第 3 态（`startRing()` `:621` → `showRing()` `:626`），`stopRing()` `:629` 按 `editing` 是否为空决定回到 Edit 还是 Home：

```639:639:apps/alarm/main.js
    if (editing) { paintEdit(); showEdit(); } else { page = 0; paintHome(); showHome(); }
```

三态互斥且只有一个容器可见，属于合理的"系统级打断页"。

Breach 的三页是**互斥 + fade 过渡**，且 Result 设计成半透明 overlay 叠在 Hack 上：

```243:247:apps/breach/main.js
function fadeTo(target) {
    pageBoot.addFlag(lv.OBJ_FLAG_HIDDEN);
    pageResult.addFlag(lv.OBJ_FLAG_HIDDEN);
    if (target !== pageResult) pageHack.addFlag(lv.OBJ_FLAG_HIDDEN);  // Result 作 overlay：保留 Hack 作暗化底
    target.removeFlag(lv.OBJ_FLAG_HIDDEN);
```

单 activity、三容器，栈深恒为 1。

---

## 三、结论

1. **真正"页面切换过多"的只有 `src/apps/settings/cos_settings.c`**：11 个子页全部 `cos_activity_create` + `cos_activity_enter`，最深 4 层，且其中 `Notification`（空 TODO 页，`:806`）与 `General`（单按钮页，`:1857`）属于纯冗余跳转。
2. 其它 C app 一律采用 **单 activity + 状态/overlay** 模式；JS app 一律 **HIDDEN 容器切换**。二者都不增长栈，符合这套 240×240 圆屏的交互预期。
3. 附带发现 1 处行为不符：Spotify 播放页 `<` 返回键不生效（见上文 `_btn_back_cb` / `_player_on_back`）。
