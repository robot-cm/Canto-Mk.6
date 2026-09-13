/**
 * @file eos_spotify.h
 * @brief Spotify App(USB UAC 耳机音乐播放器)。
 *
 * 交付批次:Batch 1 骨架(Batch 2/3/4 在本骨架内逐步填充)。
 *
 * 用户行为:
 *   打开 App → 探测 USB-C 是否连接 UAC 耳机;
 *     未连接 / 非音频设备 / 端口被调试台占用 → 全屏错误页(英文 errmsg)+ 可滑动退出;
 *     已连接 → 进入 /sdcard/spotify/ 文件树,点箭头播放 MP3/WAV;
 *   播放态:顶部歌名 / 中部三行歌词(中间高亮)/ 下部进度条 + 上下曲 + ±15s;
 *           右侧音量弧(120°) / 左侧速度弧(120°),长按 1s 滑动调节、长按 3s 复位;
 *   任意失败 → 显示 errcode/errmsg,绝不影响系统其它部分。
 *
 * 设计约束(见 docs/spotify_impact_analysis.md):
 *   - 完全自包含,不改动 Alarm/Album/Texthub/电源管理/启动流程;
 *   - 不新增应用层 FreeRTOS 任务(仅 TinyUSB Host 内部任务);
 *   - SD / USB PHY 互斥复用 USB MSC 已验证的 board_sd_* / usb_phy 机制;
 *   - 全仓新增点均由 CONFIG_USB_UAC_APP_ENABLE 包裹,关闭时产物与改动前一致。
 */
#ifndef EOS_SPOTIFY_H
#define EOS_SPOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/**
 * @brief 进入 Spotify App(native app entry,由 App 列表点击触发)。
 * @note 必须在 LVGL(ui_task)上下文调用。
 */
void eos_spotify_enter(void);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_H */
