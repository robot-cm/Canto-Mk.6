#ifndef COS_ROUND_KEYBOARD_H
#define COS_ROUND_KEYBOARD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    COS_RKB_MODE_EN = 0, /* 英文 QWERTY（左右两页） */
    COS_RKB_MODE_SYM,    /* 标点符号（三页: 第 3 页为方程/不等式运算页） */
    COS_RKB_MODE_NUM,    /* 数字（单页） */
} cos_rkb_mode_t;

/**
 * @brief 创建半圆键盘（适配 240x240 圆屏, 占用屏幕下半部分半圆）
 * @param parent 父对象（通常为全屏页面）
 * @return 键盘根对象
 */
lv_obj_t *cos_round_keyboard_create(lv_obj_t *parent);

/**
 * @brief 绑定输入框: 所有按键事件写入该 textarea
 */
void cos_round_keyboard_set_textarea(lv_obj_t *kb, lv_obj_t *ta);

/** @brief 设置输入法模式（EN/SYM/NUM） */
void cos_round_keyboard_set_mode(lv_obj_t *kb, cos_rkb_mode_t mode);

/** @brief 获取当前输入法模式 */
cos_rkb_mode_t cos_round_keyboard_get_mode(lv_obj_t *kb);

/**
 * @brief 测试注入按键
 * @param key 字符或控制标记: "@back"(退格) "@space"(空格)
 */
void cos_round_keyboard_send_key(lv_obj_t *kb, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* COS_ROUND_KEYBOARD_H */
