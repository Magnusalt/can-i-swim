#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_timer.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"


#define LCD_HOST SPI2_HOST
#define LCD_H_RES 280
#define LCD_V_RES 456
#define LCD_CS GPIO_NUM_9
#define LCD_CLK GPIO_NUM_10
#define LCD_D0 GPIO_NUM_11
#define LCD_D1 GPIO_NUM_12
#define LCD_D2 GPIO_NUM_13
#define LCD_D3 GPIO_NUM_14
#define LCD_RST GPIO_NUM_21
#define LCD_BPP 16
#define DRAW_BUF_LINES 152

static const char *TAG = "LVGL";


LV_IMG_DECLARE(beach);  // from the converted .c file
LV_FONT_DECLARE(my_font);


static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(2);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, const void *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    const lv_color_t *color_map = (const lv_color_t *)px_map;
    lv_draw_sw_rgb565_swap(color_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));

    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(disp);
}

static void lvgl_display_rounder_callback(lv_event_t *e) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 &= ~0x7U;
    area->y1 &= ~0x7U;
    area->x2 = (area->x2 & ~0x7U) + 7;
    area->y2 = (area->y2 & ~0x7U) + 7;
}

static void anim_x_cb(void *var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}

void app_main(void)
{
    // 1. Initialize SPI bus
    spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_CLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3,
        LCD_H_RES * DRAW_BUF_LINES * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Attach LCD to bus
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    co5300_vendor_config_t vendor_config = {
        .flags.use_qspi_interface = 1
    };

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // RGB element order
        .bits_per_pixel = LCD_BPP,
        .vendor_config = &vendor_config};
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 20, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    // 3. LVGL setup
    lv_init();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(disp, (lv_display_flush_cb_t)flush_cb);
    lv_display_set_user_data(disp, panel);
    lv_display_add_event_cb(disp, lvgl_display_rounder_callback, LV_EVENT_INVALIDATE_AREA, NULL);

    // Allocate buffer (1/4 screen)
    size_t buf_size = LCD_H_RES * DRAW_BUF_LINES * 2;
    
    ESP_LOGI(TAG, "Buffer size: %d bytes", (int)buf_size);

    lv_color_t *buf1 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_DMA);
    
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 4. Start LVGL tick timer
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lv_tick"};
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 2 * 1000)); // 2ms

    // --- 6. Flex layout

    lv_obj_t *img_bg = lv_image_create(lv_scr_act());
    lv_image_set_src(img_bg, &beach);
    lv_obj_set_size(img_bg, 280, 456);
    lv_obj_align(img_bg, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, 280, 456);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(overlay, LV_LAYOUT_FLEX);
    lv_obj_set_style_flex_flow(overlay, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_main_place(overlay, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(overlay, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(overlay, 20, 0);

    lv_obj_t *temp_label = lv_label_create(overlay);
    
    lv_label_set_text(temp_label, "20.4 °C");
    lv_obj_set_style_text_font(temp_label, &my_font, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFFFFF), 0);

    // Apply rotation in degrees * 10 (e.g. 90° = 900)
    lv_obj_set_style_transform_angle(temp_label, 900, 0);

    // 8. Loop
    while (1)
    {
        lv_timer_handler(); // runs animations, screen updates, etc.
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
