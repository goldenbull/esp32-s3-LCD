#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"

/* Waveshare ESP32-S3-LCD-1.47B pin assignments */
#define PIN_LED   38
#define PIN_MOSI  45
#define PIN_SCLK  40
#define PIN_CS    42
#define PIN_DC    41
#define PIN_RST   39
#define PIN_BL    46

/* Display geometry */
#define LCD_W     172
#define LCD_H     320
#define LCD_HOST  SPI2_HOST

/* ST7789 runs as a 240×320 chip; the 172-wide panel sits at column 34 */
#define X_OFFSET  34
#define Y_OFFSET   0

/* RGB565 background color */
#define COL_BG  0x0000  /* black */

/* Force the high bit of each RGB565 channel so colors are never near-black */
static uint16_t rand_color(void)
{
    return (uint16_t)(esp_random() | 0x8410);
}

/* ── 5×8 bitmap glyphs for h, e, l, o ──────────────────────────────────────
   One byte per row. Bit 7 = leftmost pixel; only the top 5 bits are used.  */
static const uint8_t GLYPHS[][8] = {
    /* h */ {0x80, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x88, 0x00},
    /* e */ {0x00, 0x00, 0x70, 0x88, 0xF8, 0x80, 0x70, 0x00},
    /* l */ {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x60, 0x00},
    /* o */ {0x00, 0x00, 0x70, 0x88, 0x88, 0x88, 0x70, 0x00},
};
static const int HELLO[]  = {0, 1, 2, 2, 3};   /* h e l l o → glyph indices */
static const int N_CHARS  = 5;
static const int GLYPH_W  = 5;
static const int GLYPH_H  = 8;
static const int SCALE    = 5;   /* each glyph pixel → 5×5 display pixels   */
static const int KERN     = 1;   /* gap between chars, in glyph pixels       */

static void draw_hello(uint16_t *fb)
{
    for (int i = 0; i < LCD_W * LCD_H; i++) fb[i] = COL_BG;

    int text_w = (N_CHARS * GLYPH_W + (N_CHARS - 1) * KERN) * SCALE;
    int x0 = (LCD_W - text_w) / 2;
    int y0 = (LCD_H - GLYPH_H * SCALE) / 2;

    for (int ci = 0; ci < N_CHARS; ci++) {
        const uint8_t *g = GLYPHS[HELLO[ci]];
        int cx = x0 + ci * (GLYPH_W + KERN) * SCALE;
        uint16_t color = rand_color();

        for (int row = 0; row < GLYPH_H; row++) {
            for (int col = 0; col < GLYPH_W; col++) {
                if (!((g[row] >> (7 - col)) & 1)) continue;
                for (int sy = 0; sy < SCALE; sy++)
                    for (int sx = 0; sx < SCALE; sx++) {
                        int px = cx + col * SCALE + sx;
                        int py = y0 + row * SCALE + sy;
                        if ((unsigned)px < LCD_W && (unsigned)py < LCD_H)
                            fb[py * LCD_W + px] = color;
                    }
            }
        }
    }
}

static void led_task(void *arg)
{
    led_strip_handle_t strip;
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_LED,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));
    led_strip_clear(strip);

    uint16_t hue = 0;
    while (1) {
        /* Full saturation, moderate brightness to avoid glare */
        led_strip_set_pixel_hsv(strip, 0, hue, 255, 60);
        led_strip_refresh(strip);
        hue = (hue + 1) % 360;
        vTaskDelay(pdMS_TO_TICKS(20));  /* full rainbow cycle ≈ 7 s */
    }
}

void app_main(void)
{
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 0);  /* backlight off during init */

    /* ── SPI bus ─────────────────────────────────────────────────────────── */
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * LCD_H * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    /* ── Panel IO (SPI → LCD command/data) ───────────────────────────────── */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = PIN_DC,
        .cs_gpio_num       = PIN_CS,
        .pclk_hz           = 40 * 1000 * 1000,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    /* ── ST7789 panel ────────────────────────────────────────────────────── */
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t dcfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        /* Tell ST7789 to expect little-endian data, matching ESP32 SPI output */
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &dcfg, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);   /* required for ST7789 */
    esp_lcd_panel_set_gap(panel, X_OFFSET, Y_OFFSET);
    esp_lcd_panel_disp_on_off(panel, true);

    /* ── Render and push framebuffer ─────────────────────────────────────── */
    size_t fb_sz = LCD_W * LCD_H * sizeof(uint16_t);
    uint16_t *fb = heap_caps_malloc(fb_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) fb = malloc(fb_sz);  /* fall back to internal RAM */
    assert(fb != NULL);

    draw_hello(fb);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb);
    free(fb);

    gpio_set_level(PIN_BL, 1);  /* backlight on */

    xTaskCreate(led_task, "led", 2048, NULL, 5, NULL);
}
