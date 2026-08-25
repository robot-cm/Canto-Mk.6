

#include "esp_lcd_panel_gc9a01.h"

static const char *TAG = "lcd_panel.gc9a01";

static esp_err_t panel_gc9a01_del(esp_lcd_panel_t *panel);
static esp_err_t panel_gc9a01_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_gc9a01_init(esp_lcd_panel_t *panel);
static esp_err_t panel_gc9a01_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_gc9a01_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_gc9a01_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_gc9a01_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_gc9a01_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_gc9a01_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    unsigned int bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_cal; // save surrent value of LCD_CMD_COLMOD register
} gc9a01_panel_t;

esp_err_t esp_lcd_new_panel_gc9a01(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    gc9a01_panel_t *gc9a01 = NULL;
    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    gc9a01 = calloc(1, sizeof(gc9a01_panel_t));
    ESP_GOTO_ON_FALSE(gc9a01, ESP_ERR_NO_MEM, err, TAG, "no mem for gc9a01 panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->color_space)
    {
    case ESP_LCD_COLOR_SPACE_RGB:
        gc9a01->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        gc9a01->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel)
    {
    case 16:
        gc9a01->colmod_cal = 0x55;
        break;
    case 18:
        gc9a01->colmod_cal = 0x66;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    gc9a01->io = io;
    gc9a01->bits_per_pixel = panel_dev_config->bits_per_pixel;
    gc9a01->reset_gpio_num = panel_dev_config->reset_gpio_num;
    gc9a01->reset_level = panel_dev_config->flags.reset_active_high;
    gc9a01->base.del = panel_gc9a01_del;
    gc9a01->base.reset = panel_gc9a01_reset;
    gc9a01->base.init = panel_gc9a01_init;
    gc9a01->base.draw_bitmap = panel_gc9a01_draw_bitmap;
    gc9a01->base.invert_color = panel_gc9a01_invert_color;
    gc9a01->base.set_gap = panel_gc9a01_set_gap;
    gc9a01->base.mirror = panel_gc9a01_mirror;
    gc9a01->base.swap_xy = panel_gc9a01_swap_xy;
    gc9a01->base.disp_on_off = panel_gc9a01_disp_on_off;
    *ret_panel = &(gc9a01->base);
    ESP_LOGD(TAG, "new gc9a01 panel @%p", gc9a01);

    return ESP_OK;

err:
    if (gc9a01)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(gc9a01);
    }
    return ret;
}

static esp_err_t panel_gc9a01_reset(esp_lcd_panel_t *panel)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;

    esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120)); /* GC9A01 SWRESET 后需 ≥120ms 恢复,期间命令被忽略 */

    return ESP_OK;
}

static esp_err_t panel_gc9a01_init(esp_lcd_panel_t *panel)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;

    /* Seeed 官方 GC9A01 1.28" init 序列(与 TFT_eSPI GC9A01_Init.h 逐条一致)
     * 关键: COLMOD=0x05(65K 16bit,必须与 RGB565 数据流匹配)
     *       INVON=0x21(颜色不反相,缺失会导致深色背景变亮/白色变黑) */
    esp_lcd_panel_io_tx_param(io, 0xEF, NULL, 0);
    esp_lcd_panel_io_tx_param(io, 0xEB, (uint8_t[]){0x14}, 1);
    esp_lcd_panel_io_tx_param(io, 0xFE, NULL, 0);
    esp_lcd_panel_io_tx_param(io, 0xEF, NULL, 0);
    esp_lcd_panel_io_tx_param(io, 0xEB, (uint8_t[]){0x14}, 1);
    esp_lcd_panel_io_tx_param(io, 0x84, (uint8_t[]){0x40}, 1);
    esp_lcd_panel_io_tx_param(io, 0x85, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(io, 0x86, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(io, 0x87, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(io, 0x88, (uint8_t[]){0x0A}, 1);
    esp_lcd_panel_io_tx_param(io, 0x89, (uint8_t[]){0x21}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8A, (uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8B, (uint8_t[]){0x80}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8C, (uint8_t[]){0x01}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8D, (uint8_t[]){0x01}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8E, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(io, 0x8F, (uint8_t[]){0xFF}, 1);
    esp_lcd_panel_io_tx_param(io, 0xB6, (uint8_t[]){0x00, 0x20}, 2);
    esp_lcd_panel_io_tx_param(io, 0x3A, (uint8_t[]){0x05}, 1);   /* COLMOD=65K 16bit */
    esp_lcd_panel_io_tx_param(io, 0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    esp_lcd_panel_io_tx_param(io, 0xBD, (uint8_t[]){0x06}, 1);
    esp_lcd_panel_io_tx_param(io, 0xBC, (uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(io, 0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x13}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x13}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC9, (uint8_t[]){0x22}, 1);
    esp_lcd_panel_io_tx_param(io, 0xBE, (uint8_t[]){0x11}, 1);
    esp_lcd_panel_io_tx_param(io, 0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    esp_lcd_panel_io_tx_param(io, 0xDF, (uint8_t[]){0x21, 0x0C, 0x02}, 3);
    esp_lcd_panel_io_tx_param(io, 0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    esp_lcd_panel_io_tx_param(io, 0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    esp_lcd_panel_io_tx_param(io, 0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    esp_lcd_panel_io_tx_param(io, 0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    esp_lcd_panel_io_tx_param(io, 0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    esp_lcd_panel_io_tx_param(io, 0xAE, (uint8_t[]){0x77}, 1);
    esp_lcd_panel_io_tx_param(io, 0xCD, (uint8_t[]){0x63}, 1);
    esp_lcd_panel_io_tx_param(io, 0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    esp_lcd_panel_io_tx_param(io, 0xE8, (uint8_t[]){0x34}, 1);
    esp_lcd_panel_io_tx_param(io, 0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    esp_lcd_panel_io_tx_param(io, 0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    esp_lcd_panel_io_tx_param(io, 0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    esp_lcd_panel_io_tx_param(io, 0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    esp_lcd_panel_io_tx_param(io, 0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    esp_lcd_panel_io_tx_param(io, 0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);   /* 60Hz */
    esp_lcd_panel_io_tx_param(io, 0x98, (uint8_t[]){0x3E, 0x07}, 2);
    esp_lcd_panel_io_tx_param(io, 0x36, (uint8_t[]){0x00}, 1);   /* MADCTL: rotation 0(RGB,无镜像,与 TFT_eSPI 一致) */
    esp_lcd_panel_io_tx_param(io, 0x35, NULL, 0);   /* Tearing Effect ON */
    esp_lcd_panel_io_tx_param(io, 0x21, NULL, 0);   /* INVON: 颜色不反相(必须!) */
    esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0);   /* Sleep Out */
    vTaskDelay(pdMS_TO_TICKS(120));                 /* GC9A01 SLPOUT 后需 ≥120ms 唤醒 */
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0);   /* Display ON */
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

static esp_err_t panel_gc9a01_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = gc9a01->io;

    // define an area of frame memory where MCU can access
    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]){
                                                     (x_start >> 8) & 0xFF,
                                                     x_start & 0xFF,
                                                     ((x_end - 1) >> 8) & 0xFF,
                                                     (x_end - 1) & 0xFF,
                                                 },
                              4);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]){
                                                     (y_start >> 8) & 0xFF,
                                                     y_start & 0xFF,
                                                     ((y_end - 1) >> 8) & 0xFF,
                                                     (y_end - 1) & 0xFF,
                                                 },
                              4);
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * gc9a01->bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_gc9a01_del(esp_lcd_panel_t *panel)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);

    if (gc9a01->reset_gpio_num >= 0)
    {
        gpio_reset_pin(gc9a01->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del gc9a01 panel @%p", gc9a01);
    free(gc9a01);
    return ESP_OK;
}

static esp_err_t panel_gc9a01_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;
    int command = 0;
    if (invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    esp_lcd_panel_io_tx_param(io, command, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_gc9a01_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;
    if (mirror_x)
    {
        gc9a01->madctl_val |= LCD_CMD_MX_BIT;
    }
    else
    {
        gc9a01->madctl_val &= ~LCD_CMD_MX_BIT;
    }
    if (mirror_y)
    {
        gc9a01->madctl_val |= LCD_CMD_MY_BIT;
    }
    else
    {
        gc9a01->madctl_val &= ~LCD_CMD_MY_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){gc9a01->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_gc9a01_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;
    if (swap_axes)
    {
        gc9a01->madctl_val |= LCD_CMD_MV_BIT;
    }
    else
    {
        gc9a01->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){gc9a01->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_gc9a01_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    gc9a01->x_gap = x_gap;
    gc9a01->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_gc9a01_disp_on_off(esp_lcd_panel_t *panel, bool off)
{
    gc9a01_panel_t *gc9a01 = __containerof(panel, gc9a01_panel_t, base);
    esp_lcd_panel_io_handle_t io = gc9a01->io;
    int command = 0;
    if (off)
    {
        command = LCD_CMD_DISPOFF;
    }
    else
    {
        command = LCD_CMD_DISPON;
    }
    esp_lcd_panel_io_tx_param(io, command, NULL, 0);
    return ESP_OK;
}
