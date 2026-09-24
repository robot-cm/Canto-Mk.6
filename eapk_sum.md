查看运行时如何加载 main.js：
# JS App `.eapk` 完整流程（仅分析，未改文件）

## 1. 如何编译/打包 `.eapk`

`.eapk` 不是 C 编译产物，是 **Python 打包脚本把 JS 目录打包成的二进制包**（`EAPK` magic）：

```bash
 python3.11 scripts/cos_pkg_builder.py /home/erashaperavm/下载/CantoMk6-fork/apps/<app_name> /home/erashaperavm/下载/CantoMk6-fork/eapk-target/<alarm>.eapk --type app 

```

来源：`scripts/cos_pkg_builder.py`（打包逻辑）与 `simulator/CMakeLists.txt`（集成）。

**App 源码目录结构**（参考 `apps/clock/`、`apps/pmdemo/`）：
```
apps/<myapp>/
├── manifest.json   # 必填: id, name, version, minApiLevel, targetApiLevel
├── main.js         # 固定入口文件名 (COS_APP_SCRIPT_ENTRY_FILE_NAME)
└── icon.bin        # 48px LVGL 二进制图标
```
- `manifest.json` 示例见 `apps/clock/manifest.json`：`id: "com.cantomk6.clock"` 等。打包器校验必填字段为 `id/name/version/minApiLevel/targetApiLevel`（`read_manifest()`）。
- 图标生成：`scripts/icon/sync_app_icons.py` 把 `icon/*.webp` 同步为 48px `icon.bin`（BUILD.md §5）。
- 入口固定为 `main.js`，manifest 无 `entry` 字段概念。

**构建集成**：`simulator/CMakeLists.txt` 为每个 app 定义 `<id>_eapk` 目标（如 `clock_eapk`、`pmdemo_eapk`、`breach_eapk`……），内部就是调用 `cos_pkg_builder.py`。只改 JS 无需重编 C：

```bash
cmake --build simulator/build --target clock_eapk -j 8
```

## 2. 存储路径（重点）

| 环节 | 路径 |
|---|---|
| 构建中间产物（模拟器） | `simulator/build/fs/.sys/app/com.cantomk6.<id>.eapk` |
| **SD 卡安装源（模拟器）** | `simulator/build/fs/sdcard/apps/com.cantomk6.<id>.eapk` |
| **SD 卡安装源（真机）** | microSD 根目录 `apps/` 下 → 设备内路径 `/sdcard/apps/<任意文件名>.eapk` |
| 安装后解包目录 | `/.sys/app/apps/<pkg_id>/`（含 `manifest.json` + `main.js` + `icon.bin` + assets） |
| App 数据目录 | `/.sys/app/app_data/<pkg_id>/` |

来源：`cos_storage_paths.h`（`COS_SD_APPS_DIR "/sdcard/apps/"`、`COS_APP_INSTALLED_DIR "/.sys/app/apps/"`）。

**是否需要手动复制？**
- **模拟器**：不需要。`<id>_eapk` 目标 POST_BUILD 自动把 `.eapk` 复制到模拟 SD 卡 `sdcard/apps/`（`simulator/CMakeLists.txt` 各 `*_eapk POST_BUILD` 段）。
- **真机 ESP32-S3**：需要。打包出的 `.eapk` 文件需**手动复制到 SD 卡根目录 `apps/` 文件夹**（可用读卡器拷贝，或系统文件管理器若支持写）。开机或执行 `plugin scan` 后系统自动安装。

> 注意：运行实例读的是**解包目录** `/.sys/app/apps/<id>/`，不是 SD 上的 `.eapk`。改了 JS 重打包后若运行仍旧，删除该 app 的安装子目录再启动（boot 会重装新包），或 `plugin scan --force`（BUILD.md §5）。

## 3. 如何加载 `.eapk`

加载由 **Plugin Manager** 完成，三步链：

1. **扫描**：boot 时 `cos_core.c` 调用 `cos_plugin_manager_init()` → `cos_plugin_manager_scan()`，打开 `/sdcard/apps/`，筛 `.eapk`/`.ewpk`（`cos_plugin_manager.c`）。SD 缺失不算错误（Shell 仍可用）。
2. **去重+安装**：读包头取 `pkg_id`（`cos_pkg_read_header`），已在 `cos_app_list_contains(id)` 中则跳过（幂等）；否则 `cos_app_install(full)` → `cos_pkg_mgr_unpack()` 解包到 `/.sys/app/apps/<pkg_id>/`（`cos_app.c:503`）。
3. **运行**：Launcher 通过 `cos_app_get_installed()` / `cos_app_list_get_id(index)` 列出插件；启动时 script engine 从 `/.sys/app/apps/<pkg_id>/main.js` 加载执行。

**Shell 手动触发**：
```
plugin scan            # 只装新包
plugin scan --force    # 强制重装（用于更新后的包）
app list / app start <id> / app stop <id> / app disable <id>
```
（`cos_shell.c:1016` 附近为 `plugin` 子命令实现）

## 4. 权限与 API 说明

- **SDK 版本校验**：安装时 `minApiLevel > CANTOMK6_OS_API_LEVEL` 直接拒绝，返回 `COS_ERR_SDK_VERSION`（`cos_app.c:503-525`）。目前样例都填 `0`。
- **无额外权限声明**：manifest 无 permissions 字段，包内文件随解包直接可读。
- **安全**：安装解包目标目录名由 `pkg_id` 拼接（`COS_APP_INSTALLED_DIR "%s"`），`pkg_id` 来自包头（打包时来自 manifest `id`），建议使用规范域名式 ID（`com.cantomk6.*`）避免路径问题。
- **相关 API**：
  - `cos_app_install(eapk_path)` / `cos_app_uninstall(id)` / `cos_app_disable(id)` / `cos_app_enable(id)` — `framework/app/cos_app.h`
  - `cos_plugin_manager_scan(force, &result)` / `cos_plugin_manager_init()` — `services/plugin/cos_plugin_manager.h`
  - `cos_pkg_read_header()` / `cos_pkg_mgr_unpack()` — `framework/package/cos_pkg_mgr.h`

**一句话总结**：`cos_pkg_builder.py` 打包 JS 目录为 `.eapk` → 放到 SD 卡 `apps/`（模拟器自动、真机手动）→ 开机或 `plugin scan` 自动解包安装到 `/.sys/app/apps/<pkg_id>/` → Launcher 列出并运行 `main.js`。