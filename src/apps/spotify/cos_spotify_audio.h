/**
 * @file eos_spotify_audio.h
 * @brief Spotify 音频管线:文件 → 解码(MP3/WAV) → PCM 环形缓冲 → UAC 推送。
 *
 * 交付批次:Batch 3。
 *
 * ── 设计要点 ────────────────────────────────────────────────
 *   · **不新增 FreeRTOS 任务**:解码以"分片"方式在 LVGL 定时器回调中执行,
 *     每次最多解码 EOS_SPOTIFY_DECODE_SLICE_MS 毫秒的音频,保证 ui_task
 *     不被长时间占用(AGENTS.md §19:SD IO 不得长时间阻塞 UI)。
 *   · PCM 走**环形缓冲**(内部 RAM + DMA 安全),UAC 等时端点从缓冲取数据。
 *   · 变速(0.5x–4.0x)在**解码侧**完成:按速度比例重复/跳过来自解码器的
 *     PCM 帧,再写入环形缓冲;UAC 侧始终按固定采样率发送,不做重采样。
 *   · 所有 SD 访问经 eos_storage_* 封装,禁止裸 fopen/sdmmc_*(任务书 §3.3)。
 */
#ifndef EOS_SPOTIFY_AUDIO_H
#define EOS_SPOTIFY_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_USB_UAC_APP_ENABLE) && CONFIG_USB_UAC_APP_ENABLE

/** 环形缓冲容量(字节)。48kHz 立体声 16bit ≈ 192KB/s,
 *  取 32KB 约可缓冲 ~170ms,足够吸收 SD 读延迟。内部 RAM + DMA。 */
#define EOS_SPOTIFY_PCM_RING_BYTES     (32 * 1024)

/** 单次解码分片的时间预算(ms):控制单次 lv_timer 回调的占用时长。 */
#define EOS_SPOTIFY_DECODE_SLICE_MS    8

/** 支持的最大单文件尺寸,超出直接拒绝(防 SD 上的异常文件)。 */
#define EOS_SPOTIFY_MAX_FILE_BYTES     (64u * 1024u * 1024u)

typedef enum
{
    EOS_SPOTIFY_FMT_UNKNOWN = 0,
    EOS_SPOTIFY_FMT_MP3,
    EOS_SPOTIFY_FMT_WAV,
} eos_spotify_fmt_t;

typedef enum
{
    EOS_SPOTIFY_ADE_OK = 0,
    EOS_SPOTIFY_ADE_ERR_OPEN,      /* 文件打不开                       */
    EOS_SPOTIFY_ADE_ERR_FORMAT,    /* 非 MP3/WAV 或格式不支持          */
    EOS_SPOTIFY_ADE_ERR_MEM,       /* 内存不足                         */
    EOS_SPOTIFY_ADE_ERR_DECODE,    /* 解码器报错                       */
} eos_spotify_ade_err_t;

/** @brief 根据扩展名判断格式。 */
eos_spotify_fmt_t eos_spotify_audio_probe(const char *path);

/**
 * @brief 打开音频文件并初始化解码器。
 * @param path        文件路径(已校验在 /sdcard/spotify 内)
 * @param out_err     失败时写入错误码(可为 NULL)
 * @return true 成功;内部已清空 PCM 环形缓冲。
 */
bool eos_spotify_audio_open(const char *path, eos_spotify_ade_err_t *out_err);

/** @brief 关闭当前文件,释放解码器与缓冲。 */
void eos_spotify_audio_close(void);

/**
 * @brief 解码一片并推入环形缓冲(在 LVGL 定时器回调内调用)。
 * @return 本次产出的 PCM 字节数;<0 表示出错或到达文件尾。
 */
int32_t eos_spotify_audio_pump(void);

/**
 * @brief 把环形缓冲中的数据推给 UAC 等时端点。
 * @return 本次成功推送的字节数。
 */
int32_t eos_spotify_audio_flush_to_uac(void);

/** @brief 跳转到指定毫秒(尽力而为;WAV 精确,MP3 按帧对齐)。 */
void eos_spotify_audio_seek_ms(uint32_t ms);

/** @brief 相对当前位置跳转(±毫秒,可为负)。 */
void eos_spotify_audio_seek_relative_ms(int32_t delta_ms);

/** @brief 设置播放速度(0.5x–4.0x)。 */
void eos_spotify_audio_set_speed(float speed);

/** @brief 当前播放速度。 */
float eos_spotify_audio_get_speed(void);

/** @brief 当前播放位置(毫秒)。 */
uint32_t eos_spotify_audio_position_ms(void);

/** @brief 总时长(毫秒);未知时为 0。 */
uint32_t eos_spotify_audio_duration_ms(void);

/** @brief 是否已到达文件尾且环形缓冲已排空。 */
bool eos_spotify_audio_eos(void);

/** @brief 当前文件中解码出的声道数(1 或 2)。 */
uint8_t eos_spotify_audio_channels(void);

/** @brief 当前文件采样率(Hz)。 */
uint32_t eos_spotify_audio_sample_rate(void);

/* ── 音量(软件增益) ──────────────────────────────────────────
 * USB UAC 等时端点没有硬件音量寄存器,因此音量以**软件增益**施加在
 * PCM 上,在 flush_to_uac() 交给端点之前生效。
 * 取值范围与系统一致的 0–100,并复用现有配置键
 * EOS_CONFIG_KEY_SPEAKER_VOLUME_NUMBER 做持久化,保证与系统静音策略统一。 */

/** @brief 设置音量(0–100),并写入系统配置持久化。 */
void eos_spotify_audio_set_volume(int vol);

/** @brief 取当前音量(0–100),启动时从系统配置读取。 */
int eos_spotify_audio_get_volume(void);

/** @brief 是否静音(复用系统 mute 配置)。 */
bool eos_spotify_audio_is_muted(void);

#endif /* CONFIG_USB_UAC_APP_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* EOS_SPOTIFY_AUDIO_H */
