
set(gc9a01_srcs "src/esp_lcd_panel_gc9a01.c")

idf_component_register( 
    SRCS "${gc9a01_srcs}"
    INCLUDE_DIRS "inc"
    REQUIRES driver esp_lcd
)
